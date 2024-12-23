// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <errno.h>
#include <stdlib.h>

#include "log.h"
#include "rnd.h"

unsigned char * _rnd_mem_ext(size_t len, unsigned char * buff, const char * urandom_path);

unsigned char * rnd_mem(size_t len, unsigned char * buff) {
    return _rnd_mem_ext(len, buff, "/dev/urandom");
}

unsigned char * _rnd_mem_ext(size_t len, unsigned char * buff, const char * urandom_path) {
    FILE * f_random = fopen(urandom_path, "r");
    if(!f_random) {
        elogf("Failed to open urandom: %s",strerror(errno));
        return NULL;
    }
    if(buff==NULL) {
        buff = malloc(len);
    } else {
        buff = realloc(buff,len);
    }
    if(!buff) {
        elogf("(m/re)alloc failed: %s",strerror(errno));
        return NULL;
    }
    if(fread(buff,len,1,f_random)!=1) {
        elogf("fread failed: %s",strerror(errno));
        return NULL;
    }
    fclose(f_random);
    return buff;
}

char * rnd_sz(size_t len, char * buff) {
    buff = (char *)rnd_mem(len,(unsigned char *)buff);
    unsigned char * pch = (unsigned char *)buff;
    for(int i=0; i<len; i++, pch++) {
        *pch = (*pch % 95) + 32;
    }
    buff[len-1] = 0;
    return buff;
}

#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"

#define TEST_DATA_DIR "src/test-data/"

UT_TEST_CASE(rnd_mem) {
    unsigned char * bytes = rnd_mem(128,NULL);
    ut_assert(bytes!=NULL);
    unsigned int chk1 = 0;
    for(int i=0;i<128;i++) {
        chk1 += bytes[i];
    }
    bytes = rnd_mem(128,bytes);
    ut_assert(bytes!=NULL);
    unsigned int chk2 = 0;
    for(int i=0;i<128;i++) {
        chk2 += bytes[i];
    }
    ut_assert(chk1!=chk2);
    bytes = rnd_mem(512,bytes);
    ut_assert(bytes!=NULL);
    free(bytes);
}

UT_TEST_CASE(rnd_str) {
    char * sz = rnd_sz(128,NULL);
    ut_assert(sz!=NULL);
    ut_assert(strlen(sz)==127);
    free(sz);
}

UT_TEST_CASE(rnd_mem_cant_open) {
    unsigned char * sz = _rnd_mem_ext(128,NULL,"/dev/bogus");
    ut_assert(sz==NULL);
}


#endif // !EXCLUDE_UNIT_TESTS
