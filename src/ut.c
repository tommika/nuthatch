// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdnoreturn.h>

#include "log.h"
#include "ut.h"
#include "rnd.h"

// TODO BEGIN - move these to utils
unsigned long ut_elapsed_millis(clock_t start, clock_t end) {
	static long clk_tck = 0;
	if(clk_tck==0) {
		if(((clk_tck = sysconf(_SC_CLK_TCK)))<0) {
			fprintf(stderr,"sysconf error: %s\n", strerror(errno));
			exit(1);
		}
	}
	return ((long)(end - start) * 1000.0) / clk_tck;
}

// TODO END - move these to utils

typedef enum Test_Outcome {
	TEST_FAILED=0,
	TEST_PASSED,
  TEST_SKIPPED
} Test_Outcome;

struct TestCase_S {
  const char * name;
  TestFn fn;
  char * log_buff;
  size_t log_buff_len;
  Test_Outcome outcome;
  clock_t clock_start;
  struct tms tms_start;
  clock_t clock_end;
  struct tms tms_end;
  bool skip;
};

static struct TestCase_S * reg_test_cases = NULL;
static int num_reg_test_cases = 0;

void ut_cleanup(void) {
	if(reg_test_cases) {
		free(reg_test_cases);
		reg_test_cases = NULL;
	}
}

int ut_register(const char * name, TestFn fn) {
	if(!reg_test_cases) {
		reg_test_cases = malloc(sizeof(struct TestCase_S) * (num_reg_test_cases+1));
		atexit(ut_cleanup);
	} else {
		reg_test_cases = realloc(reg_test_cases,sizeof(struct TestCase_S) * (num_reg_test_cases+1));
	}
	struct TestCase_S * tc = &reg_test_cases[num_reg_test_cases++];
	memset(tc,0,sizeof(struct TestCase_S));
	tc->name = name;
	tc->fn = fn;
	tc->skip = false;
	return num_reg_test_cases;
}

static unsigned long tc_elapsed_millis(const struct TestCase_S * tc) {
	return ut_elapsed_millis(tc->clock_start, tc->clock_end);
}

static char ** test_patterns = NULL;
static int num_test_patterns = 0;
static bool dump_logs = false;
static Log_Level log_level = LEVEL_INFO;
static bool list = false;

static FILE * fp_driver_out = NULL; // Where to send test driver output
static jmp_buf ut_jmp_buff;
static bool running_test = false;

noreturn void ut_exit(int code) {
	// exit was called during test execution
	elogf("unexpected call to exit(code=%d)",code);
	longjmp(ut_jmp_buff,2);
}

// This overrides the default exit() function
noreturn void exit(int code) {
	if(!running_test) {
		ilogf("exit(code=%d): shouldn't see this in RELEASE builds",code);
		_exit(code);
	} else {
		ut_exit(code);
	}
}

// to test this, forcing a seg fault:
// `*(int*)0 = 0;`
noreturn static void ut_sig_handler(int sig, siginfo_t *si, void *arg) {
	fprintf(stderr, "ut_sig_handler: sig=%d\n",sig);
	if(running_test) {
		elogf("Signal caught: %d",sig);
		longjmp(ut_jmp_buff,2);
	}
	fprintf(stderr, "Unexpected signal: %d\n",sig);
	_exit(1);
}

static void init_sig_handler(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = ut_sig_handler;
	sa.sa_flags   = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
}

void __ut_assert_failed(FILE * out, const char * test_case, const char * file, int line, const char * msg) {
	fprintf(out,"Assertion failed: %s@%s#%d:\n",test_case,file,line);
	fprintf(out,"=> %s\n",msg);
	longjmp(ut_jmp_buff,1);
}

static Test_Outcome run_test(struct TestCase_S * tc) {
	if (tc->skip) {
		tc->outcome = TEST_SKIPPED;
		tc->clock_start = times(&tc->tms_start);
		tc->clock_end = times(&tc->tms_end);
	} else {
		FILE * f_log_out = open_memstream(&tc->log_buff,&tc->log_buff_len);
		log_init(f_log_out,log_level);
		ilogf(">>> %s", tc->name);
		tc->clock_start = times(&tc->tms_start); // save start time
		running_test = true;
		if (setjmp(ut_jmp_buff) == 0) {
			(*tc->fn)();
			tc->outcome = TEST_PASSED;
		} else {
			tc->outcome = TEST_FAILED;
		}
		running_test = false;
		tc->clock_end = times(&tc->tms_end); // save end time
		ilogf("<<< %s", tc->name);
		fflush(f_log_out);
		fclose(f_log_out);
	}
	return tc->outcome;
}

static void usage(FILE * out, const char * prog) {
	fprintf(out,"Usage: %s [options] [test-pattern ...]\n",prog);
	fprintf(out,"Options:\n");
	fprintf(out,"  --help       Display this message\n");
	fprintf(out,"  --debug      Enable debug output\n");
	fprintf(out,"  --logs       Dump test execution logs\n");
	fprintf(out,"  -l, --list   List test cases\n");
}

static int parse_args(int argc, char** argv) {
	for (int iarg = 1;iarg < argc; iarg++) {
		const char* arg = argv[iarg];
		if (sz_starts_with(arg, "-")) {
			if (0 == strcmp("--help", arg)) {
				usage(fp_driver_out, argv[0]);
				return 1;
			} else if (0 == strcmp("--debug", arg)) {
				log_level = LEVEL_DEBUG;
			} else if (0 == strcmp("--logs", arg)) {
				dump_logs = true;
			} else if (0 == strcmp("-l", arg) || 0 == strcmp("--list", arg)) {
				list = true;
			} else {
				fprintf(fp_driver_out, "Unrecognized option: %s\n", arg);
				usage(fp_driver_out, argv[0]);
				return 1;
			}
		} else {
			test_patterns = argv + iarg;
			num_test_patterns = argc - iarg;
			break;
		}
	}
	// TODO - check that all of the specified test cases are valid
	return 0;
}

int ut_test_driver(int argc, char ** argv) {
	fp_driver_out = stderr;
	int c_passed = 0;
	int c_failed = 0;
	int c_skipped = 0;

	if(parse_args(argc,argv)!=0) {
		return 1;
	}

	if(list) {
		for(int i=0; i<num_reg_test_cases;i++) {
			printf("%s\n",reg_test_cases[i].name);
		}
		return 0;
	}

	if(num_test_patterns>0) {
		for(int i=0; i<num_reg_test_cases; i++) {
			struct TestCase_S * tc = &reg_test_cases[i];
			tc->skip = true;
			for(int p=0; p<num_test_patterns; p++) {
				if(sz_contains_case(tc->name,test_patterns[p],true)) {
					tc->skip = false;
					break;
				}
			}
		}
	}
	init_sig_handler();

	clock_t clock_start;
	struct tms tms_start;
	clock_t clock_end;
	struct tms tms_end;

	clock_start = times(&tms_start); // save overall start time
	fprintf(fp_driver_out,"Starting tests\n");
	for(int i=0; i<num_reg_test_cases; i++) {
		struct TestCase_S * tc = &reg_test_cases[i];
		Test_Outcome outcome = run_test(tc);
		unsigned long millis = tc_elapsed_millis(tc);
		switch(outcome) {
		case TEST_PASSED:
			c_passed++;
			fprintf(fp_driver_out,"Test passed : %s (%lu ms)\n",tc->name,millis);
			break;
		case TEST_FAILED:
			c_failed++;
			fprintf(fp_driver_out,"Test FAILED : %s (%lu ms)\n",tc->name,millis);
			break;
		case TEST_SKIPPED:
			c_skipped++;
			//fprintf(fp_driver_out,"Test skipped: %s\n",tc->name);
			break;
		}
	}
	clock_end = times(&tms_end); // save overall end time
	unsigned long total_millis = ut_elapsed_millis(clock_start,clock_end);

	for(int i=0; i<num_reg_test_cases; i++) {
		struct TestCase_S * tc = &reg_test_cases[i];
		if(tc->outcome != TEST_SKIPPED && (dump_logs || tc->outcome==TEST_FAILED)) {
			fprintf(fp_driver_out, "!!! BEGIN test execution log for test: %s\n",tc->name);
			fwrite(tc->log_buff, 1, tc->log_buff_len, fp_driver_out);
			fprintf(fp_driver_out, "!!! END test execution log for test: %s\n\n",tc->name);
		}
		if(tc->log_buff) {
			free(tc->log_buff);
		}
	}

	fprintf(fp_driver_out, "TOTAL: %d, PASSED: %d, FAILED: %d, SKIPPED: %d (%lu ms)\n\n", 
						   num_reg_test_cases, c_passed, c_failed, c_skipped,total_millis);

	free(reg_test_cases);
	reg_test_cases = NULL;

	return c_failed>0 ? 1 : 0;
}
