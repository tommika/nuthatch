// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __LOG_H__
#define __LOG_H__

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

typedef enum Log_Level {
	LEVEL_ALL=0,
	LEVEL_DEBUG,
	LEVEL_INFO,
	LEVEL_WARNING,
	LEVEL_ERROR,
	LEVEL_DEFAULT = LEVEL_INFO,
} Log_Level;

extern Log_Level __cur_log_level;

extern void log_init(FILE * out, Log_Level level);
extern const char * log_level_name(Log_Level level);
extern void log_set_level(Log_Level level);
extern Log_Level log_get_level();
extern void __log(FILE *, Log_Level, const char * file, int line, const char * func, const char * fmt, ...);

extern FILE * stdlog;

#define logging(level) ((level)>=__cur_log_level)
#define logf(level,fmt,...) {if(logging(level)) {__log(stdlog,level,__FILE__,__LINE__,__func__,fmt, ##__VA_ARGS__);}}
#define ilogf(fmt,...) logf(LEVEL_INFO,   fmt, ##__VA_ARGS__)
#define dlogf(fmt,...) logf(LEVEL_DEBUG,  fmt, ##__VA_ARGS__)
#define wlogf(fmt,...) logf(LEVEL_WARNING,fmt, ##__VA_ARGS__)
#define elogf(fmt,...) logf(LEVEL_ERROR,  fmt, ##__VA_ARGS__)

#endif // __LOG_H__
