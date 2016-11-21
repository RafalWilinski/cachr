#ifndef DICTIONARY_HEADER
#define DICTIONARY_HEADER

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct node_ {
  const char* key;
  void *value;

  struct node_ *next;
  struct node_ *prev;
} node;

typedef struct {
  int64_t    size;
  int64_t    capacity;
  node* data;
  node* entries;
} dict;

node* new_node(const char* key, void* value, node *next, node *prev);

dict* new_dict();

uint64_t hash(const char *key);

void* find(node *head, const char *key);

void append(node *head, const char *key, void *value);

void* remove_node(node *head, const char *key);

void resize(dict *d, int64_t capacity);

void add(dict *d, const char *key, void* value);

void* get(dict *d, const char *key);

void* remove(dict *d, const char *key);
#endif