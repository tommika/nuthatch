// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <stdlib.h>
#include <string.h>
#include "mem.h"

void * mem_append(void * dest, size_t dest_len, const void * src, size_t src_len, size_t * new_len) {
    size_t len = dest_len + src_len;
    if(dest==NULL) {
        dest = malloc(src_len);
    } else {
        dest = realloc(dest,len);
    }
    if(dest==NULL) {
        return NULL;
    }
    memcpy(dest+dest_len,src,src_len);
    if(new_len!=NULL) {
        *new_len = len;
    }
    return dest;
}
