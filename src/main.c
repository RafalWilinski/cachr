#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "libs/ini.h"
#include "error.h"
#include "libs/stack.h"
#include "libs/uthash.h"
#include "libs/picohttpparser.h"
#include "configutils.h"
#include "netutils.h"

/* Size of buffer/chunk read */
#define BUFSIZE 4096

/* Constant message send with every tcp request */
static const char CONNECTION_CLOSE_MSG[] = "Connection: close";

/* Cached sockaddr_in structure as we're calling the same target */
struct sockaddr_in target_serv_addr;

/* Head of our structure */
struct cache_entry *cache = NULL;

/* Number of sockets connected in fds array */
int sck_cnt = 10;

configuration cfg;

struct cache_entry {
  u_int64_t key;
  char* buffer;
  long timestamp;
  u_int32_t bytes;
  struct UT_hash_handle hh;
};

struct request_proxy_request_pair {
  int original_request_fd;
  int proxied_request_fd;
};

long get_timestamp() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
  return ms;
}

u_int64_t hash_buffer(char* str) {
  u_int64_t c, hash = 2317;
  while ((c = (u_int64_t) *str++)) {
    hash = ((hash << 5) + hash) + c;
  }

  return hash;
}

void run(int listen_sck_fd, configuration cfg) {
  int upperBound = 1;
  u_int64_t key;
  char buffer[BUFSIZE];
  socklen_t clilen;
  stackT freeIndexesStack;
  struct sockaddr_in cli_addr;
  struct pollfd *fds = (struct pollfd *) calloc(cfg.fds_count, sizeof(struct pollfd));

  char *method, *path;
  int pret, minor_version;
  struct phr_header headers[100];
  size_t buflen = 0, prevbuflen = 0, method_len, path_len, num_headers;
  ssize_t rret;

  StackInit(&freeIndexesStack, cfg.fds_count);

  fds[0].fd = listen_sck_fd;
  fds[0].events = POLLIN;

  int i = 0;
  for (i = cfg.fds_count - 1; i > 0; i--) {
    StackPush(&freeIndexesStack, (stackElementT) (i));
  }

  printf("Server started. Maximum concurrent connections: %d\n", cfg.fds_count);

  target_serv_addr = get_server_addr(cfg);
  if (target_serv_addr.sin_addr.s_addr == NULL) {
    handle_error(1, errno, "Failed to create sockaddr_in structure");
    exit(EXIT_FAILURE);
  }

  while (poll(fds, (nfds_t) sck_cnt, -1)) {
    for (i = 0; i < upperBound; i++) {
      /* Server listening socket */
      printf("%d/%d, fd: %d\n", i, upperBound, fds[i].fd);
      if (i == 0) {
        fds[i].revents = 0;

        int newsockfd = accept(listen_sck_fd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd > 0) {
          if (!StackIsEmpty(&freeIndexesStack)) {
            int idx = StackPop(&freeIndexesStack);

            fds[idx].fd = newsockfd;
            make_socket_non_blocking(newsockfd);
            fds[idx].events = POLLIN;

            if (idx >= upperBound) {
              upperBound = idx + 1;

              printf("New upper bound: %d\n", upperBound);
            }

            printf("New connection on file descriptor #%d, idx: %d\n", fds[idx].fd, idx);
          } else {
            printf("Stack empty!");
          }
        } else {
          printf("Negative newsockfd! %d\n", newsockfd);
        }
      } else {
        if (fds[i].revents > 0) {
          printf("reading: %d\n", i);
          fds[i].revents = 0;
          ssize_t bufsize = read(fds[i].fd, buffer, BUFSIZE);

          if (bufsize < 0) {
            printf("%d %s\n", errno, strerror(errno));
            handle_error(1, errno, "read");
          }

          printf("Buffer: %s\n", buffer);

          struct cache_entry *found_entry = NULL;
          key = hash_buffer(buffer);

          printf("Finding key: %llu\n", key);
          HASH_FIND_INT(cache, &key, found_entry);

          if (found_entry) {
            printf("Serving response from cache\n");

            ssize_t sent_bytes = write(fds[i].fd, found_entry->buffer, found_entry->bytes);
            if (sent_bytes > 0) {
              printf("%d bytes sent to requester\n", (int) sent_bytes);
            } else {
              printf("Failed to respond to requester from cache... %d\n", (int) sent_bytes);
            }

            close(fds[i].fd);
            StackPush(&freeIndexesStack, (stackElementT) i);
          } else {
            printf("Not found in cache! Making request to target\n");

            int idx = StackPop(&freeIndexesStack);

            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) {
              handle_error(sockfd, errno, "ERROR opening socket");
            }

            printf("Connecting to %s:%d\n", cfg.target_host, cfg.target_port);

            if (connect(sockfd, (struct sockaddr *) &target_serv_addr, sizeof(target_serv_addr)) < 0) {
              handle_error(1, errno, "Error while connecting");
            }

            int n = (int) write(sockfd, buffer, strlen(buffer));
            printf("Written to sock: %d\n", n);

            fds[idx].fd = sockfd;
            make_socket_non_blocking(sockfd);
            fds[idx].events = POLLIN;
            fds[idx].revents = 1;
            sck_cnt = upperBound;

            printf("fd: %d, idx: %d\n", fds[idx].fd, idx);

            if (idx >= upperBound) {
              upperBound = idx + 1;

              printf("New upper bound: %d\n", upperBound);
            }
          }
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  char *config_name;

  if (argc >= 2) {
    config_name = strdup(argv[1]);
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
