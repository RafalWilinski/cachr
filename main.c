#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "src/dict.h"
#include "src/ini.h"
#include "src/error.h"
#include "src/stack.h"

#define BUFSIZE 1024
char buffer[BUFSIZE];
dict* cache;
struct sockaddr_in target_serv_addr;

/* Number of sockets connected in fds array */
int sck_cnt = 1;

typedef struct {
  const char *target_host;
  unsigned short target_port;

  const char *listen_host;
  const char *listen_port;

  unsigned short fds_count;
  unsigned short non_blocking;
} configuration;

configuration cfg;

typedef struct {
  char* data;
  short status;
} target_response;

static int config_handler(void *user, const char *section, const char *name, const char *value) {
  configuration *pconfig = (configuration *) user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
  if (MATCH("target", "port")) {
    pconfig->target_port = (unsigned short) atoi(value);
  } else if (MATCH("target", "host")) {
    pconfig->target_host = strdup(value);
  } else if (MATCH("listen", "port")) {
    pconfig->listen_port = strdup(value);
  } else if (MATCH("listen", "host")) {
    pconfig->listen_host = strdup(value);
  } else if (MATCH("poll", "fds_count")) {
    pconfig->fds_count = (unsigned short) atoi(value);
  } else if (MATCH("socket", "non_blocking")) {
    pconfig->non_blocking = (unsigned short) atoi(value);
  } else {
    return 0;
  }
  return 1;
}

int make_socket_non_blocking(int sfd) {
  int flags, s;

  flags = fcntl(sfd, F_GETFL, 0);
  if (flags == -1) {
    handle_error(1, errno, "fcntl");
    return -1;
  }

  flags |= O_NONBLOCK;
  s = fcntl(sfd, F_SETFL, flags);
  if (s == -1) {
    handle_error(1, errno, "fcntl");
    return -1;
  }

  return 0;
}

int prepare_in_sock(configuration cfg) {
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int s, sfd;

  memset (&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  s = getaddrinfo(cfg.listen_host, cfg.listen_port, &hints, &result);
  if (s != 0) {
    handle_error(1, errno, "getaddrinfo");
    return -1;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1)
      continue;

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
    if (s == 0) {
      if (cfg.non_blocking > 0) {
        s = make_socket_non_blocking(sfd);
        if (s == -1) {
          perror("non-blocking");
          abort();
        }
      }

      s = listen(sfd, SOMAXCONN);
      if (s == -1) {
        handle_error(1, errno, "listen");
        abort();
      }

      freeaddrinfo(result);
      return sfd;
    }
    close(sfd);
  }

  return -1;
}

struct sockaddr_in get_server_addr() {
  struct sockaddr_in serv_addr;
  struct hostent *server;

  server = gethostbyname(cfg.target_host);
  if (server == NULL) {
    handle_error(1, EHOSTUNREACH, "No such host");

    return NULL;
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy(server->h_addr, (char *) &serv_addr.sin_addr.s_addr, (size_t) server->h_length);
  serv_addr.sin_port = htons(cfg.target_port);

  return serv_addr;
}

void* threaded_tcp_connection(void *_request_data) {
  int sockfd;
  ssize_t n;
  char* request_data = (char*)_request_data;
  target_response* response = (target_response*) malloc(sizeof(target_response));
  response->data = malloc(1024);

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    handle_error(sockfd, errno, "ERROR opening socket");

    response->status = EXIT_FAILURE;
    return response;
  }

  printf("Connecting to %s:%d\n", cfg.target_host, cfg.target_port);

  if (connect(sockfd, (struct sockaddr*) &target_serv_addr, sizeof(target_serv_addr)) < 0) {
    handle_error(1, errno, "Error while connecting");

    response->status = EXIT_FAILURE;
    return response;
  }

  n = write(sockfd, request_data, strlen(request_data));

  if (n < 0) {
    handle_error((int) n, errno, "Error while writing data to socket");

    response->status = EXIT_SUCCESS;
    return response;
  } else {
    printf("Sent! Bytes: %zi\n", n);
    printf("Sent payload: \n%s\n---\n", request_data);
  }

  /* Now read server response */
  printf("Resetting buffer...\n");
  bzero(buffer, 1024);
  printf("Reading from sock...\n");
  n = read(sockfd, response->data, 1023);

  if (n < 0) {
    handle_error((int) n, errno, "Error while reading data from socket");

    response->status = EXIT_FAILURE;
    return response;
  } else {
    printf("Downloaded! Bytes read: %zi\n", n);
  }

  response->status = EXIT_SUCCESS;
  return response;
}


target_response * tcp_request(char* request_data) {
  target_response* response = (target_response*) malloc(sizeof(target_response));

  // Run on separate thread
  pthread_t id;
  errno = pthread_create(&id, NULL, threaded_tcp_connection, (void*)request_data);
  if (errno > 0) {
    handle_error(1, errno, "Failed to create call pthread_create");
  }
  errno = pthread_join(id, (void **) &response);
  if (errno > 0) {
    handle_error(1, errno, "Failed to create call pthread_join");
  }
  return response;
}


void run(int listen_sck_fd, configuration cfg) {
  int upperBound = 1;
  socklen_t clilen;
  stackT freeIndexesStack;
  struct sockaddr_in cli_addr;
  struct pollfd *fds = (struct pollfd *) calloc(cfg.fds_count, sizeof(struct pollfd));

  StackInit(&freeIndexesStack, cfg.fds_count);

  fds[0].fd = listen_sck_fd;
  fds[0].events = POLLIN;

  int i = 0;
  for (i = cfg.fds_count - 1; i > 0; i--) {
    StackPush(&freeIndexesStack, (stackElementT) (i));
  }

  printf("Server started. Maximum concurrent connections: %d\n", cfg.fds_count);

  struct sockaddr_in target_serv_addr = get_server_addr();
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
        ssize_t bufsize = read(fds[i].fd, buffer, BUFSIZE);

        if (bufsize < 0) {
          printf("%d %s\n", errno, strerror(errno));
          handle_error(1, errno, "read");
        }

        if (bufsize > 0) {
          fds[i].revents = 0;

          printf("New request: \n%s\nFrom #%d [fd: %d]\n---\n", buffer, i, fds[i].fd);

          node* entry = dict_get(cache, buffer);
          if (entry) {
            printf("Serving response from cache\n");

            ssize_t sent_bytes = write(fds[i].fd, entry->value, 1024);
            if (sent_bytes > 0) {
              printf("%d bytes sent to requester\n", (int) sent_bytes);
            } else {
              printf("Failed to make request to target...\n");
            }
          } else {
            target_response *payload = tcp_request(buffer);
            if (payload) {
              printf("\nStatus: %d, Payload: \n%s\n---\n", payload->status, payload->data);

              if (payload->status == 0) {
                printf("Response saved to internal cache\n");
                dict_add(cache, buffer, payload->data);
              }

              ssize_t sent_bytes = write(fds[i].fd, payload->data, 1024);
              if (sent_bytes > 0) {
                printf("%d bytes sent to requester\n", (int) sent_bytes);
              } else {
                printf("Failed to make request to target...\n");
              }

              free(payload);
            } else {
              printf("Failed to make request to target...\n");
            }
          }

          printf("Connection closed. Fd: %d, idx: %d\n", fds[i].fd, i);
        } else {
          printf("Nothing to read on fds #%d, idx: %d\n, bufsize: %d", fds[i].fd, i, (int) bufsize);
        }

        close(fds[i].fd);
        StackPush(&freeIndexesStack, (stackElementT) i);
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

  cache = dict_new();

  int listen_sck = prepare_in_sock(cfg);
  if (listen_sck < 0) {
    handle_error(1, errno, "listen_sck");
  }

  run(listen_sck, cfg);
  return 0;
}
