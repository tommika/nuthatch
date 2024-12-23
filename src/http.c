// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <sys/stat.h>

#include "log.h"
#include "sz.h"
#include "io.h"
#include "http.h"
#include "ws.h"

#ifndef PATH_MAX
#warning "PATH_MAX is not defined, so setting it"
#define PATH_MAX 1024
#endif 

static char _static_files_dir[PATH_MAX+1]; // leave room for null term
static size_t _static_files_dir_len = 0;

#define HTTP_STATUS(STATUS,CODE,REASON) \
	enum { HTTP_##STATUS = CODE }; \
	const char * HTTP_##STATUS##_REASON = REASON;

// 2xx
HTTP_STATUS(OK,200,"OK");
HTTP_STATUS(CREATED,201,"Created");
HTTP_STATUS(ACCEPTED,202,"Accepted");
// 4xx
HTTP_STATUS(BAD_REQUEST,400,"Bad Request");
HTTP_STATUS(NOT_FOUND,404,"Not Found");
HTTP_STATUS(METHOD_NOT_ALLOWED,405,"Method Not Allowed");

char * realpath_uri(const char * uri) {
	int uri_len = strlen(uri);
	if(_static_files_dir_len + uri_len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	char icky_path[PATH_MAX];
	strcpy(icky_path,_static_files_dir);
	strcat(icky_path,uri);
	dlogf("icky_path=%s",icky_path);
	char * path = realpath(icky_path,NULL);
	dlogf("realpath=%s",path);
	#define icky_path DONT_USE_THIS // prevent further use of the icky variable
	if(path) {
		if(!sz_starts_with(path,_static_files_dir)) {
			// not cool!
			wlogf("uri resolved to a path outside of the static files dir");
			errno = EPERM;
			free(path);
			path = NULL;
		}
	}
	if(path != NULL) {
		errno = 0;
	}
	return path;
	#undef icky_path
}

static int http_method(const char * sz_method) {
	int method = M_UNKNOWN;
	if(sz_equal_ignore_case("GET",sz_method)) {
		method = M_GET;
	} else if(sz_equal_ignore_case("POST",sz_method)) {
		method = M_POST;
	} else if(sz_equal_ignore_case("PUT",sz_method)) {
		method = M_PUT;
	} else if(sz_equal_ignore_case("DELETE",sz_method)) {
		method = M_DELETE;
	}
	return method;
}

// Header field names, normalized to lower-case
const char * H_CONTENT_LENGTH = "content-length";
const char * H_EXPECT = "expect";
const char * H_CONNECTION = "connection";
const char * H_UPGRADE = "upgrade";

// Header values
const char * HV_EXPECT_100_CONTINUE = "100-continue";

//static const char * HTTP_SEPARATORS = "()<>@,;:\\\"/[]?={} \t";

static Hashtable parse_headers(int fd) {
	errno = 0;
	Hashtable headers = ht_create(0,NULL,free,NULL); // we don't free the value, since it will be allocated together with key
	char h_buff[MAX_HTTP_HEADER+1];
	ssize_t h_len;
	while((h_len = io_read_line_crlf(fd, h_buff, MAX_HTTP_HEADER)) > 0) {
		char * header = strndup(h_buff,h_len);
		// Does not support "folded" header lines
		// TODO: https://datatracker.ietf.org/doc/html/rfc7230#section-3.2.6
		char * name = strtok(header,":");
		char * val = strtok(NULL,"\n\r");
		if(!(name && val)) {
			wlogf("Skipping invalid header: %s",header);
			free(header);
		} else {
			// Header names are case insensitive
			sz_to_lower(name);
			// trim whitespace
			val = sz_trim(val);
			// Add to hashtable
			ht_put(headers,name,val);
		}
	}
	if(h_len<0) {
		wlogf("io_read_line_crlf failed: %s",strerror(errno));
		ht_free(headers);
		return NULL;
	}
	return headers;
}

static void free_headers(Http_Headers headers) {
    ht_free(headers);
}

static int dispatch_websocket(int fd_client_in, int fd_client_out, const Http_Headers headers, HTTP_Method method, const char * uri) {
	FILE * f_in = fdopen(fd_client_in,"r");
	if(f_in==NULL) {
		elogf("fopen failed for reading: %s",strerror(errno));
		return -1;
	}
	FILE * f_out = fdopen(fd_client_out,"w");
	if(f_out==NULL) {
		elogf("fopen failed for writing : %s",strerror(errno));
		return -1;
	}
	int ret_code = 0;
	Websocket ws = ws_upgrade(f_in,f_out,headers,uri,true);
	if(ws==NULL) {
		wlogf("Failed create websocket");
		ret_code = -1;
	} else {
		bool done=false;
		while(!done) {
			WS_Msg_Type type = ws_wait(ws);
			switch(type) {
			case WS_ERROR:
				ret_code = -1;
				done = true;
				break;
			case WS_CLOSE:
				done = true;
				ilogf("Remote client closed connection: status=%d",ws_status(ws));
				break;
			case WS_MSG_BIN:
			case WS_MSG_TXT: {
				size_t msg_len;
				const unsigned char * msg = ws_get_msg(ws, &msg_len);
				if(type==WS_MSG_TXT) {
					ilogf("WS_MSG_TXT: %.*s",msg_len,msg);
				}
				ws_send_msg(ws,type,msg, msg_len);
				} break;
			}
		}
		ws_free(ws);
	}
	return ret_code;
}

static int dispatch_http(int fd_in, int fd_out, const Http_Headers headers, HTTP_Method method, const char * uri) {
	FILE * fp_out = fdopen(dup(fd_out),"w");
	int req_content_len = 0;
	char * valT;
	if((valT=ht_get(headers,H_CONTENT_LENGTH))) {
		req_content_len = atoi(valT);
	}

	if((valT=ht_get(headers,H_EXPECT))) {
		if(sz_equal_ignore_case(valT,HV_EXPECT_100_CONTINUE)) {
			// REVIEW: We shouldn't send the HTTP 100 until we've checked all request headers
			ilogf("Sending HTTP continue");
			fprintf(fp_out,"HTTP/1.1 100 Continue\r\n\r\n");
			fflush(fp_out);
		}
	}

	char * req_body = NULL;

	int rsp_content_len = 0;
	int rsp_code = HTTP_OK;
	int rsp_fd = -1;
	size_t rsp_block_size = 0;
	const char * rsp_reason = NULL; 
	switch(method) {
	default:
		// Method not supported
		// TODO
		wlogf("Method not allowed: method=%d\n",method);
		rsp_code = HTTP_METHOD_NOT_ALLOWED;
		break;
	case M_POST:
	case M_PUT:
		if(req_content_len>0) {
			// Read request body
			ilogf("Reading request body: content-length=%d",req_content_len);
			req_body = malloc(req_content_len);
			int cb_total = 0;
			while(cb_total < req_content_len) {
				int cb_read = read(fd_in, req_body+cb_total, req_content_len-cb_total);
				if(cb_read<0) {
					wlogf("Error reading request body: %s",strerror(errno));
					free(req_body);
					// FIXME - what HTTP code to return
					rsp_code = HTTP_BAD_REQUEST;
					break;
				}
				if(cb_read==0) {
					// EOF
					// REVIEW: in this state, we have read fewer bytes
					// than is claimed in the request content-length	
					rsp_code = HTTP_BAD_REQUEST;
					break;
				}
				cb_total += cb_read;
			}
			ilogf("Done reading request body: actual_size=%d",cb_total);
		}
		if(rsp_code==HTTP_OK) {
			// TODO - dispatch POST/PUT
			rsp_code = HTTP_CREATED;
		}		
		break;
	case M_GET: {
		// GET
		if(strcmp(uri,"/")==0) {
			uri = "/index.html";
		}
		// Assume we can't find it
		rsp_code = HTTP_NOT_FOUND;
		rsp_reason = HTTP_NOT_FOUND_REASON;
		char * uri_path = realpath_uri(uri);
		if(!uri_path) {
			ilogf("Error resolving uri to path: %s",strerror(errno));
		} else {
			// Open the file and write it to the output stream
			struct stat uri_stat;
			if(stat(uri_path,&uri_stat)<0) {
				wlogf("Can't stat uri path: %s",strerror(errno));
			} else if(!S_ISREG(uri_stat.st_mode)) {
				ilogf("Must be a regular file: %s",strerror(errno));
			} else if((rsp_fd = open(uri_path,O_RDONLY))<0) {
				wlogf("Can't open file",strerror(errno));
			} else {
				// Set-up the response body.
				// This is used to write the body once the headers are written.
				rsp_code = HTTP_OK;
				rsp_reason = HTTP_OK_REASON;
				rsp_content_len = uri_stat.st_size;
				rsp_block_size = uri_stat.st_blksize;
			}
			free(uri_path);
		}
		break; }
	}

	// Response
	ilogf("HTTP response: status=%d %s",rsp_code,rsp_reason?rsp_reason:"");

	// Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
	fprintf(fp_out,"HTTP/1.1 %d %s\r\n",rsp_code,rsp_reason?rsp_reason:"");

	// Response headers
	// ...
	if(rsp_content_len>0) {
		fprintf(fp_out,"Content-Length: %d\r\n",rsp_content_len);
	}
	// Done with response headers
	fprintf(fp_out,"\r\n");
	fflush(fp_out);

	// Write response body
	if(rsp_fd>=0) {
		if(io_copy_stream(fd_out,rsp_fd,rsp_block_size)<0) {
			wlogf("Failed to copy file",strerror(errno));
		}
		close(rsp_fd);
		rsp_fd = -1;
	}

	if(req_body) {
		free(req_body);
	}

	fclose(fp_out);

	return rsp_code;
}


/*! \brief Initialize the http subsytem
 *
 * \param icky_files_dir Path to static files directory. This variable is icky
 *  because it needs to be normalized/sanitized before we use it.
 * 
 * \return Returns 0 if initialized successfully, non-zero if something went wrong.
 */	
int http_init(const char * icky_files_dir) {
	errno = 0;
	ilogf("Initializing http subsystem");
	// Get the canonical path of the given files directory
    if(!realpath(icky_files_dir, _static_files_dir)) {
		elogf("realpath failed: %s: %s", strerror(errno), icky_files_dir);
		return -1;
	}
	#define icky_files_dir DONT_USE_THIS // prevent further use of the icky variable
	if(strcmp("/",_static_files_dir)==0) {
		elogf("Files directory can't be root (sorry)");
		errno = EPERM;
		return -1;
	}
	_static_files_dir[PATH_MAX] = 0; // ensure that we're null terminated
	int l = strlen(_static_files_dir);
	if(_static_files_dir[l-1]=='/') { // removing trailing '/' if present
		// should never get here since realpath would have removed 
		// any trailing '/', but just in case
		_static_files_dir[l-1] = 0;
		l--;
	}
	_static_files_dir_len = l;
	if(_static_files_dir_len==0) {
		// should never get here since we already check for root folder,
		// but just in case
		errno = EPERM;
		return -1;
	}
	if(!io_is_dir(_static_files_dir)) {
		elogf("Static files path must be a directory: %s",_static_files_dir);
		errno = ENOTDIR;
		return -1;
	}
	ilogf("Using files from directory: %s",_static_files_dir);
	return 0;
	#undef icky_files_dir
}

/*! \brief Process a client request. Called when a client connects to the server.
 *
 * See: https://www.w3.org/Protocols/rfc2616/rfc2616.html
 * 
 */	
int http_client_connect(int fd_client_in, int fd_client_out) {
	// Read and parse request line
	char req_line[MAX_HTTP_REQ+1];
	ssize_t req_line_len;;
	if((req_line_len = io_read_line_crlf(fd_client_in,req_line,sizeof(req_line)))<0) {
		wlogf("Failed reading request line: %s",strerror(errno));
		return HTTP_BAD_REQUEST;
	}

	// Request-Line = Method SP Request-URI SP HTTP-Version CRLF
	char * sz_method = strtok(req_line," ");
	char * uri = strtok(NULL," ");
	char * version = strtok(NULL," ");
	if(!(sz_method && uri && version)) {
		ilogf("Invalid request line: %s",req_line);
		return HTTP_BAD_REQUEST;
	}
	int v_maj, v_min;
	if(2!=sscanf(version,"HTTP/%d.%d",&v_maj,&v_min)) {
		ilogf("Invalid HTTP version: %s",version);
		return HTTP_BAD_REQUEST;
	}
	int method = http_method(sz_method);
	if(!method) {
		ilogf("Invalid HTTP method: %s",sz_method);
		return HTTP_METHOD_NOT_ALLOWED;
	}
	ilogf("HTTP request: method=%s(%d) version=%d.%d uri=%s",sz_method,method,v_maj,v_min,uri);

	int ret_code = 0;

	// Read and parse request headers
	Http_Headers headers = parse_headers(fd_client_in);
	if(!headers) {
		ilogf("Failed to parse headers");
		ret_code = HTTP_BAD_REQUEST;
	} else {
		if(logging(LEVEL_DEBUG)) {
			dlogf("Headers:");
			ht_dump(headers,stdlog,ht_val_print_sz);
			ht_stats(headers,stdlog);
		}
		if(ws_is_upgradable(headers)) {
			ret_code = dispatch_websocket(fd_client_in, fd_client_out, headers, method, uri);
		} else {
			ret_code = dispatch_http(fd_client_in, fd_client_out, headers, method, uri);
		}
		free_headers(headers);
	}
	ilogf("ret_code=%d",ret_code);
	return ret_code;
}

#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"
#include "rnd.h"

#define TEST_DATA_DIR "src/test-data/"

UT_TEST_CASE(http_init) {
	ut_assert(http_init("/bogus/path")!=0);
	ut_assert(errno == ENOENT);
	ut_assert(http_init("/")!=0);
	ut_assert(errno == EPERM);
	ut_assert(http_init("/usr/..")!=0);
	ut_assert(errno == EPERM);
	ut_assert(http_init("./web/index.html")!=0);
	ut_assert(errno == ENOTDIR);

	ut_assert(http_init("./web")==0);
	ut_assert(errno == 0);
	ut_assert(http_init("./web////")==0);
	ut_assert(errno == 0);
}

UT_TEST_CASE(http_realpath_uri) {
	ut_assert(http_init("./web/")==0);

	char * big_uri = rnd_sz(PATH_MAX,NULL);
	ut_assert(realpath_uri(big_uri)==NULL);
	ut_assert(errno==ENAMETOOLONG);
	free(big_uri);

	ut_assert(realpath_uri("/../..")==NULL);
	ut_assert(errno == EPERM);

	ut_assert(realpath_uri("bogus/path")==NULL);
	ut_assert(errno==ENOENT);

	char * path = realpath_uri("/index.html");
	ut_assert(path!=NULL);
	ut_assert(errno==0);
	free(path);
}

static const char * test_headers_file = TEST_DATA_DIR "http-headers.txt";

UT_TEST_CASE(http_parse_headers) {
	ilogf("Reading test headers file: %s",test_headers_file);
	int fd = open(test_headers_file, O_RDONLY);
	ut_assert(fd>=0);
	Http_Headers headers = parse_headers(fd);
	close(fd);
	ut_assert(headers!=NULL);
	dlogf("Headers:");
	ht_dump(headers,stdlog,ht_val_print_sz);
	ht_stats(headers,stdlog);
	ut_assert(ht_size(headers)==3);
	ut_assert(strcmp("2112",ht_get(headers,"content-length"))==0);
	ut_assert(strcmp("NoOptionalWhiteSpace",ht_get(headers,"header-no-ows"))==0);
	ut_assert(strcmp("OptionalWhiteSpace",ht_get(headers,"header-ows"))==0);
	ut_assert(!ht_contains(headers,"ignored-1"));
	ut_assert(!ht_contains(headers,"ignored-2"));
	free_headers(headers);
}

UT_TEST_CASE(http_method) {
	ut_assert(http_method("Get")==M_GET);
	ut_assert(http_method("Post")==M_POST);
	ut_assert(http_method("Put")==M_PUT);
	ut_assert(http_method("Delete")==M_DELETE);
	ut_assert(http_method("Fred")==M_UNKNOWN);
}

typedef struct _http_req_testcase {
	const char * filename;
	int expected_status;
} HTTPRequestTestCase;


HTTPRequestTestCase req_testcases[] = { 
    {TEST_DATA_DIR "GET-200.txt", HTTP_OK},
    {TEST_DATA_DIR "GET-400-bad-request-1.txt", HTTP_BAD_REQUEST},
    {TEST_DATA_DIR "GET-400-bad-request-2.txt", HTTP_BAD_REQUEST},
    {TEST_DATA_DIR "GET-400-bad-request-3.txt", HTTP_BAD_REQUEST},
    {TEST_DATA_DIR "GET-404-not-found.txt", HTTP_NOT_FOUND},
    {TEST_DATA_DIR "POST-201.txt", HTTP_CREATED},
    {TEST_DATA_DIR "POST-400.txt", HTTP_BAD_REQUEST},
    {TEST_DATA_DIR "BOGUS-405-method-not-allowed.txt", HTTP_METHOD_NOT_ALLOWED},
	{TEST_DATA_DIR "POST-ws.bin", 0},
};

int test_http_req(HTTPRequestTestCase * testcase) {
	ut_assert(http_init("./web")==0);
	ilogf("Reading request file: %s",testcase->filename);
	int fd_in = open(testcase->filename, O_RDONLY);
	ut_assert(fd_in>=0);
	int fd_out = open("/dev/null", O_RDWR);
	ut_assert(fd_out>=0);
	int status = http_client_connect(fd_in, fd_out);
	close(fd_in);
	close(fd_out);
	return status == testcase->expected_status;
}

const int num_req_testcases = sizeof(req_testcases)/sizeof(HTTPRequestTestCase);

UT_TEST_CASE(http_request) {
	for (int i=0; i<num_req_testcases; i++) {
		ut_assert(test_http_req(&req_testcases[i]));
	}
}

UT_TEST_CASE(http_dispatch_misc) {
	int fd_in = open("/dev/random", O_RDWR);
	int fd_out = open("/dev/null", O_RDWR);
	ut_assert(fd_in>=0);
	ut_assert(fd_out>=0);
	Http_Headers headers = headers = ht_create(0,NULL,free,NULL);
	ut_assert(dispatch_http(fd_in,fd_out,headers,M_TRACE,"/")==HTTP_METHOD_NOT_ALLOWED);
	free_headers(headers);
	close(fd_in);
	close(fd_out);
}

#endif // !EXCLUDE_UNIT_TESTS

