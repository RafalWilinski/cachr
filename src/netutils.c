#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "configutils.h"

int make_socket_non_blocking(int sfd) {
  int  s = fcntl(sfd, F_SETFL, O_NONBLOCK);
  if (s == -1) {
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
    return -1;
  }

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1)
      continue;

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    s = bind(sfd, rp->ai_addr, rp->ai_addrlen);
    if (s == 0) {

      s = listen(sfd, SOMAXCONN);
      if (s == -1) {
        return -1;
      }

      freeaddrinfo(result);
      int blocking_status = make_socket_non_blocking(sfd);
      if (blocking_status == -1) {
        close(sfd);
        return -1;
      }
      return sfd;
    }
    close(sfd);
  }

  return -1;
}

struct sockaddr_in get_server_addr(configuration cfg) {
  struct sockaddr_in serv_addr;
  struct hostent *server;

  server = gethostbyname(cfg.target_host);
  if (server == NULL) {
    exit(0);
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy(server->h_addr, (char *) &serv_addr.sin_addr.s_addr, (size_t) server->h_length);
  serv_addr.sin_port = htons(cfg.target_port);

  return serv_addr;
}
