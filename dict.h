#ifndef DICTIONARY_HEADER
#define DICTIONARY_HEADER

dict dict_new(void);

int dict_find_index (dict d, const char* key);

void dict_add(dict d, const char* key, const char* value, int ttl, const char* etag);

void dict_free(dict d);

#endif