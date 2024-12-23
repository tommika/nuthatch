<!--
Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
-->

![alt nuthatch](./doc/nuthatch-icon.png "nuthatch")

Nuthatch
========
A Little Web Socket server (HTTP/1.1) written in C, with really good test
coverage.  The implementation conforms to the Web Socket Protocol (more or less)
as defined in [RFC 6455](https://datatracker.ietf.org/doc/html/rfc6455) and
HTTP/1.1 as defined in [RFC
9112](https://datatracker.ietf.org/doc/html/rfc9112). The codebase includes a
Unit Test framework that's easy to include in C source code. 

Goals for this work are:
1. attain a deeper understanding of the Web Socket (and HTTP) protocol by implementing it from scratch in C
2. achieve as close to 100% test coverage as possible (currently at ~ 94% line coverage, with some tricky error paths not covered.)
3. develop a simple and reusable unit-test framework in support of (1) and (2) 

Dependencies
-------------

* **libssl-dev**. OpenSSL development library.

* **lcov** and **genhtml** _(Optional)_. Used to generate code coverage reports

* **Docker**. _(Optional)_. Used for building and running containerized server executable.

* **Valgrind** _(Optional)_. Used to find memory related bugs. See [Valgrind QuickStart](https://www.valgrind.org/docs/manual/QuickStart.html).


To install dependencies:
```
sudo apt-get -y install libssl-dev
sudo apt-get -y install lcov
sudo apt-get -y install valgrind
```

On Mac,
```
brew install openssl
brew install lcov
```

Build It
--------

### Development build

Development build includes compilation and execution of unit tests with code coverage enabled
```
make clean all
```

To view code coverage report:
```
open build/coverage/index.html
```

Usage
```
$ ./build/server-main 
Usage: ./build/server-main [options] port [ip-address]
Options:
  --debug                Enable debug output
  --no-fork              Do not fork child processes
  --static-files <path>  Path to static files directory
```

Test Driver
-----------
The test driver can be run using `test` target (`make test`). The `test` target executes as part of the default make target (`make` and `make all`), and by default will execute all test cases.

The test driver executable, which can be found in `build/test-man`, has additional options:
```
$ ./build/test-main --help
Usage: ./build/test-main [options] [test-pattern ...]
Options:
  --help       Display this message
  --debug      Enable debug output
  --logs       Dump test execution logs
  -l, --list   List test cases
```

(That pattern matching is currently a prefix-match only.)

You can pass additional arguments to the test driver using the `TEST_ARGS` variable:
```
make TEST_ARGS="<...>"
```

### Listing test cases
To get a list of all test cases,
```
make TEST_ARGS="-l"
```

### Running specific test cases

To run a specific test case (or test cases), you can specify
a test case name pattern (or patterns)
```
make TEST_ARGS="<prefix> ..."
```

E.g., to run all http and ws (web socket) related test cases,
```
make TEST_ARGS="http_ ws_"
```

### Test execution logs
By default, test execution logs are only output for tests that _fail_.
To always output test execution logs, use the 
```
make TEST_ARGS="--logs"
```

To enable debug output from test cases,
```
make TEST_ARGS="--debug"
```

These flags can be combined:
```
make TEST_ARGS="--logs --debug http_"
```

Release Builds
--------------
```
make RELEASE=1 clean all
```

Release builds exclude unit tests and code coverage.

Make sure you do a clean build whenever you switch between development & release builds.

Run It
------

Start the server:

```
make run
```

Start the Web client by opening `web/index.html` in a browser. E.g.,
```
open web/index.html
```

or go to [http://localhost:8088/](http://localhost:8088/).


The server can also be built and run as a docker container:
```
make docker-run
```

or run in the background
```
# start server
make docker-start
...
# get logs
make docker-logs
...
# stop server
make docker-stop
```

Adding additional executables
-----------------------

To add a new executable, simply create a C source file with name ending in `-main.c` in the `./src` directory.
For example, `myexec-main.c`.


Notes
-----

\* The nuthatch icon image is AI-generated using [Google Gemini](https://gemini.google.com/)