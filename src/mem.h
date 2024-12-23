// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __MEM_H__
#define __MEM_H__

#include <stddef.h>

/* \brief Appends src to dest, expanding the memory allocated to dest as needed. 
 * If dest is null, returns a copy of src. 
 * If non-null, new_len is updated to hold the new length.
 * 
 * Typical usage:
 * void * buff = NULL;
 * size_t buff_len = 0;
 * buff = buff_append(buff,buff_len,data_1,data_1_len,&buff_len)
 * buff = buff_append(buff,buff_len,data_2,data_2_len,&buff_len)
 * 
 * 
 */
void * mem_append(void * dest, size_t dest_len, const void * src, size_t src_len, size_t * new_len);

#endif // __MEM_H__
