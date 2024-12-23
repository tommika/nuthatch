// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __RND_H__
#define __RND_H__

#include <stddef.h>

extern unsigned char * rnd_mem(size_t len, unsigned char * buff);
extern char * rnd_sz(size_t len, char * buff);

#endif // __RND_H__
