SHELL := /bin/bash
XARGS:=$(shell command -v gxargs || command -v xargs)
.DEFAULT_GOAL := all

THIS_DIR:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
INCLUDES:=-I$(shell brew --prefix)/include -I$(shell brew --prefix)/opt/openssl/include

else
INCLUDES:=-I/usr/include
endif

PROJ_NAME:=websocket
IMAGE_NAME=$(PROJ_NAME)

RUN_ON_PORT?=8088
RUN_ARGS?=
TEST_ARGS?=--debug

LCOV:=$(shell command -v lcov)
GENHTML:=$(shell command -v genhtml)
VALGRIND:=$(shell command -v valgrind)

BLD_DIR=$(THIS_DIR)build/
SRC_DIR=$(THIS_DIR)src/

# All source files, excluding "*-main.c"
SRCS=$(filter-out $(SRC_DIR)%-main.c,$(wildcard $(SRC_DIR)*.c))
# All object files, excluding "*-main.o"
OBJS=$(patsubst $(SRC_DIR)%.c,$(BLD_DIR)%.o,$(SRCS))

# Sources with a "main"
MAIN_SRCS:=$(wildcard $(SRC_DIR)*-main.c)
ifdef RELEASE
MAIN_SRCS:=$(filter-out $(SRC_DIR)%test-main.c,$(MAIN_SRCS))
endif
MAIN_OBJS=$(patsubst $(SRC_DIR)%.c,$(BLD_DIR)%.o,$(MAIN_SRCS))

CC_FLAGS:=-std=gnu99 -Wall -Werror -Wshadow -MMD $(INCLUDES)

ifdef RELEASE
CC_FLAGS:=$(CC_FLAGS) -O2 -DEXCLUDE_UNIT_TESTS
DEBUG:=
else
CC_FLAGS:=$(CC_FLAGS) -O1 --coverage
DEBUG:=1
endif

ifdef DEBUG
CC_FLAGS:=$(CC_FLAGS) -g
endif

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
	LIBS:=-L$(shell brew --prefix)/lib/ $(LIBS) -lssl -lcrypto
else
	# #be sure to link with static libs
	LIBS:=$(LIBS) -l:libssl.a -l:libcrypto.a -lpthread -ldl
endif

# Build executables from  "*-main.o"
EXES=$(patsubst $(BLD_DIR)%.o,$(BLD_DIR)%,$(MAIN_OBJS))

.SECONDARY: $(OBJS) $(MAIN_OBJS)

# Create object files from source (exlcudes *-main.c)

$(BLD_DIR)%.o: $(SRC_DIR)%.c
	@mkdir -p $(BLD_DIR)
	$(TOOLCHAIN)gcc $(CC_FLAGS) -o $@ -c $<

# Build executables
$(BLD_DIR)%:  $(BLD_DIR)%.o $(OBJS)
	gcc $(CC_FLAGS) -o $@ $^ $(LIBS)

all: build test

# Include all dependecy (.d) files
-include $(OBJS:%.o=%.d) $(MAIN_OBJS:%.o=%.d)

info:
	@echo "THIS_DIR:    $(THIS_DIR)"
	@echo "PROJ_NAME:   $(PROJ_NAME)"
	@echo "RELEASE:     $(RELEASE)" 
	@echo "DEBUG:       $(DEBUG)"
	@echo "EXES:        $(EXES)"
	@echo "SRC_DIR:     $(SRC_DIR)"
	@echo "BLD_DIR:     $(BLD_DIR)"
	@echo "SRCS:        $(SRCS)"
	@echo "OBJS:        $(OBJS)"
	@echo "MAIN_SRCS:   $(MAIN_SRCS)"
	@echo "MAIN_OBJS:   $(MAIN_OBJS)"
	@echo "VALGRIND:    $(VALGRIND)"
	@echo "LCOV:        $(LCOV)"
	@echo "GENHTML:     $(GENHTML)"
	@echo "RUN_ON_PORT: $(RUN_ON_PORT)"
	@echo "RUN_ARGS:    $(RUN_ARGS)"
	@echo "INCLUDES:    $(INCLUDES)"

.PHONY: build
build: rm_gcda $(EXES)

.PHONY: rm_gcda
rm_gcda:
	@rm -f $(BLD_DIR)*.gcda

.PHONY: test
ifdef RELEASE

test:
	@echo ">>> RELEASE build: not running test driver <<<"

else

test: build run-testdriver coverage-report

run-testdriver: rm_gcda
# If valgrid is enabled, "wrap" the execution of the test driver with valgrind
ifeq ($(VALGRIND),)
	$(BLD_DIR)test-main $(TEST_ARGS)
	@echo "VALGRIND was not defined, and was skipped (so there)"
else
	$(VALGRIND) --quiet --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
		$(BLD_DIR)test-main $(TEST_ARGS)
endif

COV_DIR:=$(BLD_DIR)coverage/
coverage-report:
ifeq ($(LCOV),)
	@echo "LCOV was not defined; skipping code coverage report"
else
	@echo "Generating code coverage report in $(COV_DIR)"
	@mkdir -p $(COV_DIR)
	$(LCOV) -rc branch_coverage=1  --capture --directory $(BLD_DIR) --base-directory=$(THIS_DIR) --output $(COV_DIR)lcov.info-temp --test-name "$(PROJ_NAME)"
	$(LCOV) --output $(COV_DIR)lcov.info --remove  $(COV_DIR)lcov.info-temp "$(SRC_DIR)test-main.c" "$(SRC_DIR)ut.c"
	$(GENHTML) -q $(COV_DIR)lcov.info --output-directory $(COV_DIR) --show-details --title "$(PROJ_NAME)"
	@echo "coverage report: $(COV_DIR)index.html"

endif #LCOV

endif # !RELEASE

clean: docker-clean
	rm -rf $(BLD_DIR)

run: build
	$(BLD_DIR)server-main $(RUN_ON_PORT) $(RUN_ARGS)

ifneq ($(VALGRIND),)
VALGRIND_LOG_FILE?=$(BLD_DIR)valgrind.log
run-valgrind: build
	@echo "Writing valgrind log to $(VALGRIND_LOG_FILE)"
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 --log-file=$(VALGRIND_LOG_FILE) --trace-children=yes \
		$(BLD_DIR)server-main $(RUN_ON_PORT) $(RUN_ARGS) \
		|| true
	@echo $?
	@echo "See valgrind log file: $(VALGRIND_LOG_FILE)"
endif

# Docker stuff
docker-build:
	docker build . -t $(IMAGE_NAME):local-latest -f dockerfiles/Dockerfile.ubuntu

# Run the server in a container
docker-run: docker-build
	docker-compose \
		-f $(THIS_DIR)docker-compose.yml \
		up

# Start in the background
docker-start: docker-build
	docker-compose \
		-f $(THIS_DIR)docker-compose.yml \
		up --detach

# Stop and remove the container
docker-stop:
	docker-compose \
		-f $(THIS_DIR)/docker-compose.yml \
		down || true

# Follow the container logs
docker-logs:
	docker logs --follow websocket-server

# Run bash inside container; helpful when debugging
docker-bash: docker-build
	docker run -it websocket-server bash

docker-clean: docker-stop
	docker rmi ${IMAGE_NAME}:local-latest || true


