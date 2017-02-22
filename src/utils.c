#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "utils.h"

long get_timestamp() {
  return (unsigned long) time(NULL);
}

uint64_t hash_buffer(char* str) {
  uint64_t c, hash = 2317;
  while ((c = (uint64_t) *str++)) {
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
