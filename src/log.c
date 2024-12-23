// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <stdarg.h>
#include "log.h"

FILE * stdlog = NULL;
Log_Level __cur_log_level = LEVEL_DEFAULT;


__attribute__ ((__constructor__))
static void _init() {
	log_init(stderr,LEVEL_DEFAULT);
}

void log_init(FILE * out, Log_Level level) {
	stdlog = out;
	__cur_log_level = level;
}

void log_set_level(Log_Level level) {
	__cur_log_level = level;
}
Log_Level log_get_level() {
	return __cur_log_level;
}

#define LEVEL_NAME(NAME) \
	static const char * _##NAME = #NAME;

LEVEL_NAME(ALL);
LEVEL_NAME(DEBUG);
LEVEL_NAME(INFO);
LEVEL_NAME(WARN);
LEVEL_NAME(ERROR);
LEVEL_NAME(UNKNOWN);

const char * log_level_name(Log_Level level) {
	switch(level) {
		case LEVEL_ALL:
			return _ALL;
		case LEVEL_DEBUG:
			return _DEBUG;
		case LEVEL_INFO:
			return _INFO;
		case LEVEL_WARNING:
			return _WARN;
		case LEVEL_ERROR:
			return _ERROR;
		default:
			return _UNKNOWN;
	}
}

#define MAX_MSG 128
void __log(FILE * out, Log_Level level, const char * file, int line, const char * func, const char * fmt, ...) {
	char msg[MAX_MSG];
	va_list args;
	va_start(args, fmt);
	int msg_len = vsnprintf(msg,sizeof(msg),fmt,args);
	va_end(args);
	if(msg_len<0) {
		// Failed to format message
		fprintf(out,"%-5s %d %s [%s:%d] <Failed to format message>\n",log_level_name(level),getpid(),func,file,line);
	} else if (msg_len>=MAX_MSG) {
		// Message was truncated
		fprintf(out,"%-5s %d %s: %s...\n",log_level_name(level),getpid(), func, msg);
	} else {
		// A-OK
		fprintf(out,"%-5s %d %s: %s\n",log_level_name(level),getpid(), func, msg);
	}
}


#ifndef EXCLUDE_UNIT_TESTS

#include <stdlib.h>
#include "ut.h"
#include "rnd.h"

UT_TEST_CASE(log) {
	log_set_level(LEVEL_ALL);
	ut_assert(log_get_level()==LEVEL_ALL);
	ut_assert(strcmp(log_level_name(LEVEL_ALL),_ALL)==0);
	ut_assert(strcmp(log_level_name(LEVEL_DEBUG),_DEBUG)==0);
	ut_assert(strcmp(log_level_name(LEVEL_INFO),_INFO)==0);
	ut_assert(strcmp(log_level_name(LEVEL_WARNING),_WARN)==0);
	ut_assert(strcmp(log_level_name(LEVEL_ERROR),_ERROR)==0);
	ut_assert(strcmp(log_level_name(-1),_UNKNOWN)==0);

	ilogf("Hello, World!",getpid());
	ilogf("My pid=%d",getpid());
	ilogf("My username is %s",getenv("LOGNAME"));
	// write a truncated message
	char * big_string = rnd_sz(128,NULL);
	ilogf("Truncated message: %s",big_string);
	free(big_string);
}

#endif // !EXCLUDE_UNIT_TESTS
