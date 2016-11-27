#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/fcntl.h>

#include "src/dict.h"
#include "src/ini.h"
#include "src/error.h"

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
} configuration;

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

    s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
    if (s == 0) {
      s = make_socket_non_blocking(sfd);
      if (s == -1) {
        perror("non-blocking");
        abort();
      }

      s = listen(sfd, SOMAXCONN);
      if (s == -1) {
        handle_error(1, errno, "listen");
        abort();
      }

      freeaddrinfo(result);
    }
    close(sfd);
  }

  return -1;
}

void run(int listen_sck_fd, configuration cfg) {
  socklen_t clilen;
  struct pollfd *fds = (struct pollfd *) calloc(cfg.fds_count, sizeof(struct pollfd));
  fds[0].fd = listen_sck_fd;
  fds[0].events = POLLIN;

  printf("Server started...\n");

  while (poll(fds, (nfds_t) sck_cnt, -1)) {
    for (int i = 0; i < sck_cnt; i++) {
      if (i == 0) {
        fds[i].revents = 0;
        int newsockfd = accept(listen_sck_fd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
          handle_error(1, errno, "accept");
        }

        fds[sck_cnt].fd = newsockfd;
        fds[sck_cnt].events = POLLIN;
        sck_cnt++;

        printf("New connection on file descriptor #%d\n", newsockfd);
      } else {
        int c = 0;
        ssize_t bufsize = read(fds[i].fd, buffer, BUFSIZE);

        if (bufsize > 0) {
          fds[i].revents = 0;

          printf("New request: %s from #%d [fd: %d]", buffer, i, fds[i].fd);

          while (fds[c].fd != 0) {
            write(fds[c].fd, buffer, (size_t) bufsize);
            c++;
          }
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  configuration cfg;
  char *config_name;

  if (argc >= 2) {
    config_name = strdup(argv[1]);
  } else {
    config_name = "config.ini";
  }

  if (ini_parse(config_name, config_handler, &cfg) < 0) {
    printf("Can't load '%s'\n", config_name);
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
