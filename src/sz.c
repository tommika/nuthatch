// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include "log.h"
#include "sz.h"

/*! \brief Determine if a string starts with a given prefix.
 *
 */
bool sz_starts_with(const char * sz, const char * prefix) {
    return sz_starts_with_case(sz,prefix,false);
}

/*! \brief Determine if a string starts with a given prefix. */
bool sz_starts_with_case(const char * sz, const char * prefix, bool ignore_case) {
    if(sz==NULL || prefix==NULL) {
        return false;
    }
    const char * i = sz;
    const char * j = prefix;
    for(; *i && *j; i++, j++) {
        char a = *i, b = *j;
        if(ignore_case) {
            a = tolower(a);
            b = tolower(b);
        }
        if(a != b) {
            return false;
        }
    }
    return *j==0;
}

/*! \brief Determine if a string contains a given substring. */
bool sz_contains(const char * sz, const char * substr) {
   return sz_contains_case(sz,substr,false);
}

/*! \brief Determine if a string contains a given substring. */
bool sz_contains_case(const char * sz, const char * substr, bool ignore_case) {
    if(sz==NULL || substr==NULL) {
        return false;
    }    
    for(; *sz; sz++) {
        if(sz_starts_with_case(sz,substr,ignore_case)) {
            return true;
        }
    }
    return false;
}

/*! \brief Transforms the string to lower-case.
 *
 */
char * sz_to_lower(char * sz) {
	for(char * pch=sz;*pch;pch++) {
		*pch = tolower(*pch);
	}
    return sz;
}

/*! \brief Determine if two strings are equal.
 *
 */
bool sz_equal(const char * sz1, const char * sz2) {
    if(sz1==sz2) {
        // pointing to same memory, or both NULL
        return true;
    }
    if(sz1==NULL || sz2==NULL) {
        return false;
    }
	for(;*sz1 && *sz2; sz1++, sz2++) {
		if(*sz1!=*sz2) {
			return false;
		}
	}
	return *sz1==*sz2;
}

/*! \brief Determine if two strings are equal, ignoring case. */
bool sz_equal_ignore_case(const char * sz1, const char * sz2) {
    if(sz1==sz2) {
        // pointing to same memory, or both NULL
        return true;
    }
    if(sz1==NULL || sz2==NULL) {
        return false;
    }
	for(;*sz1 && *sz2; sz1++, sz2++) {
		if(tolower(*sz1)!=tolower(*sz2)) {
			return false;
		}
	}
	return *sz1==*sz2;
}

static const char * SP_HT = " \t\n\r";

char * sz_trim(char * sz) {
	if(!sz) {
		return sz;
	}
    // remove leading WS
    for(;*sz && strchr(SP_HT,*sz);sz++);
    if(*sz) {
        // go to end of string
        char * szT;
        for(szT=sz;*szT;szT++);
        // remove trailing ws
        while(strchr(SP_HT,*--szT));
        *(szT+1) = 0;
    }
    return sz;
}


char * sz_cat(const char * sz1, const char * sz2) {
    size_t l1 = strlen(sz1);
    size_t l2 = strlen(sz2);
    char * sz_new = malloc(l1 + l2 + 1);
    strcpy(sz_new,sz1);
    strcat(sz_new,sz2);
    return sz_new;
}

bool sz_is_in_szv(const char * sz, size_t szc, const char ** szv) {
    for(int i=0; i<szc; i++) {
        if(strcmp(sz,szv[i])==0) {
            return true;
        }
    }
    return false;
}

struct Sz_Pool_S {
	size_t cap;
    size_t size;
	char ** szs;
};

Sz_Pool szp_create(size_t init_cap) {
    struct Sz_Pool_S * pool = malloc(sizeof(struct Sz_Pool_S));
    if(!pool) {
        elogf("malloc failed: %s",strerror(errno));
        return NULL;
    }
    if(init_cap<=0) {
        init_cap = 1;
    }
	pool->cap = init_cap;
    pool->size = 0;
	pool->szs = malloc(pool->cap * sizeof(char *));
    return pool;
}

size_t szp_size(Sz_Pool pool) {
	return pool->size;
}

char * szp_get(Sz_Pool pool, size_t i) {
	return pool->szs[i];
}

char * szp_strdup(Sz_Pool pool, const char * sz) {
	if(pool->size >= pool->cap) {
		pool->cap *= 2;
		pool->szs = realloc(pool->szs, pool->cap * sizeof(char *));
	}
	char * sz_dup = strdup(sz);
	pool->szs[pool->size++] = sz_dup;
	return sz_dup;
}

void szp_dump(Sz_Pool pool, FILE * fp) {
    fprintf(fp,"Pool (size=%zu):\n",pool->size);
	for(int i=0; i<pool->size; i++) {
        fprintf(fp, ">%s\n",pool->szs[i]);
    }
}

void szp_clear(Sz_Pool pool) {
	for(int i=0; i<pool->size; i++) {
        free(pool->szs[i]);
    }
	pool->size = 0;
	pool->size = 0;
}

void szp_free(Sz_Pool pool) {
	szp_clear(pool);
	free(pool->szs);
    free(pool);
}

Sz_Pool szp_from_file(char * file) {
	dlogf("Reading strings from file: ",file);
	FILE * fp = fopen(file, "r");
	if(!fp) {
		elogf("Can't open file for reading: %s",file);
        return NULL;
	}
	Sz_Pool szp = szp_create(93398);
    if(!szp) {
        fclose(fp);
        return NULL;
    }
    size_t buffer_size = 0;
    char * line_buffer = NULL;
    size_t line_len;
    while( (line_len = getline(&line_buffer, &buffer_size, fp)) != -1 ) {
        // line includes the newline character, so remove it
        line_buffer[line_len-1] = 0;
        szp_strdup(szp,line_buffer);
    }
    free(line_buffer);
    fclose(fp);
    dlogf("%zu strings read from file '%s'\n",szp_size(szp),file);

	return szp;
}


#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"

UT_TEST_CASE(sz_equal) {
    Sz_Pool pool = szp_create(1);
    char * fred = szp_strdup(pool, "Fred");
    ut_assert(sz_equal(fred,"Fred"));
    ut_assert(!sz_equal(fred,"Bob"));
    ut_assert(sz_equal_ignore_case("FRED","fred"));
    ut_assert(!sz_equal_ignore_case("Fred","Bob"));
    szp_free(pool);
}

UT_TEST_CASE(sz_starts_with) {
    ut_assert(sz_starts_with("A Guy Named Fred","A Guy"));
    ut_assert(!sz_starts_with("A Guy Named Fred","a gUY"));
    ut_assert(sz_starts_with_case("A Guy Named Fred","A Guy",false));
    ut_assert(!sz_starts_with("A Guy Named Fred","Fred"));
    ut_assert(sz_starts_with_case("A Guy Named Fred","a gUY",true));
    ut_assert(!sz_starts_with_case("A Guy Named Fred","gUY",true));
    ut_assert(!sz_starts_with_case("A","a gUY",true));
}

UT_TEST_CASE(sz_contains) {
    ut_assert(sz_contains("A Guy Named Fred","A Guy"));
    ut_assert(sz_contains("A Guy Named Fred","Fred"));
    ut_assert(sz_contains("A Guy Named Fred","Named"));
    ut_assert(!sz_contains("A Guy Named Fred","a guy"));
    ut_assert(!sz_contains("A Guy Named Fred","fred"));
    ut_assert(!sz_contains("A Guy Named Fred","named"));

    ut_assert(sz_contains_case("A Guy Named Fred","a guY",true));
    ut_assert(!sz_contains_case("A Guy Named Fred","Joe",true));
    ut_assert(sz_contains_case("A Guy Named Fred","fRED",true));
    ut_assert(sz_contains_case("A Guy Named Fred","nAMED",true));
    ut_assert(sz_contains_case("A Guy Named Fred","a gUY",true));
    ut_assert(sz_contains_case("A Guy Named Fred","fRED",true));
    ut_assert(sz_contains_case("A Guy Named Fred","nAMED",true));
}

UT_TEST_CASE(sz_to_lower) {
    Sz_Pool strings = szp_create(16);
    ut_assert(0==strcmp("hello, world!",sz_to_lower(szp_strdup(strings,"Hello, World!") ) ) );
    szp_free(strings);
}

UT_TEST_CASE(sz_trim) {
    Sz_Pool strings = szp_create(16);
    #define TEST_SZ_TRIM(expected,sz) { ut_assert(0==strcmp(expected, sz_trim(szp_strdup(strings,sz))));}
    ut_assert(sz_trim(NULL)==NULL);
    TEST_SZ_TRIM("","");
    TEST_SZ_TRIM(""," ");
    TEST_SZ_TRIM(""," \t ");
    TEST_SZ_TRIM("wow"," wow");
    TEST_SZ_TRIM("wow","wow ");
    TEST_SZ_TRIM("wow"," wow ");
    TEST_SZ_TRIM("wow","wow");
    szp_free(strings);
}

UT_TEST_CASE(sz_is_in_szv) {
    const char * szv[] = {"apple","banana","orange"};
    const size_t szc = sizeof(szv)/sizeof(char*);
    for(int i=0;i<szc;i++) {
        ut_assert(sz_is_in_szv(szv[i],szc,szv));
    }
}

UT_TEST_CASE(szp) {
    Sz_Pool szp = szp_create(0);
    ut_assert(0==szp_size(szp));
    ut_assert(0==strcmp("one",szp_strdup(szp,"one")));
    ut_assert(0==strcmp("two",szp_strdup(szp,"two")));
    ut_assert(2==szp_size(szp));
    szp_dump(szp,stdlog);
    szp_free(szp);
}

UT_TEST_CASE(szp_from_file) {
    Sz_Pool szp = szp_from_file("src/test-data/words");
	ut_assert(szp!=NULL);
    ut_assert(szp_size(szp)==93398)
    szp_free(szp);

    ut_assert(szp_from_file("this-file-does-not-exist")==NULL);
}

#endif // !EXCLUDE_UNIT_TESTS
