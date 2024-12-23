// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __NET_H__
#define __NET_H__

#include <stdint.h>

#define INVALID_ADDR ((uint32_t)-1)

uint32_t net_atoipv4(const char * sz);

#endif // __NET_H__
