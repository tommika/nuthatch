// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <signal.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <openssl/crypto.h>

#include "sz.h"
#include "io.h"
#include "log.h"
#include "net.h"
#include "ht.h"
#include "http.h"
#include "ws.h"

static volatile int shutdown_server = 0;

static void sigint_handler(int sig) {
	ilogf("Received signal: pid=%d, sig=%d",getpid(),sig);
	switch(sig) {
	default:
		elogf("Unexpected signal: %d",sig);
		break;
	case SIGINT:  // sig 2
	case SIGTERM: // sig 15
		ilogf("Received shutdown signal=%d",sig);
		shutdown_server = 1;
		break;
	case SIGCHLD:
		ilogf("A child process has termninated");
		break;
	}
}

static int _fd_client = 0;
static void sigint_handler_child(int sig) {
	ilogf("Child received signal: pid=%d, sig=%d",getpid(),sig);
	switch(sig) {
	default:
		elogf("Unexpected signal: %d",sig);
		break;
	case SIGINT:
	case SIGTERM:
		ilogf("Child received shutdown signal=%d",sig);
		if(_fd_client) {
			close(_fd_client);
		}
		shutdown_server = 1;
		break;
	}
}

static void do_server_maintenance() {
	int status;
	int pid;
	while((pid=waitpid(-1,&status,WNOHANG))>0) {
		ilogf("Child pid=%d terminated with status=0x%x", pid, status);
	}
}


static int server(bool use_fork, int port, const char * static_files_dir) {
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
	signal(SIGCHLD, sigint_handler);

	if(http_init(static_files_dir)!=0) { // TODO - get this from config
		elogf("Failed to initialize http subsystem");
		return 1;
	};

	ilogf("Starting server on port %d",port);

	int fd_server;
	if((fd_server = socket(AF_INET,SOCK_STREAM,0))<0) {
		elogf("Failed to create server socket: %s",strerror(errno));
		return 1;
	}

	int ov = 1;
	if(setsockopt(fd_server,SOL_SOCKET,SO_REUSEADDR,&ov,sizeof(ov))<0) {
		elogf("Failed to set socket options: %s",strerror(errno));
		close(fd_server);
		return 1;
	}

	ov = 1;
	if(ioctl(fd_server,FIONBIO,&ov)<0) {
		elogf("Failed to enable non-blocking IO mode: %s",strerror(errno));
		close(fd_server);
		return 1;
	}

	struct sockaddr_in addr;
	int addr_len = sizeof(addr);
	memset(&addr,0,addr_len);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if(bind(fd_server,(struct sockaddr *)&addr,addr_len)<0) {
		elogf("Failed to bind to server socket: %s",strerror(errno));
		return 1;
	}

	if(listen(fd_server,10)<0) {
		elogf("Failed to listen to server socket: %s",strerror(errno));
		return 1;
	}

	while(!shutdown_server) {
		do_server_maintenance();
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd_server, &fds);
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;		
		int s = select(fd_server+1,&fds,NULL,NULL,&timeout);
		if(s>0) {
			int fd_client;
			struct sockaddr client_addr = {0};
			socklen_t client_addr_len = 0;
			if((fd_client = accept(fd_server,&client_addr,&client_addr_len))<0) {
				elogf("Failed to accept on server socket: %s",strerror(errno));
				shutdown_server = 1;
			} else {
				ilogf("Accepted client connection");
				ov = 0;
				if(ioctl(fd_client,FIONBIO,&ov)<0) {
					wlogf("Failed to disable non-blocking IO mode: %s",strerror(errno));
					return 1;
				}
				if(!use_fork) {
					http_client_connect(fd_client,fd_client);
					ilogf("Closing client connection");
					close(fd_client);
				} else {
					ilogf("Forking child process");
					int pgrp = getpgrp();
					int child_pid = fork();
					if(child_pid!=0) {
						// parent process
						ilogf("Forked child pid=%d",child_pid);
						setpgid(child_pid,pgrp);
						close(fd_client);
					} else {
						// child process
						setpgid(child_pid,pgrp);
						signal(SIGINT, sigint_handler_child);
						signal(SIGTERM, sigint_handler_child);
						close(fd_server);
						_fd_client = fd_client;
						// handle request
						http_client_connect(fd_client,fd_client);
						ilogf("Closing client connection");
						close(fd_client);
						CRYPTO_cleanup_all_ex_data();
						ilogf("Exiting child process");
						exit(0);
					} 
				}
			}
		}
	}
	ilogf("Shutting down");
	shutdown(fd_server,SHUT_RDWR);
	close(fd_server);
	// TODO - kill all children

	CRYPTO_cleanup_all_ex_data();

	exit(0);
}

static void usage(FILE * out, const char * prog) {
	fprintf(out,"Usage: %s [options] port [ip-address]\n",prog);
	fprintf(out,"Options:\n");
	fprintf(out,"  --debug                Enable debug output\n");
	fprintf(out,"  --no-fork              Do not fork child processes\n");
	fprintf(out,"  --static-files <path>  Path to static files directory\n");
}

int main(int argc, char ** argv) {
	log_set_level(LEVEL_INFO);
	bool use_fork = true;
	int port = 0;
	uint32_t addr = INVALID_ADDR;
	const char * static_files_dir = "./web";
	// Parse command line arguments
	for(int iarg=1;iarg<argc; iarg++) {
		const char * arg = argv[iarg];
		if(sz_starts_with(arg,"--")) {
			if(0==strcmp("--debug",arg)) {
				log_set_level(LEVEL_DEBUG);
			} else if(0==strcmp("--no-fork",arg)) {
				use_fork = false;
			} else if(0==strcmp("--static-files",arg)) {
				if(++iarg>=argc) {
					fprintf(stderr,"Argument missing for command line option: %s\n",arg);	
					return 1;
				}
				static_files_dir = argv[iarg];
				if(!io_is_dir(static_files_dir)) {
					fprintf(stderr,"Must be a directory: %s\n",static_files_dir);
					return 1;
				}
			} else {
				fprintf(stderr,"Unrecognized command line option: %s\n",arg);
				return 1;
			}
		} else if(port==0) {
			port = atoi(arg);
			if(port<=0) {
				fprintf(stderr,"Invalid port number: %s\n",arg);
				return 1;
			}
		} else if(addr==INVALID_ADDR) {
			addr = net_atoipv4(arg);
			if(addr==INVALID_ADDR) {
				fprintf(stderr,"Invalid ip address: %s\n",arg);
				return 1;
			}
		} else {
			fprintf(stderr,"Unexpected command line argument: %s\n",arg);
			return 1;
		}
	}
	if(port<=0) {
		usage(stderr,argv[0]);
		return 1;
	}
	server(use_fork, port, static_files_dir);

}
