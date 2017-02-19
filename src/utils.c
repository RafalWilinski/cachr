#include <sys/time.h>
/* u_int64_t */
#include <ntsid.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "utils.h"

long get_timestamp() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
  return ms;
}

u_int64_t hash_buffer(char* str) {
  u_int64_t c, hash = 2317;
  while ((c = (u_int64_t) *str++)) {
    hash = ((hash << 5) + hash) + c;
  }

  return hash;
}

uint64_t gettid() {
  pthread_t ptid = pthread_self();
  uint64_t threadId = 0;
  memcpy(&threadId, &ptid, sizeof(ptid));
  return threadId;
}
