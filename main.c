#include <stdlib.h>
#include "src/dict.h"
#include "src/ini.h"

typedef struct {
    const char* host;
    unsigned short port;
} configuration;

static int handler(void* user, const char* section, const char* name, const char* value)  {
    configuration* pconfig = (configuration*)user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("target", "port")) {
        pconfig->port = atoi(value);
    } else if (MATCH("target", "host")) {
        pconfig->host = strdup(value);
    } else {
        return 0;
    }
    return 1;
}


int main(int argc, char **argv) {
    configuration config;

    if (ini_parse("test.ini", handler, &config) < 0) {
        printf("Can't load 'test.ini'\n");
        return 1;
    }

    printf("Cachr started with config from 'test.ini': host=%s, port=%d...\n", config.host, config.port);
}
