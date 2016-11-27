#include <stdlib.h>
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>

#include "src/dict.h"
#include "src/ini.h"
#include "src/error.h"

#define BUFSIZE 1024
char buffer[BUFSIZE];

struct sockaddr_in serv_addr, cli_addr;

// Number of sockets connected in fds array
int sck_cnt = 0;

typedef struct {
    const char* target_host;
    unsigned short target_port;

    const char* listen_host;
    unsigned short listen_port;

    unsigned short fds_count;
} configuration;

static int config_handler(void* user, const char* section, const char* name, const char* value)  {
    configuration* pconfig = (configuration*)user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("target", "port")) {
        pconfig->target_port = (unsigned short) atoi(value);
    } else if (MATCH("target", "host")) {
        pconfig->target_host = strdup(value);
    } else  if (MATCH("listen", "port")) {
        pconfig->listen_port = (unsigned short) atoi(value);
    } else if (MATCH("listen", "host")) {
        pconfig->listen_host = strdup(value);
    } else if (MATCH("poll", "fds_count")) {
        pconfig->fds_count = (unsigned short) atoi(value);
    } else {
        return 0;
    }
    return 1;
}

int prepare_in_sock(configuration cfg) {
    struct addrinfo *ao;
    int res = getaddrinfo(cfg.listen_host, (const char *) cfg.listen_port, 0, &ao);
    if (res || !ao) handle_error(1, errno, "getaddrinfo");

    int sock = socket(ao->ai_family, SOCK_STREAM, 0);
    if (sock < 0) handle_error(1, errno, "socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(cfg.listen_port);

    if (bind(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        return 1;
    }

    res = listen(sock, 1024);
    if (res) handle_error(1, errno, "connect");
    freeaddrinfo(ao);
}

int run(int listen_sck_fd, configuration cfg) {
    socklen_t clilen;
    struct pollfd *fds = (struct pollfd*) calloc(cfg.fds_count, sizeof(struct pollfd));
    fds[0].fd = listen_sck_fd;
    fds[0].events = POLLIN;

    while (poll(fds, (nfds_t) sck_cnt, -1)) {
        for (int i = 0; i < sck_cnt; i++) {
            if (i == 0) {
                fds[i].revents = 0;
                int newsockfd = accept(listen_sck_fd, (struct sockaddr *) &cli_addr, &clilen);

                fds[sck_cnt].fd = newsockfd;
                fds[sck_cnt].events = POLLIN;
                sck_cnt++;

                printf("New connection on file descriptor #%d\n", newsockfd);
            } else {
                int c = 0;
                ssize_t bufsize = read(fds[i].fd, buffer, BUFSIZE);

                if (bufsize > 0) {
                    fds[i].revents = 0;

                    printf("New request: %s from %d [fd: %d]", buffer, i, fds[i]);
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
    char* config_name;

    if (argc >= 2) {
        config_name = strdup(argv[1]);
    } else {
        config_name = "config.ini";
    }

    if (ini_parse(config_name, config_handler, &cfg) < 0) {
        printf("Can't load '%s'\n", config_name);
        return 1;
    }

    printf("Cachr started with config from '%s': host=%s, port=%d...\n",
           config_name, cfg.listen_host, cfg.listen_port);

    int listen_sck = prepare_in_sock(cfg);
    run(listen_sck, cfg);
}
