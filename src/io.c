// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <string.h>

#include "io.h"
#include "log.h"

size_t io_copy_stream(int fd_out, int fd_in, size_t buff_size) {
	long total = 0;
	int n;
	unsigned char buff[buff_size];
	errno = 0;
	while((n = read(fd_in,buff,sizeof(buff)))>0) {
		if(write(fd_out,buff,n) != n) {
			return -1;
		}
		total += n;
	}
	if(errno>0) {
		return -1;
	}
	return total;
}

ssize_t io_read_line_crlf(int fd, void *buffer, size_t buffer_len) {
    if(!buffer || buffer_len < 1) {
		errno = EINVAL;
        return -1;
    }
    size_t line_len = 0;
	char * pch_line = buffer;
	char ch_prev = 0;
	bool eol = false;
    while(!eol) {
		char ch;
        ssize_t cb_read = read(fd, &ch, 1);
		if(cb_read>0) {
			if(line_len >= (buffer_len - 1)) {
				// line too long
				errno = EIO;
				return -1;
			}
			eol = (ch_prev=='\r' && ch=='\n');
			if(!eol) {
				*pch_line++ = ch;
				line_len += 1;
				ch_prev = ch;
			}
		} else if(cb_read==0) {
			// unexpected EOF
			errno = EIO;
			return -1;
		} else if(errno!=EINTR) {
			// error
			return -1;
		}
    }
	// add null terminator
	*(pch_line-1) = 0;
    return line_len-1;
}

int io_encode_hex(FILE * out, const unsigned char * bytes, size_t len) {
	int n = 0;
	for(size_t i=0; i<len; i++) {
		n += fprintf(out,"%02x",(unsigned int)(bytes[i]));
	}
	return n;
}

static const char *nibbles[16] = {
	"0000",
	"0001",
	"0010",
	"0011",
	"0100",
	"0101",
	"0110",
	"0111",
	"1000",
	"1001",
	"1010",
	"1011",
	"1100",
	"1101",
	"1110",
	"1111"
};

int io_encode_bin(FILE * out, const unsigned char * bytes, size_t len) {
	int n = 0;
	for(size_t i=0; i<len; i++) {
		unsigned char b = bytes[i];
		n += fprintf(out,"%s%s",nibbles[b>>4],nibbles[b&0xf]);
	}
	return n;
}

int io_encode_b64(FILE * out, const unsigned char * bytes, size_t len) {
	BIO *bio, *b64;
	b64 = BIO_new(BIO_f_base64());
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	bio = BIO_new_fp(out, BIO_NOCLOSE);
	BIO_push(b64, bio);
	int n = BIO_write(b64, bytes, len);
	BIO_flush(b64);
	BIO_free_all(b64);
	return n;
}

bool io_is_dir(const char * path) {
	struct stat s;
	errno = 0;
	if(stat(path,&s)<0) {
		dlogf("Can't stat path: %s: %s",strerror(errno),path);
		return false;
	}
	if(!S_ISDIR(s.st_mode)) {
		errno = ENOTDIR;
		return false;
	}
	return true;
}

#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"
#include "rnd.h"

#define TEST_DATA_DIR "src/test-data/"
static const char * words_file = TEST_DATA_DIR "words";

UT_TEST_CASE(io_copy_io_stream) {
	struct stat s;
	ut_assert(stat(words_file,&s)>=0);
	int in = open(words_file, O_RDONLY);
	int out = open("/dev/null", O_WRONLY);
	ut_assert(in>=0);
	ut_assert(out>=0);
	ut_assert(io_copy_stream(out,in,s.st_size)==s.st_size);
	close(in);
	close(out);
}

UT_TEST_CASE(io_encodings) {
	#define NUM_BYTES 64
	unsigned char * bytes = rnd_mem(NUM_BYTES, NULL);

	char * buff = NULL;
	size_t buff_len = 0;
	size_t exp_len = 0;
	FILE * out = open_memstream(&buff,&buff_len);
	ut_assert(out!=NULL);

	ut_assert(io_encode_bin(out,bytes,NUM_BYTES)>0);
	fflush(out);
	exp_len += NUM_BYTES*8;
	ut_assert(exp_len==buff_len);

	ut_assert(io_encode_hex(out,bytes,NUM_BYTES)>0);
	fflush(out);
	exp_len += NUM_BYTES*2;
	ut_assert(exp_len==buff_len);

	ut_assert(io_encode_b64(out,bytes,NUM_BYTES)==NUM_BYTES);
	fflush(out);
	exp_len += ((NUM_BYTES + 2) / 3) << 2;
	ut_assert(exp_len==buff_len);

	fclose(out);
	free(buff);
	free(bytes);
}

UT_TEST_CASE(io_is_dir) {
	ut_assert(io_is_dir("./src"));
	ut_assert(!io_is_dir("./Makefile"));
	ut_assert(!io_is_dir("./this-file-does-not-exist"));
}


#endif // !EXCLUDE_UNIT_TESTS
