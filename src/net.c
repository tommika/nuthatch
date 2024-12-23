// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 

#include <stdlib.h>
#include <string.h>
#include "net.h"

/*! \brief Parse the given string as a dot notated ipv4 address.
 *
 */
uint32_t net_atoipv4(const char * sz) {
	if(strlen(sz)>15) {
		return INVALID_ADDR;
	}
	char sz_copy[1+strlen(sz)];
	strcpy(sz_copy,sz);
	char * p;
   	if(!(p=strtok(sz_copy,"."))) {
		return INVALID_ADDR;
	}
	uint32_t ipv4 = ((uint32_t)atoi(p) & 0xFF);
   	if(!(p=strtok(NULL,"."))) {
		return INVALID_ADDR;
	}
	ipv4 |= ((uint32_t)atoi(p) & 0xFF) << 8;
   	if(!(p=strtok(NULL,"."))) {
		return INVALID_ADDR;
	}
	ipv4 |= ((uint32_t)atoi(p) & 0xFF) << 16;
   	if(!(p=strtok(NULL,"."))) {
		return INVALID_ADDR;
	}
	ipv4 |= ((uint32_t)atoi(p) & 0xFF) << 24;

	return ipv4;
}

#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"

UT_TEST_CASE(net_atoipv4) {
	ut_assert(net_atoipv4("1.2.3.4")!=INVALID_ADDR);
	ut_assert(net_atoipv4("")==INVALID_ADDR);
	ut_assert(net_atoipv4("a.b.c.d")!=INVALID_ADDR);
	ut_assert(net_atoipv4("123.123.123.123.123")==INVALID_ADDR);
	ut_assert(net_atoipv4("1")==INVALID_ADDR);
	ut_assert(net_atoipv4("1.2")==INVALID_ADDR);
	ut_assert(net_atoipv4("1.2.3")==INVALID_ADDR);
	ut_assert(net_atoipv4("1.2..4")==INVALID_ADDR);
	ut_assert(net_atoipv4("...")==INVALID_ADDR);
}

#endif // !EXCLUDE_UNIT_TESTS
