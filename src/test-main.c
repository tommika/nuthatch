// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef EXCLUDE_UNIT_TESTS

#include "ut.h"
#include "log.h"

#include <openssl/crypto.h>
  
int main(int argc, char ** argv) {
  int ec = ut_test_driver(argc, argv);
  CRYPTO_cleanup_all_ex_data();
  return ec;
}

#endif // !EXCLUDE_UNIT_TESTS

