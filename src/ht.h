// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __HT__H__
#define __HT__H__

#include <stdio.h>
#include <stdbool.h>

typedef struct Hashtable_S * Hashtable;
typedef unsigned int (*ht_hash_fn)(const char * key);
typedef void (*ht_key_free_fn)(void * val);
typedef void (*ht_val_free_fn)(void * val);
typedef void (*ht_val_print_fn)(FILE *fp, const void * val);

unsigned int ht_hash_sz(const char * val);
void ht_val_print_sz(FILE * fp, const void * val);
void ht_val_print_long(FILE * fp, const void * val);

Hashtable ht_create(unsigned int nhash, ht_hash_fn hash, ht_key_free_fn free_key, ht_val_free_fn free_val);
void ht_clear(Hashtable ht);
void ht_free(Hashtable ht);
size_t ht_size(Hashtable ht);

/* ht_put: The hashtable will take ownership of the given key and value, and free the
 * storage for these using free functions specified during ht_create.
 */
void ht_put(Hashtable ht, char * key, void * val);
bool ht_contains(Hashtable ht, const char * key);
/* Returns a borrowed pointer to the value associated with the given key. */
void * ht_get(const Hashtable ht, const char * key);
void ht_stats(const Hashtable ht, FILE *);
void ht_dump(const Hashtable ht, FILE *, ht_val_print_fn print_val);

#endif // __HT__H__

