#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <arpa/inet.h>

#include "src/dict.h"
#include "src/ini.h"
#include "src/error.h"
#include "src/stack.h"

#define BUFSIZE 1024
char buffer[BUFSIZE];

struct sockaddr_in cli_addr;

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
  const char* host;
  const char* type;
  const char* content_length;
  const char* etag;
  const char* data;
} http_request;

typedef struct {
  const char* data;
  short status;
} target_request;

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
  hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;     /* All interfaces */

  s = getaddrinfo(cfg.listen_host, cfg.listen_port, &hints, &result);
  if (s != 0) {
    handle_error(1, errno, "getaddrinfo");
    return -1;
  }

  /* Iterate through all "suggestions" and find suitable IPs */
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

char* parse_response(char* response) {
  char *token = NULL;
  token = strtok(response, "\n");
  while (token) {
    printf("Current token: %s.\n", token);
    token = strtok(NULL, "\n");
  }
}

target_request* tcp_request(char* request_data) {
  struct sockaddr_in sck_addr;
  pid_t pid;
  int status = 0, sck;
  ssize_t bytes_read;
  char buffer[1024];

  // Run on separate thread
  if ((pid = fork ()) == 0) {
    memset (&sck_addr, 0, sizeof sck_addr);
    sck_addr.sin_family = AF_INET;
    inet_aton (cfg.target_host, &sck_addr.sin_addr);
    sck_addr.sin_port = htons (cfg.target_port);

    if ((sck = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
      handle_error(sck, errno, "Could not create socket for target request");
      exit(EXIT_FAILURE);
    }

    if (connect (sck, (struct sockaddr*) &sck_addr, sizeof sck_addr) < 0) {
      handle_error(EHOSTUNREACH, EHOSTUNREACH, "Could not connect to host");
      exit(EXIT_FAILURE);
    }

    printf("Requesting %s:%d...\n", cfg.target_host, cfg.target_port);

    // Send request data
    // ssize_t write_status = write(sck, request_data, sizeof(request_data));
    // printf("written: %d", (int) write_status);

    // Receive response from server
    while ((bytes_read = read (sck,  buffer, 1024)) > 0)
      write (1, buffer, (size_t) bytes_read);

    printf("Data from target received: %s\n", buffer);
    close (sck);
    exit(EXIT_SUCCESS);
  }

  waitpid(pid, &status, WCONTINUED);

  target_request *resp = malloc(sizeof(target_request));

  if(WIFEXITED(status)) {
    if(WEXITSTATUS(status) == 0) {
      memcpy(buffer, resp->data, sizeof(buffer));
      resp->status = 1;
    } else {
      resp->status = -1;
    }
  } else {
    resp->status = -1;
  }

  return resp;
}


void run(int listen_sck_fd, configuration cfg) {
  int upperBound = 1;
  socklen_t clilen;
  stackT freeIndexesStack;
  struct pollfd *fds = (struct pollfd *) calloc(cfg.fds_count, sizeof(struct pollfd));
  StackInit(&freeIndexesStack, cfg.fds_count);

  fds[0].fd = listen_sck_fd;
  fds[0].events = POLLIN;

  int i = 0;
  for (i = cfg.fds_count - 1; i > 0; i--) {
    StackPush(&freeIndexesStack, (stackElementT) (i));
  }

  printf("Server started. Maximum concurrent connections: %d\n", cfg.fds_count);

  while (poll(fds, (nfds_t) sck_cnt, -1)) {
    printf("Poll!\n");

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

          printf("New request: %s\nFrom #%d [fd: %d]\n", buffer, i, fds[i].fd);
          parse_response(buffer);

          // Find in cache
          // If not found request & respond
          // Else respond with content from cache
          // Close socket

          target_request *payload = tcp_request(buffer);
          printf("Status: %d, Payload: %s", payload->status, payload->data);

          close(fds[i].fd);
          StackPush(&freeIndexesStack, (stackElementT) i);

          printf("Connection closed. Fd: %d, idx: %d\n", fds[i].fd, i);
        } else {
          printf("Nothing to read on fds #%d, idx: %d\n, bufsize: %d", fds[i].fd, i, (int) bufsize);
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
