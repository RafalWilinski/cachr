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
    char* config_name;

    if (argc >= 2) {
        config_name = strdup(argv[1]);
    } else {
        config_name = "config.ini";
    }

    if (ini_parse(config_name, handler, &config) < 0) {
        printf("Can't load '%s'\n", config_name);
        return 1;
    }

    printf("Cachr started with config from '%s': host=%s, port=%d...\n", config_name, config.host, config.port);
}
