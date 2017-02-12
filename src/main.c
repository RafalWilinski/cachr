#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

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

struct buffer_data {
  u_int64_t key;
  int bytes_read;
  int allocated_mem;
  char* buffer;
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

void find_pair_and_save_to_cache(int fd, char* buffer) {
  struct socket_request_pair *pair = NULL;
  HASH_FIND_INT(pairs, &fd, pair);

  if (pair) {
    u_int32_t bytes = sizeof(char) * strlen(buffer);

    if (pair->ttl > 0) {
      printf("Adding cache entry \n");
      struct cache_entry *entry = (struct cache_entry *) malloc(sizeof(struct cache_entry));
      entry->key = hash_buffer(pair->buffer);
      entry->timestamp = get_timestamp() + pair->ttl;
      entry->buffer = malloc(bytes);
      entry->bytes = bytes;
      strcpy(entry->buffer, buffer);

      HASH_ADD_INT(cache, key, entry);
    }

    StackPush(&freeIndexesStack, (stackElementT) pair->idx);
  } else {
    printf("socket-request pair not found for key = %d!\n", fd);
  }
}

void run(int listen_sck_fd, configuration cfg) {
  int upperBound = 1;
  u_int64_t key;
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
          if (!StackIsEmpty(&freeIndexesStack)) {
            int idx = StackPop(&freeIndexesStack);
            int blocking_status = make_socket_non_blocking(newsockfd);
            if (blocking_status == -1) {
              break;
            }

            fds[idx].fd = newsockfd;
            fds[idx].events = POLLIN;
            if (idx >= upperBound) upperBound = idx + 1;
            sck_cnt = upperBound;

            printf("[New connection] FD: %d, idx: %d\n", newsockfd, idx);
          } else {
            /* We reached the maximum amount of concurrent connections, ignore this connection */
            printf("Stack empty!\n");
            close(newsockfd);
          }
        }
      } else {
        if (fds[i].revents & POLLHUP) {
          printf("Fd: %d was disconnected.\n", fds[i].fd);
          StackPush(&freeIndexesStack, i);
        } else if (fds[i].revents & POLLNVAL) {
          printf("Fd: %d, id: %d invalid.\n", fds[i].fd, i);
        } else if (fds[i].revents & POLLERR) {
          printf("Fd: %d is broken.\n", fds[i].fd);
          StackPush(&freeIndexesStack, i);
        } else if (fds[i].revents & POLLIN) {

          char *buffer = malloc(BUFSIZE);
          buffer = memset(buffer, 0, BUFSIZE);
          size_t size = 0;
          ssize_t rsize = 0, capacity = BUFSIZE;

          while ((rsize = read(fds[i].fd, buffer + size, capacity - size))) {
            /* Reading should be continued later */
            if (rsize == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
              printf("would block\n");
              break;
            }

            /* Finished reading */
            if (rsize == 0) {
              break;
            }

            /* Else (positive number of bytes) */
            size += rsize;

            /* Each dot informs about chunk of data, just for debugging purposes */
            printf("Rsize: %d\n", rsize);

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

          /* If zero or greater than zero bytes read = EOF */
          /* Otherwise socket returned -1 because it was not ready or error occured */
          if (rsize >= 0) {
            fds[i].revents = 0;
          } else {
            break;
          }

          printf("\nidx: %d, fd: %d, bytes read: %d\n", i, fds[i].fd, (int) size);

          /* If entry is present in request_fds array then it's response from target.
           * Otherwise it's request directly to cachr */
          if (request_fds[fds[i].fd] > 0) {
            /* Find requester responsible for that request */
            int requester_fds = request_fds[fds[i].fd];
            int data_source_fds = fds[i].fd;

            /* Put response from target to cache */
            find_pair_and_save_to_cache(fds[i].fd, buffer);

            /* Write non-cached response from target to requester */
            int ws = (int) write(requester_fds, buffer, strlen(buffer));
            if (ws < -1) {
              perror("Failed to send response to requester\n");
            }

            printf("Non-cached response. %d bytes sent.\n", ws);

            /* Mark index "data_source_fds" as free */
            request_fds[data_source_fds] = 0;

            /* Close sockets, release index */
            close(data_source_fds);
            close(requester_fds);
            StackPush(&freeIndexesStack, i);
            printf("Closed socket %d & %d\n", data_source_fds, requester_fds);
          } else {
            struct cache_entry *found_entry = NULL;
            int ttl = cfg.ttl;
            char *method, *path;
            size_t method_len, path_len, num_headers;
            int minor_version, pret;
            struct phr_header headers[100];

            pret = phr_parse_request(buffer, size, &method, &method_len, &path, &path_len,
                                     &minor_version, headers, &num_headers, 0);
            /* Parsed successfully */
            if (pret > 0) {
              /* Find header called "TTL" and parse it's value to integer */
              for (i = 0; i != num_headers; ++i) {
                if (headers[i].name == "Cache-Control") {
                  if (headers[i].value == "no-cache" || headers[i].value == "no-store") ttl = 0;
                  else {
                    /* Parse "max-age=X" */
                    int ttl = get_ttl_value(headers[i].value);
                  }

                  break;
                }
              }
            }

            key = hash_buffer(buffer);

            HASH_FIND_INT(cache, &key, found_entry);
            if (found_entry && found_entry->timestamp > get_timestamp()) {
              serve_response_from_cache(found_entry, fds[i].fd, i);
            } else {
              /* Cached response was found but it was too old */
              if (found_entry) {
                printf("Cache entry expired! %d < %d\n", (int) found_entry->timestamp,
                       (int) get_timestamp());
                HASH_DEL(cache, found_entry);
              }

              /* Perform request to the target */
              /* Find suitable index in fds array */
              int idx = StackPop(&freeIndexesStack);

              /* Create new socket */
              int sockfd = initialize_new_socket();
              printf("New socket: %d (requesting data from target)\n", sockfd);

              /* Write request payload to that socket */
              int ws = (int) write(sockfd, buffer, strlen(buffer));

              /* Close connection immediately (Connection: close instead of keep-alive) */
              write(sockfd, CONNECTION_CLOSE_MSG, strlen(CONNECTION_CLOSE_MSG));
              if (ws < -1) {
                perror("Failed to send payload to target\n");
              }

              /* Put that socket to fds array so we could "poll" it */
              fds[idx].fd = sockfd;
              fds[idx].events = POLLIN;
              fds[idx].revents = 1;

              /* Add request to array of pending requests */
              request_fds[sockfd] = fds[i].fd;

              /* Add request to dictionary of pending requests */
              printf("Adding pending request, buffer: \n%s\n", buffer);
              struct socket_request_pair* entry =
                  (struct socket_request_pair*) malloc(sizeof(struct socket_request_pair));
              entry->key = (u_int64_t) sockfd;
              entry->buffer = malloc(sizeof(char) * strlen(buffer));
              entry->idx = idx;
              entry->ttl = ttl;
              strcpy(entry->buffer, buffer);
              HASH_ADD_INT(pairs, key, entry);

              /* If number of simultaneous requests is bigger than before we have to iterate to bigger index */
              if (idx >= upperBound)
                upperBound = idx + 1;

              sck_cnt = upperBound;
            }
          }

          free(buffer);
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
