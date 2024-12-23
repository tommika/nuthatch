// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <openssl/sha.h>
#include <string.h>
#include <stdint.h>

#include "endian.h"

#include "log.h"
#include "ht.h"
#include "http.h"
#include "ws.h"
#include "sz.h"
#include "io.h"
#include "math.h"
#include "mem.h"

// https://tools.ietf.org/html/rfc6455

// Header names
static const char * H_SEC_WEBSOCKET_KEY    = "sec-websocket-key";
static const char * H_SEC_WEBSOCKET_EXT    = "sec-websocket-extensions";
static const char * H_SEC_WEBSOCKET_ACCEPT = "sec-websocket-accept";

// Other constants
static const char * WS_UPGRADE = "websocket";
static const char * WS_MAGIC   = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

typedef enum {
	OC_CONT  = 0x0,
	OC_TEXT  = 0x1,
	OC_BIN   = 0x2,
	OC_CLOSE = 0x8,
	OC_PING  = 0x9,
	OC_PONG  = 0xA,
} Opcode_Type;

// Internal representation of a Data Frame
typedef struct Data_Frame_S {
	Opcode_Type   opcode;     // see Opcode_Type
	bool          fin;        // true if final fragment of message
	uint64_t      len;        // payload length
	uint64_t      size;       // currently allocated size of this Data_Frame
	unsigned char payload[0]; // payload data
 } * Data_Frame;

// Wire representation of Data Frame header (rfc6455)
struct Data_Frame_Header_S {
	// First byte
	unsigned char opcode:4;
	unsigned char rsv3:1;
	unsigned char rsv2:1;
	unsigned char rsv1:1;
	bool          fin:1;
	// Second byte
	unsigned char len:7;
	unsigned char mask:1;
};

// Allocate (or re-allocate) a Data Frame
static Data_Frame alloc_dataframe(char opcode, bool fin, uint64_t len, Data_Frame df) {
	uint64_t size = sizeof(struct Data_Frame_S) + len;
	if(df==NULL) {
		df = malloc(size);
		df->size = size;
	} else if(df->size < size) {
		df = realloc(df, size);
		df->size = size;
	}
	df->opcode = opcode;
	df->fin = fin;
	df->len = len;
	return df;
}

static void free_dataframe(Data_Frame df) {
	free(df);
}
/*! \brief Read a Websocket data frame
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-------+-+-------------+-------------------------------+
 *    |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 *    |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 *    |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 *    | |1|2|3|       |K|             |                               |
 *    +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 *    |     Extended payload length continued, if payload len == 127  |
 *    + - - - - - - - - - - - - - - - +-------------------------------+
 *    |                               |Masking-key, if MASK set to 1  |
 *    +-------------------------------+-------------------------------+
 *    | Masking-key (continued)       |          Payload Data         |
 *    +-------------------------------- - - - - - - - - - - - - - - - +
 *    :                     Payload Data continued ...                :
 *    + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 *    |                     Payload Data continued ...                |
 *    +---------------------------------------------------------------+
 *
 *
 * \param df If non-null, ownership of the given dataframe is transferred to this function.
 * If the dataframe is read successfully, ownership of a dataframe is given back to the caller
 * via return value. There is no guarantee that the same dataframe instance is returned
 * (e.g., the original dataframe instance may have been re-allocated.)
 * \return If successful, a dataframe is returned. Ownership of the dataframe is transferred
 * to the caller. If an error occurs, NULL is returned.
 *
 */
static Data_Frame read_dataframe(FILE * f, bool require_masked, Data_Frame df) {
	struct Data_Frame_Header_S dfh;
	// (1) Read data frame header
	if(fread(&dfh, sizeof(dfh), 1, f)!=1) {
		wlogf("Failed to read data frame header");
		goto on_error;
	}
	dlogf("Received websocket data frame header: fin=%d, opcode=0x%x, mask=%d, len=%d",dfh.fin,dfh.opcode,dfh.mask,dfh.len);
	if(!dfh.mask && require_masked) {
		wlogf("Unexpected mask bit in data frame header");
		goto on_error;
	}
	uint64_t len64;
	if(dfh.len==127) {
		// 64-bit extended payload length
		// (2) Read extended payload length
		if(fread(&len64,sizeof(len64),1,f)!=1) {
			wlogf("Failed to read 64-bit payload length");
			goto on_error;
		}
		len64 = be64toh(len64);
		if(len64 & ((uint64_t)1<<63)) {
			// high-order bit must be zero
			wlogf("Expected 64-bit payload length most significant bit to be zero");
			goto on_error;
		}
	} else if(dfh.len == 126) {
		// 16-bit extended payload length
		uint16_t len16;
		// (2) Read extended payload length
		if(fread(&len16,sizeof(len16),1,f)!=1) {
			wlogf("Failed to read 16-bit payload length");
			goto on_error;
		}
		len64 = be16toh(len16);
	} else {
		len64 = dfh.len;
	}
	
	dlogf("Websocket payload len=%llu",len64);
	unsigned char mask_key[4];
	if(dfh.mask) {
		// (3) Read mask
		if(fread(&mask_key,sizeof(mask_key),1,f)!=1) {
			wlogf("Failed to read mask key");
			goto on_error;
		}
		if(logging(LEVEL_DEBUG)) {
			dlogf("mask_key:");
			io_encode_hex(stdlog,mask_key,4);
			fprintf(stdlog,"\n");
		}
	}
	df = alloc_dataframe(dfh.opcode,dfh.fin,len64,df);
	// (4) Read payload
	if(len64>0) {
		if(fread(df->payload,len64,1,f)!=1) {
			wlogf("Failed to read payload");
			goto on_error;
		}
		if(dfh.mask) {
			if(logging(LEVEL_DEBUG)) {
				dlogf("Payload before unmasking:");
				io_encode_hex(stdlog,df->payload,min(32,df->len));
				fprintf(stdlog,"\n");
			}
			// Unmask the payload
			for(uint64_t i=0;i<df->len;i++) {
				df->payload[i] = df->payload[i] ^ mask_key[i%4];
			}
		}
	}
	if(logging(LEVEL_DEBUG) && df->len>0) {
		dlogf("Payload:");
		io_encode_hex(stdlog,df->payload,min(32,df->len));
		fprintf(stdlog,"\n");
	}
	ilogf("Received dataframe: opcode=0x%x, len=%llu", df->opcode, df->len);
	return df;

on_error:
	if(df) {
		free(df);
	}
	return NULL;

}

static bool write_dataframe(FILE * f, const Data_Frame df, unsigned char * mask_key) {
	ilogf("Sending dataframe: opcode=0x%x, len=%llu", df->opcode, df->len);

	struct Data_Frame_Header_S dfh;
	dfh.opcode = df->opcode;
	dfh.rsv1 = dfh.rsv2 = dfh.rsv3 = 0;
	dfh.fin = df->fin;
	dfh.mask = mask_key==NULL ? 0 : 1;
	if(df->len<=125) {
		// 7-bit length payload
		dfh.len = (unsigned char)df->len;
		// (1) Write data frame header
		if(fwrite(&dfh, sizeof(dfh), 1, f)!=1) {
			wlogf("Failed to write data frame header: %s",strerror(errno));
			return false;
		}
	} else if(df->len <= 0xffff) {
		// 16-bit length payload
		uint16_t len16 = htobe16((uint16_t)df->len);
		dfh.len = 126;
		// (1) Write data frame header
		if(fwrite(&dfh, sizeof(dfh), 1, f)!=1) {
			wlogf("Failed to write data frame header: %s",strerror(errno));
			return false;
		}
		// (2) Write extended payload length
		if(fwrite(&len16, sizeof(len16), 1, f)!=1) {
			wlogf("Failed to payload length: %s",strerror(errno));
			return false;
		}
	} else {
		// 64-bit length payload
		uint64_t len64 = htobe64((uint64_t)df->len);
		dfh.len = 127;
		// (1) Write data frame header
		if(fwrite(&dfh, sizeof(dfh), 1, f)!=1) {
			wlogf("Failed to write data frame header: %s",strerror(errno));
			return false;
		}
		// (2) Write extended payload length
		if(fwrite(&len64, sizeof(len64), 1, f)!=1) {
			wlogf("Failed to write payload length: %s",strerror(errno));
			return false;
		}
	}
	dlogf("Sent websocket data frame header: fin=%d, opcode=0x%x, mask=%d, len=%d",dfh.fin,dfh.opcode,dfh.mask,dfh.len);

	if(mask_key) {
		// (3) write mask
		if(logging(LEVEL_DEBUG)) {
			dlogf("mask_key:");
			io_encode_hex(stdlog,mask_key,4);
			fprintf(stdlog,"\n");
		}
		if(fwrite(mask_key,4,1,f)!=1) {
			wlogf("Failed to write mask key: %s",strerror(errno));
			return false;
		}

	}
	if(df->len > 0) {
		if(logging(LEVEL_DEBUG)) {
			dlogf("Payload:");
			io_encode_hex(stdlog,df->payload,min(32,df->len));
			fprintf(stdlog,"\n");
		}
		// Mask the payload
		if(mask_key) {
			for(uint64_t i=0;i<df->len;i++) {
				df->payload[i] = df->payload[i] ^ mask_key[i%4];
			}
			if(logging(LEVEL_DEBUG)) {
				dlogf("Payload after masking:");
				io_encode_hex(stdlog,df->payload,min(32,df->len));
				fprintf(stdlog,"\n");
			}
		}
		if(fwrite(df->payload,df->len,1,f)!=1) {
			wlogf("Failed to write payload: %s",strerror(errno));
			return false;
		}
	}
	fflush(f);

	return true;
}

/*! \brief Read a data frame from the given file. 
 *
 *  \param f The file from which to read the data frame
 *  \param masked If true, the payload must be masked; if false, the payload must not be masked.
 *  \param df Pointer to existing data frame, or null. 
 *            If NULL, a new data frame will be returned.
 *            Otherwise, the given data frame will be reused if there is sufficient room for the payload,
 *            and reallocated if not.
 *   \return Returns point to the data frame, or NULL if something bad happened.
 */

bool _ws_handshake(
		FILE * f_out, 
		const Http_Headers headers) {
	ilogf("performing websocket handshake");
	if(!sz_equal_ignore_case(WS_UPGRADE,ht_get(headers,H_UPGRADE))) {
		wlogf("not a websocket request");
		return false;
	}
	const char * ws_key = ht_get(headers,H_SEC_WEBSOCKET_KEY);
	const char * ws_ext = ht_get(headers,H_SEC_WEBSOCKET_EXT);
	if(!ws_key) {
		wlogf("websocket security key not found in headers");
		return false;
	}
	dlogf("ws_ext: %s", ws_ext?ws_ext:"<NULL>");
	ilogf("switching protocols");
	fprintf(f_out,"HTTP/1.1 101 Switching Protocols\r\n");
	fprintf(f_out,"%s: %s\r\n",H_CONNECTION,H_UPGRADE);
	fprintf(f_out,"%s: %s\r\n",H_UPGRADE,WS_UPGRADE);
	fprintf(f_out,"%s: ",H_SEC_WEBSOCKET_ACCEPT);
	char * ws_accept = sz_cat(ws_key,WS_MAGIC);
	dlogf("ws_accept: %s",ws_accept);
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((unsigned char *)ws_accept, strlen(ws_accept), hash);
	free(ws_accept);
	if(logging(LEVEL_DEBUG)) {
		dlogf("hash: ");
		io_encode_hex(stdlog,hash,SHA_DIGEST_LENGTH);
		fprintf(stdlog,"\n");
	}
	io_encode_b64(f_out,hash,SHA_DIGEST_LENGTH);
	if(logging(LEVEL_DEBUG)) {
		dlogf("base64:");
		io_encode_b64(stdlog, hash, SHA_DIGEST_LENGTH);
		fprintf(stdlog,"\n");
	}
	fprintf(f_out,"\r\n\r\n");
	fflush(f_out);
	return true;
}

struct Websocket_S {
	int fd_client;
	FILE * f_in;
	FILE * f_out;
	bool is_masked_client;
	Data_Frame df;
	unsigned char * buff;
	size_t buff_len;
	uint16_t status_code; // reason for closure: see https://tools.ietf.org/html/rfc6455#section-7.4.1
	uint16_t ping_recv_count;
	uint16_t ping_sent_count;
	uint16_t pong_recv_count;
};

static Websocket _ws_create(
		FILE * f_in, FILE * f_out, 
		bool masked_client) {

	// Allocate inital data frame, and send a PING
	Data_Frame df = alloc_dataframe(OC_PING,true,0,NULL);
	if(!write_dataframe(f_out,df,NULL)) {
		free_dataframe(df);
		return NULL;
	}

	Websocket ws = malloc(sizeof(struct Websocket_S));
	ws->f_in = f_in;
	ws->f_out = f_out;
	ws->df = df;
	ws->status_code = 0;
	ws->buff = NULL;
	ws->buff_len = 0;
	ws->is_masked_client = masked_client;
	// zero-out stats
	ws->ping_recv_count = ws->pong_recv_count = 0;
	return ws;
}

/* Read a message from the remote endpoint */ 
static char _ws_read(Websocket ws) {	
	char opcode_prev = -1;
	for/*ever*/(;;) {
		Data_Frame df = ws->df = read_dataframe(ws->f_in,ws->is_masked_client,ws->df);
		if(df==NULL) {
			ilogf("Failed to read data frame");
			return WS_ERROR;
		}
		char opcode = df->opcode;
		if(opcode==OC_CONT) {
			opcode = opcode_prev;
		} else {
			ws->buff_len = 0;
		}
		switch(opcode) {
		default:
		// shouldn't get here
			wlogf("Unexpected opcode: 0x%x",opcode);
			return -1;
		// Control Frames
		case OC_PING:
			ilogf("Received OC_PING; sending OC_PONG");
			ws->ping_recv_count++;
			df->opcode = OC_PONG;
			write_dataframe(ws->f_out,df,NULL);
			break;
		case OC_PONG:
			ilogf("Received OC_PONG");
			ws->pong_recv_count++;
			break;
		case OC_CLOSE: {
			// Close status codes: https://tools.ietf.org/html/rfc6455#section-7.4.1			
			uint16_t status_code = 0;
			if(df->len >= 2) {
				memcpy(&status_code,df->payload,sizeof(status_code));
			}
			status_code = be16toh(status_code);
			ilogf("Received OC_CLOSE: status_code=%u",status_code);
			ws->status_code = status_code;
			return OC_CLOSE;
			} 
			break;
		// Message frames
		case OC_TEXT:
		case OC_BIN:
			ws->buff = mem_append(ws->buff,ws->buff_len,ws->df->payload,ws->df->len,&ws->buff_len);
			if(df->fin) {
				return opcode;
			}
			break;
		}
		opcode_prev = df->fin ? -1 : opcode;
	}
}

bool _ws_send_close(Websocket ws, uint16_t status_code) {
	Data_Frame df = alloc_dataframe(OC_CLOSE,true,2,NULL);
	if(!df) {
		return false;
	}
	status_code = htobe16(status_code);
	memcpy(df->payload,&status_code,sizeof(status_code));
	bool ok = write_dataframe(ws->f_out,df,NULL);
	free_dataframe(df);
	return ok;
}

bool _ws_send_msg(Websocket ws, WS_Msg_Type type, const unsigned char * msg, size_t msg_len) {
	Data_Frame df = alloc_dataframe(type==WS_MSG_TXT?OC_TEXT:OC_BIN,true,msg_len,NULL);
	if(!df) {
		return false;
	}
	memcpy(df->payload,msg,msg_len);
	bool ok = write_dataframe(ws->f_out,df,NULL);
	free_dataframe(df);
	return ok;
}

// PUBLIC interface

bool ws_is_upgradable(const Http_Headers headers) {
	char * valT;
	// REVIEW:
	// We should be looking for a `connection` header with value 'Upgrade'.
	// However, this will not work (currently) if there are multiple values associated
	// with the `connection` header. E.g., Firefox sends "connection:keep-alive, Upgrade"
	// To resolve this, we need to parse comma-separated values into multiple values
	// as per HTTP spec. 
	// The quick fix (as done here) is to simply remove the check for "Upgrade" in
	// the `connection` header value, and just look for an `upgrade:websocket` header.
    return 
	    // (valT=ht_get(headers,H_CONNECTION)) &&
        // sz_equal_ignore_case(valT,H_UPGRADE) &&
        (valT=ht_get(headers,H_UPGRADE)) &&
        sz_equal_ignore_case(valT,WS_UPGRADE);
}

Websocket ws_upgrade(FILE * f_in, FILE * f_out, const Http_Headers headers, const char * uri, bool masked_client) {
	if(!_ws_handshake(f_out,headers)) {
		wlogf("not a websocket connection");
		return NULL;
	}
	return _ws_create(f_in,f_out, masked_client);
}

bool ws_is_open(Websocket ws) {
	return ws->f_in!=NULL && ws->f_out!=NULL;
}

void ws_close(Websocket ws, WS_Status_Code code) {
	if(!ws->f_out) {
		wlogf("websocket already closed");
		return;
	}
	_ws_send_close(ws,code);

	// It's possible for f_in and f_out to be the same object,
	// in which case we want to close it only once, after flushing it.
	if(ws->f_in && (ws->f_in != ws->f_out)) {
		fclose(ws->f_in);
	}
	if(ws->f_out) {
		fflush(ws->f_out);
		fclose(ws->f_out);
	}
	ws->f_in = ws->f_out = NULL;
}


bool ws_send_msg(Websocket ws, WS_Msg_Type type, const unsigned char * msg, size_t msg_len) {
	return _ws_send_msg(ws, type, msg, msg_len);
}

void ws_free(Websocket ws) {
	ws_close(ws,WS_STATUS_GOING_AWAY);
	if(ws->df) {
		free_dataframe(ws->df);
		ws->df = NULL;
	}
	if(ws->buff) {
		free(ws->buff);
		ws->buff_len = 0;
	}
	free(ws);
}

WS_Msg_Type ws_wait(Websocket ws) {
	char oc = _ws_read(ws);
	switch(oc) {
	default:
		return WS_ERROR;
	case OC_CLOSE:
		return WS_CLOSE;
	case OC_BIN:
		return WS_MSG_BIN;
	case OC_TEXT:
		return WS_MSG_TXT;
	}
}

const unsigned char * ws_get_msg(Websocket ws, size_t * msg_len) {
	if(msg_len!=NULL) {
		*msg_len = ws->buff_len;
	}
	return ws->buff;
}

WS_Status_Code ws_status(Websocket ws) {
	return ws->status_code;
}

#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"
#include "rnd.h"

UT_TEST_CASE(ws_is_upgradable) {
	Http_Headers headers = ht_create(0,NULL,NULL,NULL);
	
	ht_put(headers,(char*)H_CONNECTION,(char*)H_UPGRADE);
	ut_assert(!ws_is_upgradable(headers));

	ht_put(headers,(char*)H_UPGRADE,(char*)WS_UPGRADE);
	ut_assert(ws_is_upgradable(headers));

	ht_free(headers);
}

UT_TEST_CASE(ws_dataframe_io_round_trip) {
	char * buff = NULL;
	size_t buff_len = 0;
	FILE * out = open_memstream(&buff,&buff_len);
	ut_assert(out!=NULL);
	
	unsigned char mask_key[4] = {2,1,1,2};

	char bin_payload[8] = {0,1,2,3,4,5,6,7};
	const size_t bin_payload_len = sizeof(bin_payload);

	const size_t payload_1_len = 0x07D;
	char * payload_1 = rnd_sz(payload_1_len,NULL);

	const size_t payload_2_len = 0xFF;
	char * payload_2 =  rnd_sz(payload_2_len,NULL);

	const size_t payload_3_len = 0x10000;
	char * payload_3 = rnd_sz(payload_3_len,NULL);

	// Write data frames to the buffer
	Data_Frame df_out = NULL;
	
	df_out = alloc_dataframe(OC_PONG,true,0,df_out);
	write_dataframe(out, df_out, mask_key);

	df_out = alloc_dataframe(OC_BIN,true,bin_payload_len,df_out);
	memcpy(df_out->payload,bin_payload,bin_payload_len);
	write_dataframe(out, df_out, mask_key);

	// fits in default payload size
	df_out = alloc_dataframe(OC_TEXT,false,payload_1_len,df_out);
	memcpy(df_out->payload,payload_1,payload_1_len);
	write_dataframe(out, df_out, mask_key);

	// requires extended payload size (16 bit)
	df_out = alloc_dataframe(OC_CONT,false,payload_2_len,df_out);
	memcpy(df_out->payload,payload_2,payload_2_len);
	write_dataframe(out, df_out, mask_key);

	// requires extended payload size (64 bit)
	df_out = alloc_dataframe(OC_CONT,true,payload_3_len,df_out);
	memcpy(df_out->payload,payload_3,payload_3_len);
	write_dataframe(out, df_out, mask_key);

	df_out = alloc_dataframe(OC_PING,true,0,df_out);
	write_dataframe(out, df_out, mask_key);

	WS_Status_Code status_code = WS_STATUS_NORMAL;
	df_out = alloc_dataframe(OC_CLOSE,true,2,df_out);
	*(uint16_t* )(df_out->payload) = htobe16(status_code);
	write_dataframe(out, df_out, mask_key);

	free_dataframe(df_out);
	fclose(out);

	// Save data frames to file. This can be used to create
	// test input files (e.g., in the case of test-data/POST-ws.bin)
	FILE * ft = fopen("build/data-frames.out","w");
	fwrite(buff, buff_len, 1, ft);
	fclose(ft);

	// Read data frames from the buffer
	FILE * in = fmemopen(buff, buff_len,"r");
	Data_Frame df_in = NULL;

	df_in = read_dataframe(in,true,df_in);
	ut_assert(df_in->opcode==OC_PONG);
	ut_assert(df_in->len==0);

	df_in = read_dataframe(in,true,df_in);
	ut_assert(df_in->opcode==OC_BIN);
	ut_assert(df_in->len==bin_payload_len);
	ut_assert(df_in->fin==true);
	ut_assert(memcmp(df_in->payload,bin_payload,bin_payload_len)==0);

	df_in = read_dataframe(in,true,df_in);
	ut_assert(df_in->opcode==OC_TEXT);
	ut_assert(df_in->len==payload_1_len);
	ut_assert(df_in->fin==false);
	ut_assert(memcmp(df_in->payload,payload_1,payload_1_len)==0);

	df_in = read_dataframe(in,true,df_in);
	ut_assert(df_in->opcode==OC_CONT);
	ut_assert(df_in->len==payload_2_len);
	ut_assert(df_in->fin==false);
	ut_assert(memcmp(df_in->payload,payload_2,payload_2_len)==0);

	df_in = read_dataframe(in,true,df_in);
	ut_assert(df_in->opcode==OC_CONT);
	ut_assert(df_in->len==payload_3_len);
	ut_assert(df_in->fin==true);
	ut_assert(memcmp(df_in->payload,payload_3,payload_2_len)==0);

	df_in = read_dataframe(in,true,df_in);
	ut_assert(df_in->opcode==OC_PING);
	ut_assert(df_in->len==0);

	df_in = read_dataframe(in,true,df_in);
	ut_assert(df_in->opcode==OC_CLOSE);
	ut_assert(df_in->len==2);
	ut_assert(df_in->fin==true);
	uint16_t t = be16toh(*(uint16_t* )df_in->payload);
	ut_assert(status_code==t);
	free_dataframe(df_in);
	fclose(in);

	// Combine all the OC_TEXT payloads into a single, contiguous byte array
	void * orig_msg = malloc(1);
	size_t orig_msg_len = 0;
	orig_msg = mem_append(orig_msg,orig_msg_len,payload_1,payload_1_len,&orig_msg_len);
	orig_msg = mem_append(orig_msg,orig_msg_len,payload_2,payload_2_len,&orig_msg_len);
	orig_msg = mem_append(orig_msg,orig_msg_len,payload_3,payload_3_len,&orig_msg_len);
	ut_assert(orig_msg_len == (payload_1_len+payload_2_len+payload_3_len));
	free(payload_1);
	free(payload_2);
	free(payload_3);

	// can now take this buffer and run it through _ws_connection
	in = fmemopen(buff, buff_len,"r");
	out = fopen("/dev/null","w");

	// Create websocket request
	Http_Headers headers = ht_create(0,NULL,NULL,NULL);
	ht_put(headers,(char*)H_UPGRADE,(char*)WS_UPGRADE);
	ht_put(headers,(char*)H_SEC_WEBSOCKET_KEY,(char*)"ThisIsTheKey");
	Websocket ws = ws_upgrade(in,out,headers,"/ws",false);
	ut_assert(ws);
	ut_assert(ws_is_open(ws));
	ut_assert(ws_wait(ws)==WS_MSG_BIN);
	ut_assert(ws_wait(ws)==WS_MSG_TXT);
	size_t msg_len;
	const unsigned char * msg = ws_get_msg(ws,&msg_len);
	ilogf("org_msg_len=%zu, msg_len=%zu",orig_msg_len,msg_len);
	ut_assert(msg_len == orig_msg_len);
	ut_assert(memcmp(orig_msg,msg,msg_len)==0);

	ut_assert(ws_wait(ws)==WS_CLOSE);
	ut_assert(ws_status(ws)==status_code);
	ut_assert(ws->pong_recv_count==1);
	ut_assert(ws->ping_recv_count==1);
	ws_free(ws);
	
	ht_free(headers);
	free(orig_msg);
	free(buff);
}


UT_TEST_CASE(ws_dataframe_mask_required) {
	char * buff = NULL;
	size_t buff_len = 0;
	FILE * out = open_memstream(&buff,&buff_len);
	ut_assert(out!=NULL);

	unsigned char mask_key[4] = {2,1,1,2};

	Data_Frame df = alloc_dataframe(OC_PING,true,0,NULL);
	ut_assert(write_dataframe(out, df, mask_key));
	ut_assert(write_dataframe(out, df, NULL));
	fclose(out);

	FILE * in = fmemopen(buff, buff_len, "r");
	ut_assert(in!=NULL);
	df = read_dataframe(in,true,df);
	ut_assert(df!=NULL);
	df = read_dataframe(in,true,df);
	ut_assert(df==NULL);
	fclose(in);
	free(buff);
}

UT_TEST_CASE(ws_not_upgradable) {
	Http_Headers headers = ht_create(0,NULL,NULL,NULL);
	Websocket ws = ws_upgrade(stdin,stdout,headers,"/ws",false);
	ut_assert(ws==NULL);
	ht_free(headers);
}

UT_TEST_CASE(ws_already_closed) {
	Http_Headers headers = ht_create(0,NULL,NULL,NULL);
	ht_put(headers,(char*)H_UPGRADE,(char*)WS_UPGRADE);
	ht_put(headers,(char*)H_SEC_WEBSOCKET_KEY,(char*)"ThisIsTheKey");
	FILE * in = fopen("/dev/random", "r");
	FILE * out = fopen("/dev/null", "w");
	Websocket ws = ws_upgrade(in,out,headers,"/ws",false);
	ut_assert(ws!=NULL);
	ws_close(ws,WS_STATUS_NORMAL);
	ws_close(ws,WS_STATUS_NORMAL);
	ws_free(ws);
	ht_free(headers);
}

#endif // !EXCLUDE_UNIT_TESTS
