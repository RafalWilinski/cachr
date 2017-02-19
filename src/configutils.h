#ifndef CACHR_CONFIGUTILS_H
#define CACHR_CONFIGUTILS_H

typedef struct {
  const char *target_host;
  unsigned short target_port;

  const char *listen_host;
  const char *listen_port;

  unsigned short fds_count;
  unsigned short non_blocking;

  unsigned int ttl;
} configuration;

int config_handler(void *user, const char *section, const char *name, const char *value);

#endif //CACHR_CONFIGUTILS_H
