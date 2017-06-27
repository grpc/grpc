/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

static void test_setenv_getenv(void) {
  const char *name = "FOO";
  const char *value = "BAR";
  char *retrieved_value;

  LOG_TEST_NAME("test_setenv_getenv");

  gpr_setenv(name, value);
  retrieved_value = gpr_getenv(name);
  GPR_ASSERT(retrieved_value != NULL);
  GPR_ASSERT(strcmp(value, retrieved_value) == 0);
  gpr_free(retrieved_value);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_setenv_getenv();
  return 0;
}
