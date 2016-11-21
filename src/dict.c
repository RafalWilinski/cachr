#include <string.h>
#include "dict.h"

node* new_node(const char* key, void* value, node *next, node *prev) {
  node *n = malloc(sizeof(node));
  n->key   = key;
  n->value = value;
  n->next  = next;
  n->prev  = prev;

  return n;
}

dict* new_dict() {
  dict *d = malloc(sizeof(dict));
  d->size      = 0;
  d->capacity  = 16;
  d->data      = malloc(d->capacity * sizeof(node));

  return d;
}

uint64_t hash(const char *key) {
  int32_t v = 37;
  uint64_t h;
  unsigned const char *ukey;
  ukey = (unsigned const char *) key;
  h = 0;
  while (*ukey != '\0') {
    h = h * v + *ukey;
    ukey++;
  }

  return h;
}

void* find(node *head, const char *key) {
  while (head && strcmp(head->key, key))
    head = head->next;

  return head ? head->value : NULL;
}

void append(node *head, const char *key, void *value) {
  while (head && strcmp(head->key, key))
    head = head->next;

  if (head)
    head->next = new_node(key, value, NULL, head);
}

void* remove_node(node *head, const char *key) {
  while (head && strcmp(head->key, key))
    head = head->next;

  if (head) {
    if (head->prev)
      head->prev->next = head->next;
    if (head->next)
      head->next->prev = head->prev;
    head->prev = head->next = NULL;
  }

  return head ? head->value : NULL;
}

void resize(dict *d, int64_t capacity) {
  node* new_data = malloc(capacity * sizeof(node));
  node* old_data = d->data;

  // WRITEME

  d->capacity = capacity;
  d->data = new_data;
  free(old_data);
}

void add(dict *d, const char *key, void* value) {
  uint64_t idx = hash(key) % d->capacity;
  append(d->data + idx, key, value);

  if (d->size)
    resize(d, d->capacity * 2);
}

void* get(dict *d, const char *key) {
  uint64_t idx = hash(key) % d->capacity;
  return find(d->data+idx, key);
}

void* remove(dict *d, const char *key) {
  uint64_t idx = hash(key) % d->capacity;
  return remove_node(d->data+idx, key);
}
