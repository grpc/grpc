/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/log.h>

#include <stdbool.h>
#include <string.h>

#include "src/core/lib/support/env.h"
#include "test/core/util/test_config.h"

static bool log_func_reached = false;

static void test_callback(gpr_log_func_args *args) {
  GPR_ASSERT(0 == strcmp(__FILE__, args->file));
  GPR_ASSERT(args->severity == GPR_LOG_SEVERITY_INFO);
  GPR_ASSERT(0 == strcmp(args->message, "hello 1 2 3"));
}

static void test_should_log(gpr_log_func_args *args) {
  log_func_reached = true;
}

static void test_should_not_log(gpr_log_func_args *args) { GPR_ASSERT(false); }

#define test_log_function_reached(SEVERITY)     \
  gpr_set_log_function(test_should_log);        \
  log_func_reached = false;                     \
  gpr_log_message(SEVERITY, "hello 1 2 3");     \
  GPR_ASSERT(log_func_reached);                 \
  log_func_reached = false;                     \
  gpr_log(SEVERITY, "hello %d %d %d", 1, 2, 3); \
  GPR_ASSERT(log_func_reached);

#define test_log_function_unreached(SEVERITY) \
  gpr_set_log_function(test_should_not_log);  \
  gpr_log_message(SEVERITY, "hello 1 2 3");   \
  gpr_log(SEVERITY, "hello %d %d %d", 1, 2, 3);

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  /* test logging at various verbosity levels */
  gpr_log(GPR_DEBUG, "%s", "hello world");
  gpr_log(GPR_INFO, "%s", "hello world");
  gpr_log(GPR_ERROR, "%s", "hello world");
  /* should succeed */
  GPR_ASSERT(1);
  gpr_set_log_function(test_callback);
  gpr_log_message(GPR_INFO, "hello 1 2 3");
  gpr_log(GPR_INFO, "hello %d %d %d", 1, 2, 3);
  gpr_set_log_function(NULL);

  /* gpr_log_verbosity_init() will be effective only once, and only before
   * gpr_set_log_verbosity() is called */
  gpr_setenv("GRPC_VERBOSITY", "ERROR");
  gpr_log_verbosity_init();

  test_log_function_reached(GPR_ERROR);
  test_log_function_unreached(GPR_INFO);
  test_log_function_unreached(GPR_DEBUG);

  /* gpr_log_verbosity_init() should not be effective */
  gpr_setenv("GRPC_VERBOSITY", "DEBUG");
  gpr_log_verbosity_init();
  test_log_function_reached(GPR_ERROR);
  test_log_function_unreached(GPR_INFO);
  test_log_function_unreached(GPR_DEBUG);

  gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
  test_log_function_reached(GPR_ERROR);
  test_log_function_reached(GPR_INFO);
  test_log_function_reached(GPR_DEBUG);

  gpr_set_log_verbosity(GPR_LOG_SEVERITY_INFO);
  test_log_function_reached(GPR_ERROR);
  test_log_function_reached(GPR_INFO);
  test_log_function_unreached(GPR_DEBUG);

  gpr_set_log_verbosity(GPR_LOG_SEVERITY_ERROR);
  test_log_function_reached(GPR_ERROR);
  test_log_function_unreached(GPR_INFO);
  test_log_function_unreached(GPR_DEBUG);

  /* gpr_log_verbosity_init() should not be effective */
  gpr_setenv("GRPC_VERBOSITY", "DEBUG");
  gpr_log_verbosity_init();
  test_log_function_reached(GPR_ERROR);
  test_log_function_unreached(GPR_INFO);
  test_log_function_unreached(GPR_DEBUG);

  /* TODO(ctiller): should we add a GPR_ASSERT failure test here */
  return 0;
}
