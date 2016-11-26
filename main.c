#include <stdlib.h>
#include "src/dict.h"
#include "src/ini.h"
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>

#define BUFSIZE 1024
char buffer[BUFSIZE];

struct sockaddr_in serv_addr, cli_addr;

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
        pconfig->target_port = atoi(value);
    } else if (MATCH("target", "host")) {
        pconfig->target_host = strdup(value);
    } else  if (MATCH("listen", "port")) {
        pconfig->listen_port = atoi(value);
    } else if (MATCH("listen", "host")) {
        pconfig->listen_host = strdup(value);
    } else if (MATCH("poll", "fds_count")) {
        pconfig->fds_count = atoi(value);
    } else {
        return 0;
    }
    return 1;
}

int prepare_target_sock() {

}

int prepare_in_sock(configuration cfg) {
    struct addrinfo *ao;
    int res = getaddrinfo(cfg.listen_host, cfg.listen_port, nullptr, &ao);
    if (res || !ao) error(1, errno, "getaddrinfo");

    int sock = socket(ao->ai_family, SOCK_STREAM, 0);
    if (sock < 0) error(1, errno, "socket");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(cfg.listen_port);

    if (bind(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        return 1;
    }

    res = listen(sock, 1024);
    if (res) error(1, errno, "connect");
    freeaddrinfo(ao);
}

int remove_sock() {

}

int run(configuration cfg, int listen_sck_fd) {
    struct pollfd *fds = (struct pollfd) calloc(cfg.fds_count, sizeof(struct pollfd));
    fds[0].fd = listen_sck_fd;
    fds[0].events = POLLIN;

    while (poll(fds, sck_cnt, -1)) {
        for (int i = 0; i < sck_cnt; i++) {
            if (i == 0) {
                fds[i].revents = 0;
                int newsockfd = accept(sock, (struct sockaddr *) &cli_addr, &clilen);

                fds[sck_cnt].fd = newsockfd;
                fds[sck_cnt].events = POLLIN;
                sck_cnt++;

                printf("New connection on file descriptor #%d\n", newsockfd);
            } else {
                int c = 0;
                ssize_t bufsize = read(fds[i].fd, bufor, BUFSIZE);

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
    run(listen_sck);
}
