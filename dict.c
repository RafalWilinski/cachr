#include <stdlib.h>
#include "dict.h"

struct dict_entry {
    const char* key;
    const char* data;
    int ttl;
    const char *etag;
};

struct dict {
    int len;
    struct dict_entry *entries;
};

dict dict_new(void) {

}

int dict_find_index (dict d, const char* key) {

}

void dict_add(dict d, const char* key, const char* value, int ttl, const char* etag) {

}

void dict_free(dict d) {

}
