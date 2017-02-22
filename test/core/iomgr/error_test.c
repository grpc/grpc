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

#include "src/core/lib/iomgr/error.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include "test/core/util/test_config.h"

#include <string.h>

static void test_set_get_int() {
  grpc_error* error = GRPC_ERROR_CREATE(grpc_slice_from_static_string("Test"));
  intptr_t i = 0;
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_FILE_LINE, &i));
  GPR_ASSERT(i);  // line set will never be 0
  GPR_ASSERT(!grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  GPR_ASSERT(!grpc_error_get_int(error, GRPC_ERROR_INT_SIZE, &i));

  intptr_t errno = 314;
  error = grpc_error_set_int(error, GRPC_ERROR_INT_ERRNO, errno);
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_ERRNO, &i));
  GPR_ASSERT(i == errno);

  intptr_t line = 555;
  error = grpc_error_set_int(error, GRPC_ERROR_INT_FILE_LINE, line);
  GPR_ASSERT(grpc_error_get_int(error, GRPC_ERROR_INT_FILE_LINE, &i));
  GPR_ASSERT(i == line);
}

static void test_set_get_str() {
  grpc_error* error = GRPC_ERROR_CREATE(grpc_slice_from_static_string("Test"));
  GPR_ASSERT(!grpc_error_get_str(error, GRPC_ERROR_STR_SYSCALL));
  GPR_ASSERT(!grpc_error_get_str(error, GRPC_ERROR_STR_TSI_ERROR));

  const char* c = grpc_error_get_str(error, GRPC_ERROR_STR_FILE);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "test/core/iomgr/error_test.c"));

  c = grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "Test"));

  error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, "message");
  c = grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "message"));

  error = grpc_error_set_str(error, GRPC_ERROR_STR_DESCRIPTION, "new desc");
  c = grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "new desc"));
}

static void test_copy_and_unref() {
  grpc_error* error1 = GRPC_ERROR_CREATE(grpc_slice_from_static_string("Test"));
  grpc_error* error2 =
      grpc_error_set_str(error1, GRPC_ERROR_STR_GRPC_MESSAGE, "message");
  const char* c = grpc_error_get_str(error1, GRPC_ERROR_STR_GRPC_MESSAGE);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "message"));
  c = grpc_error_get_str(error2, GRPC_ERROR_STR_GRPC_MESSAGE);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "message"));
  GPR_ASSERT(error1 == error2);

  GRPC_ERROR_REF(error1);
  grpc_error* error3 =
      grpc_error_set_str(error1, GRPC_ERROR_STR_DESCRIPTION, "reset");
  GPR_ASSERT(error3 != error1);  // should not be the same because of extra ref
  c = grpc_error_get_str(error3, GRPC_ERROR_STR_GRPC_MESSAGE);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "message"));

  c = grpc_error_get_str(error1, GRPC_ERROR_STR_DESCRIPTION);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "Test"));

  c = grpc_error_get_str(error3, GRPC_ERROR_STR_DESCRIPTION);
  GPR_ASSERT(c);
  GPR_ASSERT(!strcmp(c, "reset"));
}

static void print_error_strings() {
  grpc_error* error = grpc_error_set_int(
      GRPC_ERROR_CREATE(grpc_slice_from_static_string("Error")),
      GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNIMPLEMENTED);
  error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS, 0);
  error = grpc_error_set_int(error, GRPC_ERROR_INT_SIZE, 666);
  error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE, "message");
  gpr_log(GPR_DEBUG, "%s", grpc_error_string(error));
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_set_get_int();
  test_set_get_str();
  test_copy_and_unref();
  print_error_strings();
  grpc_shutdown();

  return 0;
}
