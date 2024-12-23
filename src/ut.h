// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __UT_H__
#define __UT_H__

#ifndef EXCLUDE_UNIT_TESTS

#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <sys/times.h>

#include "log.h"
#include "sz.h"

typedef void (*TestFn)(void);

extern int ut_register(const char * test_name, TestFn test_fn);
extern int ut_test_driver(int, char**);

#define UT_TEST_CASE(T) \
    static void _ut_##T(void);\
    __attribute__ ((__constructor__)) void register_##T() {ut_register(#T, _ut_##T); } \
    static void _ut_##T(void)

extern void __ut_assert_failed(FILE * out, const char * test_case, const char * file, int line, const char * msg);

#define ut_assert(EXPR) \
    { if(!(EXPR)) { __ut_assert_failed(stderr,__func__,__FILE__,__LINE__,#EXPR); } }

#define ut_fail(MSG) \
    { if(!(EXPR)) { __ut_assert_failed(stderr,__func__,__FILE__,__LINE__,#MSG); } }

#endif // EXCLUDE_UNIT_TESTS
#endif // __UT_H__
