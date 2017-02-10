#include <string.h>
#include <stdlib.h>
#include "configutils.h"

int config_handler(void *user, const char *section, const char *name, const char *value) {
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
  } else if (MATCH("cache", "ttl")) {
    pconfig->ttl = (unsigned short) atoi(value);
  } else {
    return 0;
  }
  return 1;
}
