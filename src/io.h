// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __IO_H__
#define __IO_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

/*! \brief Reads a CRLF-terminated line from the given file descriptor. Returns
 *         the length of the line (not including the CRLF terminator). If the
 *         line exceeds the given buffer, or if EOF or other error is
 *         encountered, returns -1 and sets errno. The line is returned as a
 *         null-terminated string in the given buffer. The buffer must have
 *         room for the entire line plus the null-terminator.
 */
ssize_t io_read_line_crlf(int fd, void *buffer, size_t buffer_len);


int io_encode_hex(FILE * out, const unsigned char * data, size_t len);
int io_encode_bin(FILE * out, const unsigned char * data, size_t len);
int io_encode_b64(FILE * out, const unsigned char * data, size_t len);

size_t io_copy_stream(int fd_dst, int fd_src, size_t block_size);

bool io_is_dir(const char * path);

#endif // __IO_H__
