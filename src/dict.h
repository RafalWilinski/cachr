#ifndef __DICTIONARY_H__
#define __DICTIONARY_H__

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

node* dict_new_node(const char* key, void* value, node *next, node *prev);

dict* dict_new();

uint64_t dict_hash(const char *key);

void* dict_find(node *head, const char *key);

void dict_append(node *head, const char *key, void *value);

void* dict_remove_node(node *head, const char *key);

void dict_resize(dict *d, int64_t capacity);

void dict_add(dict *d, const char *key, void* value);

void* dict_get(dict *d, const char *key);

void* dict_remove(dict *d, const char *key);
#endif