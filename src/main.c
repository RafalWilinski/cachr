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
int sck_cnt = 100;

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

void serve_response_from_cache(struct cache_entry *found_entry, int fd, int i) {
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

    printf("Adding cache entry... \n");
    struct cache_entry* entry = (struct cache_entry*) malloc(sizeof(struct cache_entry));
    entry->key = hash_buffer(pair->buffer);
    entry->timestamp = get_timestamp();
    entry->buffer = malloc(bytes);
    entry->bytes = bytes;
    strcpy(entry->buffer, buffer);

    HASH_ADD_INT(cache, key, entry);
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
  if (target_serv_addr.sin_addr.s_addr == NULL) {
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
            fds[idx].fd = newsockfd;
            fds[idx].events = POLLIN | POLLPRI;
            make_socket_non_blocking(newsockfd);

            if (idx >= upperBound) upperBound = idx + 1;
            printf("[New connection] FD: %d, idx: %d\n", newsockfd, idx);
          } else {
            printf("Stack empty!\n");
          }
        } else {
          printf("Negative newsockfd! %d\n", newsockfd);
        }
      } else {
        if (fds[i].revents == POLLIN) {
          char *buffer = malloc(BUFSIZE);
          buffer = memset(buffer, 0, BUFSIZE);
          size_t size = 0;
          ssize_t rsize = 0, capacity = BUFSIZE;

          while ((rsize = read(fds[i].fd, buffer + size, capacity - size)) != -1 && rsize != 0) {
            if (rsize == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
              return;
            }

            if (rsize < 0) {
              break;
            }

            size += rsize;
            printf("Reading: %d\n", (int) rsize);

            if (size == capacity) {
              printf("Reallocating buffer to capacity=%d\n", (int) (capacity * 2));
              capacity *= 2;
              buffer = realloc(buffer, (size_t) capacity);

              if (buffer == NULL) {
                printf("Failed to rellocate the buffer!\n");
              }
            }
          }

          /* Because something was read we can mark job as done */
          /* Otherwise socket returned -1 because it was not ready or error occured */
          if (size > 0) {
            fds[i].revents = 0;
          } else {
            break;
          }

          printf("Bytes read: %d, %d\n", (int) size, (int) rsize);

          /* If entry is present in request_fds array then it's response from target
           * Otherwise it's request directly to cachr */
          if (request_fds[fds[i].fd] > 0) {
            /* Find requester responsible for that request */
            int requester_fds = request_fds[fds[i].fd];
            int data_source_fds = fds[i].fd;

            /* Put response from target to cache */
            find_pair_and_save_to_cache(fds[i].fd, buffer);

            //TODO: Handle status
            /* Write non-cached response from target to requester */
            int ws = (int) write(requester_fds, buffer, strlen(buffer));

            printf("Non-cached response. %d bytes sent.\n", ws);

            /* Mark index "data_source_fds" as free */
            request_fds[data_source_fds] = 0;

            /* Close sockets, release idex */
            close(data_source_fds);
            close(requester_fds);
            StackPush(&freeIndexesStack, i);
          } else {
            struct cache_entry *found_entry = NULL;
            key = hash_buffer(buffer);

            HASH_FIND_INT(cache, &key, found_entry);
            if (found_entry && found_entry->timestamp + cfg.ttl > get_timestamp()) {
              serve_response_from_cache(found_entry, fds[i].fd, i);
            } else {
              /* Cached response was found but it was too old */
              if (found_entry) {
                printf("Cache entry expired! %d < %d\n", (int) (found_entry->timestamp + cfg.ttl),
                       (int) get_timestamp());
                HASH_DEL(cache, found_entry);
              }

              /* Perform request to the target */
              /* Find suitable index in fds array */
              int idx = StackPop(&freeIndexesStack);
              /* Create new socket */
              int sockfd = initialize_new_socket();

              //TODO: Handle status
              /* Write request payload to that socket */
              int ws = (int) write(sockfd, buffer, strlen(buffer));

              /* Close connection immediately (Connection: close instead of keep-alive) */
              write(sockfd, CONNECTION_CLOSE_MSG, strlen(CONNECTION_CLOSE_MSG));

              /* Put that socket to fds array so we could "poll" it */
              fds[idx].fd = sockfd;
              fds[idx].events = POLLIN;
              fds[idx].revents = 1;
              sck_cnt = upperBound;

              /* Add request to array of pending requests */
              request_fds[sockfd] = fds[i].fd;

              /* Add request to dictionary of pending requests */
              printf("Adding entry, buffer: \n%s\n", buffer);
              struct socket_request_pair* entry =
                  (struct socket_request_pair*) malloc(sizeof(struct socket_request_pair));
              entry->key = (u_int64_t) sockfd;
              entry->buffer = malloc(sizeof(char) * strlen(buffer));
              strcpy(entry->buffer, buffer);
              HASH_ADD_INT(pairs, key, entry);

              /* If number of simultaneous requests is bigger than before we have to iterate to bigger index */
              if (idx >= upperBound)
                upperBound = idx + 1;
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

  if (argc >= 2)
    config_name = strdup(argv[1]);
  else
    config_name = "config.ini";

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
