// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ht.h"
#include "log.h"

typedef struct NVP_s NVP;
struct NVP_s {
	char * key;
	void * val;
	NVP * next;
};

struct Hashtable_S {
	size_t size; // number of elements
	size_t nhash; // number of entries in the hash table array
	ht_hash_fn hash; // hash func
	ht_key_free_fn free_key; // func to free a key
	ht_val_free_fn free_val; // func to free a value
	NVP * chains[0]; // the hash table array
};

enum { HASH_MUL = 31 };
unsigned int ht_hash_sz(const char * str) {
	unsigned int h = 0;
	unsigned char * p;
	for(p=(unsigned char *)str; *p!='\0'; p++) {
		h = HASH_MUL * h + *p;
	}
	return h;
}

Hashtable ht_create(unsigned int nhash, ht_hash_fn hash, ht_key_free_fn free_key, ht_val_free_fn free_val) {
	if(nhash==0) {
		nhash = 1021;
	}
	if(hash==NULL) {
		hash = ht_hash_sz;
	}
	int alloc_size = sizeof(struct Hashtable_S) + (nhash * sizeof(NVP *));
	Hashtable ht = malloc(alloc_size);
	if(!ht) {
		elogf("malloc failed: %s",strerror(errno));
		return NULL;
	}
	memset(ht, 0, alloc_size);
	ht->nhash = nhash;
	ht->hash = hash;
	ht->free_key = free_key;
	ht->free_val = free_val;
	return ht;
}

void ht_free(Hashtable ht) {
	ht_clear(ht);
	free(ht);
}

void ht_clear(Hashtable ht) {
	for(size_t i=0; i<ht->nhash; i++) {
		NVP * nvp = ht->chains[i];
		while(nvp!=NULL) {
			NVP * next = nvp->next;
			if(ht->free_key) {
				ht->free_key(nvp->key);
			}			
			if(ht->free_val && nvp->val) {
				ht->free_val(nvp->val);
			}
			free(nvp);
			nvp = next;
		}
		ht->chains[i] = NULL;
	}
	ht->size = 0;
}

size_t ht_size(Hashtable ht) {
	return ht->size;
}

void ht_put(Hashtable ht, char * key, void * val) {
	unsigned int h = ht->hash(key) % ht->nhash;
	NVP * nvp = ht->chains[h];
	while(nvp!=NULL) {
		if(strcmp(key,nvp->key)==0) {
			if(ht->free_val && nvp->val) {
				ht->free_val(nvp->val);
			}
			// update existing val
			nvp->val = val;
			return;
		}
		nvp = nvp->next;
	}
	ht->size++;
	nvp = malloc(sizeof(NVP));
	nvp->key = key;
	nvp->val = val;
	nvp->next = ht->chains[h];
	ht->chains[h] = nvp;
}

bool ht_contains(Hashtable ht, const char * key) {
	unsigned int h = ht->hash(key) % ht->nhash;
	NVP * nvp = ht->chains[h];
	while(nvp!=NULL) {
		if(strcmp(key,nvp->key)==0) {
			return true;
		}
		nvp = nvp->next;
	}
	return false;
}

void * ht_get(const Hashtable ht, const char * key) {
	unsigned int h = ht->hash(key) % ht->nhash;
	NVP * nvp = ht->chains[h];
	while(nvp!=NULL) {
		if(strcmp(key,nvp->key)==0) {
			return (void *)nvp->val;
		}
		nvp = nvp->next;
	}
	return NULL;
}

void ht_dump(const Hashtable ht, FILE * fp, ht_val_print_fn print_val) {
	fprintf(fp,"Hashtable (size=%zu):\n",ht->size);
	for(int i=0; i<ht->nhash; i++) {
		NVP * nvp = ht->chains[i];
		if(nvp) {
			for(;nvp;nvp=nvp->next) {
				fprintf(fp,"[%d] %s",i,nvp->key);
				if(print_val) {
					fprintf(fp,":");
					print_val(fp,nvp->val);
				}
				fprintf(fp,"\n");
			}
		}
	}
}

void ht_stats(const Hashtable ht, FILE * fp) {
	size_t longest = 0;
	size_t chains = 0;
	for(int i=0; i<ht->nhash; i++) {
		NVP * nvp = ht->chains[i];
		if(nvp!=NULL) {
			int len = 0;
			chains++;
			while(nvp!=NULL) {
				len++;
				nvp = nvp->next;
			}
			if(len>longest) {
				longest = len;
			}
		}
	}

	fprintf(fp, "Hashtable Stats\n");
	fprintf(fp, "  size       : %zu\n",ht->size);
	fprintf(fp, "  nhash      : %zu\n",ht->nhash);
	fprintf(fp, "  chains     : %zu\n",chains);
	fprintf(fp, "  longest    : %zu\n",longest);
	if(chains>0) {
		fprintf(fp, "  avg len    : %f\n",((double)ht->size/(double)chains));
	}
}

void ht_val_print_sz(FILE * fp, const void * val) {
	fprintf(fp, "%s",(const char *)val);
}

void ht_val_print_long(FILE * fp, const void * val) {
	fprintf(fp, "%ld",(long)val);
}


#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"
#include "sz.h"

static const char * key1 = "key1";
static const char * val1 = "value1";
static const char * val2 = "value2";

UT_TEST_CASE(ht_free_key) {
	Hashtable ht = ht_create(0,NULL,free,NULL);
	ht_put(ht,strdup(key1),(char*)val1);
	ut_assert(ht_contains(ht,key1));
	ut_assert(0==strcmp(val1,ht_get(ht,key1)));
	ht_clear(ht);
	ut_assert(!ht_contains(ht,key1));
	ht_free(ht);
}

UT_TEST_CASE(ht_free_val) {
	Hashtable ht = ht_create(0,NULL,NULL,free);
	ht_put(ht,(char*)key1,strdup(val1));
	ut_assert(ht_contains(ht,key1));
	ut_assert(0==strcmp(val1,ht_get(ht,key1)));
	ht_clear(ht);
	ut_assert(!ht_contains(ht,key1));
	ht_free(ht);
}

UT_TEST_CASE(ht_put_null) {
	Hashtable ht = ht_create(0,NULL,NULL,NULL);
	ht_put(ht,(char*)key1,0L);
	ut_assert(ht_contains(ht,key1));
	ut_assert(ht_get(ht,key1)==NULL);
	ht_free(ht);
}

UT_TEST_CASE(ut_put_replace) {
	Hashtable ht = ht_create(0,NULL,NULL,free);
	ht_put(ht,(char*)key1,strdup(val1));
	ut_assert(ht_contains(ht,key1));
	ht_put(ht,(char*)key1,strdup(val2));
	ut_assert(0==strcmp(val2,ht_get(ht,key1)));
	ht_free(ht);
}

UT_TEST_CASE(ht_lookups) {
	Sz_Pool words = szp_from_file("src/test-data/words");
	ut_assert(words!=NULL);
	size_t count = szp_size(words);
	Hashtable ht = ht_create(104729,NULL,NULL,NULL);
	for(long i=0; i<count; i++) {
		const char * word = szp_get(words,i);
		ut_assert(!ht_contains(ht,word));
		ht_put(ht,(char*)word,(void *)i);
	}
	ut_assert(count==ht_size(ht));
	ht_stats(ht, stdlog);
	for(int i=0; i<count; i++) {
		const char * word = szp_get(words,i);
		ut_assert(ht_contains(ht,word));
		long val = (long)ht_get(ht,word);
		ut_assert(val == i);
	}
	ht_clear(ht);
	ut_assert(ht_size(ht)==0);
	for(long i=0; i<10; i++) {
		const char * word = szp_get(words,i);
		ut_assert(!ht_contains(ht,word));
		ht_put(ht,(char*)word,(void *)i);
	}
	ht_free(ht);
	szp_free(words);
}

UT_TEST_CASE(ht_dump) {
	Sz_Pool words = szp_from_file("src/test-data/words");
	ut_assert(words!=NULL);
	Hashtable ht = ht_create(104729,NULL,NULL,NULL);

	int dump_count = 10;
	for(long i=0; i<dump_count; i++) {
		const char * word = szp_get(words,i);
		ut_assert(!ht_contains(ht,word));
		ht_put(ht,(char*)word,(void *)i);
	}
	ut_assert(dump_count==ht_size(ht));
	ht_dump(ht, stdlog, ht_val_print_long);
	ht_clear(ht);

	for(long i=0; i<dump_count; i++) {
		const char * word = szp_get(words,i);
		ut_assert(!ht_contains(ht,word));
		ht_put(ht,(char*)word,(char*)word);
	}
	ut_assert(dump_count==ht_size(ht));
	ht_dump(ht, stdlog, ht_val_print_sz);
	ht_free(ht);
	szp_free(words);
}

#endif // !EXCLUDE_UNIT_TESTS
