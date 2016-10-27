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

#include <stdio.h>
#include <string.h>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/support/tmpfile.h"
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

static const char prefix[] = "file_test";

static void test_load_empty_file(void) {
  FILE *tmp = NULL;
  grpc_slice slice;
  grpc_slice slice_with_null_term;
  grpc_error *error;
  char *tmp_name;

  LOG_TEST_NAME("test_load_empty_file");

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp_name != NULL);
  GPR_ASSERT(tmp != NULL);
  fclose(tmp);

  error = grpc_load_file(tmp_name, 0, &slice);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == 0);

  error = grpc_load_file(tmp_name, 1, &slice_with_null_term);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice_with_null_term) == 1);
  GPR_ASSERT(GRPC_SLICE_START_PTR(slice_with_null_term)[0] == 0);

  remove(tmp_name);
  gpr_free(tmp_name);
  grpc_slice_unref(slice);
  grpc_slice_unref(slice_with_null_term);
}

static void test_load_failure(void) {
  FILE *tmp = NULL;
  grpc_slice slice;
  grpc_error *error;
  char *tmp_name;

  LOG_TEST_NAME("test_load_failure");

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp_name != NULL);
  GPR_ASSERT(tmp != NULL);
  fclose(tmp);
  remove(tmp_name);

  error = grpc_load_file(tmp_name, 0, &slice);
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == 0);
  gpr_free(tmp_name);
  grpc_slice_unref(slice);
}

static void test_load_small_file(void) {
  FILE *tmp = NULL;
  grpc_slice slice;
  grpc_slice slice_with_null_term;
  grpc_error *error;
  char *tmp_name;
  const char *blah = "blah";

  LOG_TEST_NAME("test_load_small_file");

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp_name != NULL);
  GPR_ASSERT(tmp != NULL);
  GPR_ASSERT(fwrite(blah, 1, strlen(blah), tmp) == strlen(blah));
  fclose(tmp);

  error = grpc_load_file(tmp_name, 0, &slice);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == strlen(blah));
  GPR_ASSERT(!memcmp(GRPC_SLICE_START_PTR(slice), blah, strlen(blah)));

  error = grpc_load_file(tmp_name, 1, &slice_with_null_term);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice_with_null_term) == (strlen(blah) + 1));
  GPR_ASSERT(strcmp((const char *)GRPC_SLICE_START_PTR(slice_with_null_term),
                    blah) == 0);

  remove(tmp_name);
  gpr_free(tmp_name);
  grpc_slice_unref(slice);
  grpc_slice_unref(slice_with_null_term);
}

static void test_load_big_file(void) {
  FILE *tmp = NULL;
  grpc_slice slice;
  grpc_error *error;
  char *tmp_name;
  static const size_t buffer_size = 124631;
  unsigned char *buffer = gpr_malloc(buffer_size);
  unsigned char *current;
  size_t i;

  LOG_TEST_NAME("test_load_big_file");

  memset(buffer, 42, buffer_size);

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp != NULL);
  GPR_ASSERT(tmp_name != NULL);
  GPR_ASSERT(fwrite(buffer, 1, buffer_size, tmp) == buffer_size);
  fclose(tmp);

  error = grpc_load_file(tmp_name, 0, &slice);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == buffer_size);
  current = GRPC_SLICE_START_PTR(slice);
  for (i = 0; i < buffer_size; i++) {
    GPR_ASSERT(current[i] == 42);
  }

  remove(tmp_name);
  gpr_free(tmp_name);
  grpc_slice_unref(slice);
  gpr_free(buffer);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_load_empty_file();
  test_load_failure();
  test_load_small_file();
  test_load_big_file();
  return 0;
}
