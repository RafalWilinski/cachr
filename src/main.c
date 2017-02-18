#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "libs/ini.h"
#include "error.h"
#include "libs/stack.h"
#include "libs/uthash.h"
#include "configutils.h"
#include "netutils.h"
#include "utils.h"
#include "libs/picohttpparser.h"

/* Size of buffer/chunk read */
#define BUFSIZE 4096

/* Constant message send with every tcp request */
static const char CONNECTION_CLOSE_MSG[] = "Connection: close";

/* Cached sockaddr_in structure as we're calling the same target */
struct sockaddr_in target_serv_addr;

/* Head of cache data structure */
struct cache_entry *cache = NULL;

/* Head of socket-request pairs dictionaryy structure */
struct socket_request_pair *pairs = NULL;

/* Number of sockets connected in fds array */
int sck_cnt = 1;

/* Stack of available indexes in fds array */
stackT freeIndexesStack;

/* Parsed configuration structure */
configuration cfg;

struct cache_entry {
  u_int64_t key;
  char* buffer;
  long timestamp;
  u_int32_t bytes;
  struct UT_hash_handle hh;
};

struct socket_request_pair {
  u_int64_t key;
  char* buffer;
  int idx;
  int ttl;
  struct UT_hash_handle hh;
};

int initialize_new_socket() {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    handle_error(sockfd, errno, "ERROR opening socket");

  printf("Connecting to %s:%d\n", cfg.target_host, cfg.target_port);

  if (connect(sockfd, (struct sockaddr *) &target_serv_addr, sizeof(target_serv_addr)) < 0)
    handle_error(1, errno, "Error while connecting");

  return sockfd;
}

int get_ttl_value(char *header_value) {
  char *separator = "=";
  char *b = strtok(header_value, separator);
  char *c = strtok(NULL, "");
  printf("c: %s\n", c);
  return atoi(c);
};

void serve_response_from_cache(struct cache_entry *found_entry, int fd, int i) {
  printf("Serving response from cache (%d bytes) to fd: %d\n", found_entry->bytes, fd);
  ssize_t sent_bytes = write(fd, found_entry->buffer, found_entry->bytes);
  if (sent_bytes > 0)
    printf("Cached response. %d bytes sent.\n", (int) sent_bytes);
  else
    printf("Failed to respond to requester from cache... %d\n", (int) sent_bytes);

  close(fd);
  StackPush(&freeIndexesStack, (stackElementT) i);
}

struct connection_info {
  int sck;
};

/*
 * Statuses:
 * -1: Receiving data from requester
 *  1: Sending data to target
 *  2: Receiving data from target
 *  3: Sending back data to requester
 */
void* handle_tcp_connection(void *ctx) {
  struct connection_info *conn_info = ctx;
  int read_timeout = -1, write_timeout = -1, status = -1, request_content_length = -1, response_content_length = -1;
  u_int64_t key;
  struct cache_entry *found_entry = NULL;
  char *buffer = malloc(BUFSIZE);
  buffer = memset(buffer, 0, BUFSIZE);

  struct pollfd *fds = (struct pollfd *) calloc(1, sizeof(struct pollfd));
  fds[0].fd = conn_info->sck;
  fds[0].events = POLLIN;

  while (poll(fds, (nfds_t) 1, read_timeout) && (status != 0)) {
    if (fds[0].revents & POLLHUP) {
      printf("Fd: %d was disconnected.\n", conn_info->sck);
    } else if (fds[0].revents & POLLNVAL) {
      printf("Fd: %d is invalid.\n", conn_info->sck);
    } else if (fds[0].revents & POLLERR) {
      printf("Fd: %d is broken.\n", conn_info->sck);
    } else if (fds[0].revents & POLLIN && status == -1) {
      /* Handle Incoming Request to proxy */
      printf("POLLIN - %d\n", conn_info->sck);
      fds[0].revents = 0;
      size_t size = 0;
      ssize_t rsize = 0, capacity = BUFSIZE;

      while ((rsize = read(fds[0].fd, buffer + size, capacity - size))) {
        /* Reading should be continued later or end of transmission */
        if (rsize == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          printf("would block\n");
          break;
        }

        /* Else (positive number of bytes) */
        size += rsize;

        /* Each dot informs about chunk of data, just for debugging purposes */
        printf("Receiving bytes: %d\n", rsize);

        if (size == capacity) {
          printf("Reallocating buffer to capacity: %d\n", (int) (capacity * 2));
          capacity *= 2;
          buffer = realloc(buffer, (size_t) capacity);

          if (buffer == NULL) {
            printf("Failed to rellocate the buffer!\n");
            exit(EXIT_FAILURE);
          }
        }
      }

      int ttl = cfg.ttl, i;
      char *method, *path;
      size_t method_len, path_len, num_headers;
      int minor_version, pret;
      struct phr_header headers[100];
      num_headers = sizeof(headers) / sizeof(headers[0]);

      pret = phr_parse_request(buffer, strlen(buffer), &method, &method_len, &path, &path_len,
                               &minor_version, headers, &num_headers, 0);

      /* Request is incomplete */
      if (pret == -2) break;

      /* Parse Error */
      if (pret == -1) {
        printf("Parse Error! rsize=%d, buffer:\n%s\n", (int) rsize, buffer);
        break;
      }

      /* Else pret = number of bytes consumed */

      status = 1;
      printf("Request parsed!\n");

      /* In request find header called "TTL" and parse it's value to integer */
      for (i = 0; i != num_headers; ++i) {
        if (headers[i].name == "Cache-Control") {
          if (headers[i].value == "no-cache" || headers[i].value == "no-store") ttl = 0;
          else if (headers[i].value == "max-age") {
            /* Parse "max-age=X" */
            ttl = get_ttl_value((char *) headers[i].value);
          }

          break;
        } else if (headers[i].name == "Content-Length") {
          request_content_length = atoi(headers[i].value);
        }
      }

      /* Content-Length header was present and it's value is bigger than downloaded bytes */
      if (request_content_length != -1 && size < request_content_length + pret) {
        printf("request_content_length: %d but read so far: %d, retrying...\n", request_content_length, (int) size);
        break;
      }

      key = hash_buffer(buffer);

      HASH_FIND_INT(cache, &key, found_entry);
      if (found_entry && found_entry->timestamp > get_timestamp()) {
        serve_response_from_cache(found_entry, fds[i].fd, i);
      } else {
        /* Request not found in internal cache, requesting target */

        // TODO: Modify buffer to use "Connection: close" and custom "Host" header
        int req_sockfd = initialize_new_socket();
        ssize_t bytes_sent = 0, total_bytes_sent = 0;
        printf("New socket: %d (sending request to target)\n", req_sockfd);

        struct pollfd *req_fds = (struct pollfd *) calloc(1, sizeof(struct pollfd));
        req_fds[0].fd = req_sockfd;
        req_fds[0].events = POLLOUT;

        while (poll(req_fds, (nfds_t) 1, write_timeout)) {
          if (req_fds[0].revents & POLLOUT && status == 1) {
            printf("POLLOUT2\n");
            req_fds[0].revents = 0;

            if (total_bytes_sent < strlen(buffer)) {
              printf("Sending...\n");
              while ((bytes_sent = write(req_sockfd, buffer + total_bytes_sent, strlen(buffer - total_bytes_sent)))) {
                /* Writing should be continued later or end of transmission */
                if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                  printf("Sending would block.\n");
                  break;
                }

                total_bytes_sent += bytes_sent;

                printf("%d / %d bytes sent. (%d in this tick)\n", (int) total_bytes_sent, (int) strlen(buffer),
                       (int) bytes_sent);
              }

            } else {
              printf("Whole request sent! %s\n", buffer);

              write(req_sockfd, "Host: cs.put.poznan.pl", strlen("Host: cs.put.poznan.pl"));

              req_fds[0].events = POLLIN;
              free(buffer);
              buffer = malloc(BUFSIZE);
              size = 0;
              capacity = BUFSIZE;
              status = 2;
            }
          } if (req_fds[0].revents & POLLIN && status == 2) {
            printf("POLLIN2\n");
            req_fds[0].revents = 0;

            while ((rsize = read(req_fds[0].fd, buffer + size, capacity - size))) {
              /* Reading should be continued later or end of transmission */
              if (rsize == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                printf("would block\n");
                break;
              }

              /* Else (positive number of bytes) */
              size += rsize;

              /* Each dot informs about chunk of data, just for debugging purposes */
              printf("Receiving bytes from target: %d\n", (int) rsize);

              if (size == capacity) {
                printf("Reallocating buffer to capacity: %d\n", (int) (capacity * 2));
                capacity *= 2;
                buffer = realloc(buffer, (size_t) capacity);

                if (buffer == NULL) {
                  printf("Failed to rellocate the buffer!\n");
                  exit(EXIT_FAILURE);
                }
              }
            }

            int minor_version, status;
            char *msg;
            size_t msg_len;

            num_headers = sizeof(headers) / sizeof(headers[0]);
            pret = phr_parse_response(buffer, strlen(buffer), &minor_version, &status, &msg, &msg_len, headers, &num_headers, 0);
            if (pret == -2) break;
            if (pret == -1) {
              printf("Response parse error! %s\n", buffer);
            }



            printf("Response parsed, Received buffer: %s\n", buffer);
            close(req_fds[0].fd);
            status = 3;
            fds[0].events = POLLOUT;
            printf("Closing sock=%d, status: %d\n", req_fds[0].fd, status);
            break;

            /*
             * struct cache_entry *entry = (struct cache_entry *) malloc(sizeof(struct cache_entry));
              entry->key = hash_buffer(pair->buffer);
              entry->timestamp = get_timestamp() + pair->ttl;
              entry->buffer = malloc(bytes);
              entry->bytes = bytes;
              strcpy(entry->buffer, buffer);

              HASH_ADD_INT(cache, key, entry);
             */
          }
        }
      }
    } else if (fds[0].revents & POLLOUT) {
      printf("POLLOUT - %d, status=%d\n", conn_info->sck, status);
      if (status == 2) {
        printf("napierdalam\n");
        fds[0].revents = 0;
        ssize_t bytes_sent = 0, total_bytes_sent = 0;

        if (total_bytes_sent < strlen(buffer)) {
          printf("Sending...\n");
          while ((bytes_sent = write(fds[0].fd, buffer + total_bytes_sent, strlen(buffer - total_bytes_sent)))) {
            /* Writing should be continued later or end of transmission */
            if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
              printf("Sending would block.\n");
              break;
            }

            total_bytes_sent += bytes_sent;

            printf("%d / %d bytes sent. (%d in this tick)\n", (int) total_bytes_sent, (int) strlen(buffer),
                   (int) bytes_sent);

            if (total_bytes_sent == strlen(buffer)) {
              close(fds[0].fd);
              printf("End\n");
              status = 0;
              break;
            }
          }
        }
      }
    }
  }

  free(buffer);
  printf("poll outer end, status: %d\n", status);
}

void handle_socket(int newsockfd) {
  pthread_t thid;
  int rc;
  struct connection_info conn_info;
  conn_info.sck = newsockfd;

  rc = pthread_create(&thid, NULL, handle_tcp_connection, &conn_info);
  if (rc != 0) {
    printf("Thread created. Rc: %d, Pid: %d, Sock: %d\n", rc, (int) thid, newsockfd);
    rc = pthread_detach(thid);

    if (rc != 0) {
      printf("Thread detached. Rc: %d\n", rc);
    } else {
      printf("Failed to detach thread. Rc: %d, Errno: %d\n", rc, errno);
    }
  } else {
    printf("Failed to create thread. Rc: %d, Errno: %d\n", rc, errno);
  }
}

void run(int listen_sck_fd, configuration cfg) {
  int upperBound = 1;
  socklen_t clilen;

  struct sockaddr_in cli_addr;
  struct pollfd *fds = (struct pollfd *) calloc(cfg.fds_count, sizeof(struct pollfd));

  int request_fds[cfg.fds_count];
  for(int j = 0; j < cfg.fds_count; j++) request_fds[j] = 0;

  /* Initialize stack of free indexes in fds array */
  StackInit(&freeIndexesStack, cfg.fds_count);

  fds[0].fd = listen_sck_fd;
  fds[0].events = POLLIN;

  int i = 0;
  for (i = cfg.fds_count - 1; i > 0; i--)
    StackPush(&freeIndexesStack, (stackElementT) (i));

  /* Get sockaddr_in structure only once as it's unlikely to change */
  target_serv_addr = get_server_addr(cfg);
  if (target_serv_addr.sin_addr.s_addr == 0) {
    handle_error(1, errno, "Failed to create sockaddr_in structure");
    exit(EXIT_FAILURE);
  }

  while (poll(fds, (nfds_t) sck_cnt, -1)) {
    for (i = 0; i < upperBound; i++) {
      if (i == 0) {
        fds[i].revents = 0;

        int newsockfd = accept(listen_sck_fd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd > 0) {
          handle_socket(newsockfd);
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  char *config_name;

  if (argc >= 2) {
    config_name = malloc(sizeof(char) * strlen(argv[1]));
    strcpy(config_name, argv[1]);
  } else {
    config_name = "config.ini";
  }

  if (ini_parse(config_name, config_handler, &cfg) < 0) {
    handle_error(1, EACCES, "Can't load config");
    return 1;
  }

  printf("Cachr started with config from '%s': host=%s, port=%s...\n",
         config_name, cfg.listen_host, cfg.listen_port);

  int listen_sck = prepare_in_sock(cfg);
  if (listen_sck < 0) {
    handle_error(1, errno, "listen_sck");
  }

  run(listen_sck, cfg);
  return 0;
}
