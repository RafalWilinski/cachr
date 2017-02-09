#ifndef CACHR_NETURILS_H
#define CACHR_NETURILS_H

#include "configutils.h"

int prepare_in_sock(configuration cfg);
struct sockaddr_in get_server_addr(configuration cfg);
int make_socket_non_blocking(int sfd);

#endif //CACHR_NETURILS_H
