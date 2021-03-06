#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "libs/ini.h"
#include "error.h"
#include "libs/uthash.h"
#include "configutils.h"
#include "netutils.h"
#include "utils.h"
#include "libs/picohttpparser.h"

/* Size of buffer/chunk read */
#define BUFSIZE 4096

/* Cached sockaddr_in structure as we're calling the same target */
struct sockaddr_in target_serv_addr;

/* Head of cache data structure */
struct cache_entry *cache = NULL;

/* Parsed configuration structure */
configuration cfg;

/* Cache mutex */
pthread_mutex_t cache_mutex;

struct cache_entry {
  uint64_t key;
  char* buffer;
  long timestamp;
  u_int32_t bytes;
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
  char *key = strtok(header_value, separator);
  char *value = strtok(NULL, "");
  if (strcmp(key, "max-age") == 0) {
    return atoi(value);
  }
  return 0;
};

void serve_response_from_cache(struct cache_entry *found_entry, int fd) {
  printf("[%d] Serving response from cache (%d bytes) to fd: %d\n", (int) gettid(), found_entry->bytes, fd);

  struct pollfd *fds = (struct pollfd *) calloc(1, sizeof(struct pollfd));
  int tid = (int) gettid();

  fds[0].fd = fd;
  fds[0].events = POLLOUT;

  while (poll(fds, (nfds_t) 1, -1)) {
    if (fds[0].revents & POLLOUT) {
      fds[0].revents = 0;
      ssize_t bytes_sent = 0, total_bytes_sent = 0;

      if (total_bytes_sent < found_entry->bytes) {
        printf("[%d] Sending cached response...\n", tid);
        while ((bytes_sent = write(fds[0].fd, found_entry->buffer + total_bytes_sent,
                                   (size_t) (found_entry->bytes - total_bytes_sent)))) {
          /* Writing should be continued later */
          if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            printf("[%d] Sending would block.\n", tid);
            break;
          }

          total_bytes_sent += bytes_sent;

          printf("[%d] %d / %d bytes sent. (%d in this tick)\n", tid, (int) total_bytes_sent, found_entry->bytes,
                 (int) bytes_sent);

          if (total_bytes_sent == found_entry->bytes) {
            close(fds[0].fd);

            printf("[%d] Cached response sent.\n", tid);
            pthread_exit(NULL);
          }
        }
      }
    }
  }
}

char *rewrite_request(char *response_buffer, struct phr_header *headers, int headers_size, int headers_count,
                      int total_size, char* method, size_t method_len, char *path, size_t path_len, int minor_version) {
  size_t bufsize = method_len + path_len + 12, header_size = 0;
  char *buffer = malloc(sizeof(char) * bufsize);

  printf("Initial buffer: \n%s\n", response_buffer);

  sprintf(buffer, "%.*s %.*s HTTP/1.%d\r\n\0", (int) method_len, method, (int) path_len, path, minor_version);

  /* Rewrite headers */
  for (int i = 0; i < headers_count; i++) {
    char *name = malloc(sizeof(char) * headers[i].name_len);
    char *value = malloc(sizeof(char) * headers[i].value_len);
    sprintf(name, "%.*s\0", (int) headers[i].name_len, headers[i].name);
    sprintf(value, "%.*s\0", (int) headers[i].value_len, headers[i].value);

    header_size = headers[i].name_len + 4;

    if (strcmp("Host", name) == 0) {
      printf("[%d] Writing custom host...\n", (int) gettid());
      free(value);
      value = malloc(sizeof(char) * (strlen(cfg.target_host) + 2));
      strcpy(value, cfg.target_host);
      value[strlen(value)] = '\0';

      header_size += strlen(cfg.target_host);
    } else {
      header_size += (int) headers[i].value_len;
    }

    buffer = realloc(buffer, sizeof(char) * (bufsize + header_size));
    sprintf(buffer + bufsize, "%s: %s\r\n\0", name, value);
    bufsize += header_size;

    free(name);
    free(value);
  }

  /* Rewrite rest of request */
  buffer = realloc(buffer, (size_t) (bufsize + total_size - headers_size + 2));
  memcpy(buffer + bufsize, response_buffer + headers_size, total_size - headers_size);
  buffer[strlen(buffer)] = '\n';

  return buffer;
}

/*
 * Statuses:
 * -1: Receiving data from requester
 *  1: Sending data to target
 *  2: Receiving data from target
 *  3: Sending back data to requester
 */
void* handle_tcp_connection(void *ctx) {
  int tid = (int) gettid(), read_timeout = -1, write_timeout = -1, status = -1, request_content_length = -1,
      response_content_length = -1, ttl = cfg.ttl, i, req_parsed = 0, res_parsed = 0, res_minor_version, res_status,
      minor_version, pret = -2;
  int sck = (int) ctx, chunked = 0;
  uint64_t key;
  size_t method_len, path_len, num_headers;
  char *buffer = malloc(BUFSIZE), *req_method, *req_path;
  struct cache_entry *found_entry = NULL;
  struct phr_header headers[100];
  struct phr_header res_headers[100];
  struct pollfd *fds = (struct pollfd *) calloc(1, sizeof(struct pollfd));

  buffer = memset(buffer, 0, BUFSIZE);

  num_headers = sizeof(headers) / sizeof(headers[0]);

  fds[0].fd = (int) ctx;
  fds[0].events = POLLIN;

  printf("[%d] Polling... FD: %d\n", (int) gettid(), sck);

  while (poll(fds, (nfds_t) 1, read_timeout) && (status != 0)) {
    if (fds[0].revents & POLLHUP) {
      printf("[%d] Fd: %d was disconnected.\n", tid, sck);
      pthread_exit(NULL);
    } else if (fds[0].revents & POLLNVAL) {
      printf("[%d] Fd: %d is invalid.\n", tid, sck);
    } else if (fds[0].revents & POLLERR) {
      printf("[%d] Fd: %d is broken.\n", tid, sck);
    } else if (fds[0].revents & POLLIN && status == -1) {
      /* Handle Incoming Request to proxy */
      printf("[%d] POLLIN OUTER - %d\n", tid, sck);
      fds[0].revents = 0;
      size_t size = 0;
      ssize_t rsize = 0, capacity = BUFSIZE;

      while ((rsize = read(fds[0].fd, buffer + size, capacity - size))) {
        if (rsize == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          printf("[%d] Would block\n", tid);
          break;
        }

        /* Else (positive number of bytes) */
        size += rsize;

        /* Each dot informs about chunk of data, just for debugging purposes */
        printf("[%d] Receiving bytes: %d\n", tid, (int) rsize);

        if (size == capacity) {
          printf("[%d] Reallocating buffer to capacity: %d\n", tid, (int) (capacity * 2));
          capacity *= 2;
          buffer = realloc(buffer, (size_t) capacity);

          if (buffer == NULL) {
            printf("[%d] Failed to rellocate the buffer!\n", tid);
            exit(EXIT_FAILURE);
          }
        }

        /* Parse request headers only once */
        if (req_parsed != 1) {
          pret = phr_parse_request(buffer, size, &req_method, &method_len, &req_path, &path_len,
                                   &minor_version, headers, &num_headers, 0);

          /* Request is incomplete */
          if (pret == -2) {
            printf("[%d] Request incomplete, continue.\n", tid);
            continue;
          }

          /* Parse Error */
          if (pret == -1) {
            printf("[%d] Parse Error! rsize=%d, buffer:\n%s\n", tid, (int) rsize, buffer);
            continue;
          }

          /* Else pret = number of bytes consumed */

          status = 1;
          printf("[%d] Request parsed!\n", tid);

          for (i = 0; i != num_headers; ++i) {
            char *name = malloc(sizeof(char) * headers[i].name_len);
            char *value = malloc(sizeof(char) * headers[i].value_len);
            sprintf(name, "%.*s", (int) headers[i].name_len, headers[i].name);
            sprintf(value, "%.*s", (int) headers[i].value_len, headers[i].value);

            if(strcmp(name, "Cache-Control") == 0) {
              if (strcmp(value, "no-cache") == 0 || strcmp(value, "no-store") == 0) ttl = 0;
              else {
                ttl = get_ttl_value(value);
              }

              break;
            } else if (strcmp("Content-Length", name) == 0) {
              request_content_length = atoi(value);
            } else if (strcmp("Pragma", name) == 0) {
              if (strcmp("no-cache", value) == 0) ttl = 0;
            }

            free(name);
            free(value);
          }
          req_parsed = 1;
        }

        /* Content-Length header was present and it's value is bigger than downloaded bytes */
        if (request_content_length != -1) {
          if (size < request_content_length + pret) {
            printf("[%d] request_content_length: %d but read so far: %d, retrying...\n", tid,
                   request_content_length, (int) size);
            continue;
          } else {
            printf("[%d] Whole request captured, break\n", tid);
            break;
          }
        }
      }

      key = hash_buffer(buffer);

      /* Avoid concurrent access */
      pthread_mutex_lock(&cache_mutex);
      HASH_FIND_INT(cache, &key, found_entry);
      pthread_mutex_unlock(&cache_mutex);

      if (found_entry && found_entry->timestamp > get_timestamp()) {
        serve_response_from_cache(found_entry, fds[0].fd);
      } else {
        if (found_entry) {
          printf("[%d] Entry found but was too old. %d vs %d\n", tid, (int) found_entry->timestamp,
                 (int) get_timestamp());
        }
        /* Request not found in internal cache, requesting target */
        char * request_buffer = rewrite_request(buffer, headers, pret, (int) num_headers, (int) size, req_method,
                                                method_len, req_path, path_len, minor_version);

//        printf("Request buffer: %s\n", request_buffer);

        int req_sockfd = initialize_new_socket();
        ssize_t bytes_sent = 0, total_bytes_sent = 0;
        printf("[%d] New socket: %d (sending request to target)\n", tid, req_sockfd);
        size = 0;

        struct pollfd *req_fds = (struct pollfd *) calloc(1, sizeof(struct pollfd));
        req_fds[0].fd = req_sockfd;
        req_fds[0].events = POLLOUT;

        while (poll(req_fds, (nfds_t) 1, write_timeout)) {
          if (fds[0].revents & POLLHUP) {
            printf("[%d] Fd: %d was disconnected.\n", tid, req_fds[0].fd);
            sleep(1);
          } else if (req_fds[0].revents & POLLNVAL) {
            printf("[%d] Fd: %d is invalid.\n", tid, req_fds[0].fd);
          } else if (req_fds[0].revents & POLLERR) {
            printf("[%d] Fd: %d is broken.\n", tid, req_fds[0].fd);
          } else if (req_fds[0].revents & POLLOUT && status == 1) {
            printf("[%d] POLLOUT INNER\n", tid);
            req_fds[0].revents = 0;

            if (total_bytes_sent < strlen(request_buffer)) {
              printf("[%d] Sending...\n", tid);
              while ((bytes_sent = write(req_sockfd, request_buffer + total_bytes_sent,
                                         strlen(request_buffer) - total_bytes_sent))) {
                /* Writing should be continued later or end of transmission */
                if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                  printf("[%d] Sending would block.\n", tid);
                  break;
                }

                total_bytes_sent += bytes_sent;

                printf("[%d] %d / %d bytes sent. (%d in this tick)\n", tid, (int) total_bytes_sent,
                       (int) strlen(request_buffer), (int) bytes_sent);
              }

            } else {
              printf("[%d] Whole request sent.\n", tid);
              req_fds[0].events = POLLIN;
              free(request_buffer);
              buffer = malloc(BUFSIZE);
              memset(buffer, 0, BUFSIZE);
              size = 0;
              capacity = BUFSIZE;
              status = 2;
            }
          } if (req_fds[0].revents & POLLIN && status == 2) {
            char *msg;
            size_t msg_len;

            printf("[%d] POLLIN INNER rsize: %d, size: %d, capacity: %d, buffer: %s\n", tid, (int) rsize, (int) size,
                   (int) capacity, buffer);
            req_fds[0].revents = 0;

            while ((rsize = read(req_fds[0].fd, buffer + size, capacity - size))) {
              /* Reading should be continued later or end of transmission */
              if (rsize == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                printf("[%d] would block\n", tid);
                break;
              }

              /* Else (positive number of bytes) */
              size += rsize;

              printf("[%d] Receiving bytes from target: %d, errno: %d\n", tid, (int) rsize, errno);

              if (res_parsed != 1) {
                num_headers = sizeof(res_headers) / sizeof(res_headers[0]);
                pret = phr_parse_response(buffer, strlen(buffer), &res_minor_version, &res_status, &msg, &msg_len,
                                          res_headers,
                                          &num_headers, 0);

                if (pret == -2) {
                  /* Keep on receiving, request incomplete */
                  continue;
                } else if (pret == -1) {
                  printf("Response parse error! pret=1%s\n", buffer);
                  break;
                }

                printf("[%d] Response parsed! Headers: %d\n", tid, (int) num_headers);

                for (i = 0; i != num_headers; ++i) {
                  char *name = malloc(sizeof(char) * res_headers[i].name_len);
                  char *value = malloc(sizeof(char) * res_headers[i].value_len);
                  sprintf(name, "%.*s", (int) res_headers[i].name_len, res_headers[i].name);
                  sprintf(value, "%.*s", (int) res_headers[i].value_len, res_headers[i].value);

                  printf("[%d][%d] %s: %s\n", tid, i, name, value);

                  if (strcmp("Cache-Control", name) == 0) {
                    /* Override requests caching strategy by response caching strategy */
                    if (strcmp(value, "no-cache") == 0 || strcmp(value, "no-store") == 0) ttl = 0;
                    else {
                      /* Parse "max-age=X" */
                      ttl = get_ttl_value((char *) value);
                    }
                  } else if (strcmp("Content-Length", name) == 0) {
                    response_content_length = atoi(value);
                  } else if (strcmp("Transfer-Encoding", name) == 0) {
                    if (strcmp("chunked", value) == 0) {
                      printf("Detected chunked response...\n");
                      chunked = 1;
                    }
                  }

                  free(name);
                  free(value);
                }

                res_parsed = 1;
              }

              if (size == capacity) {
                printf("[%d] Reallocating buffer to capacity: %d\n", tid, (int) (capacity * 2));
                capacity *= 2;
                buffer = realloc(buffer, (size_t) capacity);

                if (buffer == NULL) {
                  printf("[%d] Failed to reallocate the buffer!\n", tid);
                  exit(EXIT_FAILURE);
                }
              }

              /* Content-Length header was present and it's value is bigger than downloaded bytes */
              if (response_content_length != -1) {
                if (size < response_content_length + pret) {
                  printf("[%d] response_content_length: %d but read so far: %d, retrying...\n", tid,
                         response_content_length, (int) size);
                  continue;
                } else {
                  printf("[%d] Whole request downloaded\n", tid);
                  break;
                }
              }

              if (chunked == 1 && buffer[strlen(buffer) - 1] == 10 && buffer[strlen(buffer) - 2] == 13) {
                printf("[%d] End of chunked response\n", tid);
                break;
              } else continue;
            }


            /* Save to cache only if TTL is greater than zero */
            if (ttl > 0) {
              struct cache_entry *entry = (struct cache_entry *) malloc(sizeof(struct cache_entry));
              entry->key = key;
              entry->timestamp = get_timestamp() + ttl;
              entry->buffer = malloc(size);
              entry->bytes = (u_int32_t) size;
              strcpy(entry->buffer, buffer);

              /* Avoid concurrent writes */
              pthread_mutex_lock(&cache_mutex);
              HASH_ADD_INT(cache, key, entry);
              pthread_mutex_unlock(&cache_mutex);
            }

            close(req_fds[0].fd);
            status = 3;
            fds[0].events = POLLOUT;
            printf("[%d] Closing sock=%d, status: %d\n", tid, req_fds[0].fd, status);
            break;
          }
        }
      }
    } else if (fds[0].revents & POLLOUT) {
      printf("[%d] POLLOUT OUTER - %d, status=%d\n", tid, sck, status);
      if (status == 3) {
        fds[0].revents = 0;
        ssize_t bytes_sent = 0, total_bytes_sent = 0;

        if (total_bytes_sent < strlen(buffer)) {
          printf("[%d] Sending...\n", tid);
          while ((bytes_sent = write(fds[0].fd, buffer + total_bytes_sent, strlen(buffer) - total_bytes_sent))) {
            /* Writing should be continued later */
            if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
              printf("[%d] Sending would block.\n", tid);
              break;
            }

            total_bytes_sent += bytes_sent;

            printf("[%d] %d / %d bytes sent. (%d in this tick)\n", tid, (int) total_bytes_sent, (int) strlen(buffer),
                   (int) bytes_sent);

            if (total_bytes_sent == strlen(buffer)) {
              close(fds[0].fd);
              printf("[%d] End\n", tid);
              status = 0;
              break;
            }
          }
        } else {
          printf("[%d] Whole response sent!\n", tid);

          close(fds[0].fd);
          free(buffer);
          free(fds);
          pthread_exit(NULL);
        }
      } else {
        printf("[%d] Incorrect status! %d\n", tid, status);
      }
    }
  }

  free(buffer);
  free(fds);

  printf("[%d] poll outer end, status: %d\n", tid, status);
  pthread_exit(NULL);
}

void handle_socket(int newsockfd) {
  pthread_t thid;
  int rc;

  rc = pthread_create(&thid, NULL, handle_tcp_connection, (void *) newsockfd);
  if (rc == 0) {
    printf("[%d] Thread created. Sock: %d\n", (int) thid, newsockfd);
    rc = pthread_detach(thid);

    if (rc == 0) {
      printf("[%d] Thread detached.\n", (int) thid);
    } else {
      printf("Failed to detach thread. Rc: %d, Errno: %d\n", rc, errno);
    }
  } else {
    printf("Failed to create thread. Rc: %d, Errno: %d\n", rc, errno);
  }
}

void run(int listen_sck_fd, configuration cfg) {
  socklen_t clilen;

  struct sockaddr_in cli_addr;
  struct pollfd *fds = (struct pollfd *) calloc(cfg.fds_count, sizeof(struct pollfd));

  int request_fds[cfg.fds_count];
  for(int j = 0; j < cfg.fds_count; j++) request_fds[j] = 0;

  fds[0].fd = listen_sck_fd;
  fds[0].events = POLLIN;

  /* Get sockaddr_in structure only once as it's unlikely to change */
  target_serv_addr = get_server_addr(cfg);
  if (target_serv_addr.sin_addr.s_addr == 0) {
    handle_error(1, errno, "Failed to create sockaddr_in structure");
    exit(EXIT_FAILURE);
  }

  while (poll(fds, (nfds_t) 1, -1)) {
    fds[0].revents = 0;

    int newsockfd = accept(listen_sck_fd, (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd > 0) {
      handle_socket(newsockfd);
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

  free(cache);
  return 0;
}
