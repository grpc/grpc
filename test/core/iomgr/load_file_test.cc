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

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

static const char prefix[] = "file_test";

static void test_load_empty_file(void) {
  FILE* tmp = nullptr;
  grpc_slice slice;
  grpc_slice slice_with_null_term;
  grpc_error* error;
  char* tmp_name;

  LOG_TEST_NAME("test_load_empty_file");

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp_name != nullptr);
  GPR_ASSERT(tmp != nullptr);
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
  FILE* tmp = nullptr;
  grpc_slice slice;
  grpc_error* error;
  char* tmp_name;

  LOG_TEST_NAME("test_load_failure");

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp_name != nullptr);
  GPR_ASSERT(tmp != nullptr);
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
  FILE* tmp = nullptr;
  grpc_slice slice;
  grpc_slice slice_with_null_term;
  grpc_error* error;
  char* tmp_name;
  const char* blah = "blah";

  LOG_TEST_NAME("test_load_small_file");

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp_name != nullptr);
  GPR_ASSERT(tmp != nullptr);
  GPR_ASSERT(fwrite(blah, 1, strlen(blah), tmp) == strlen(blah));
  fclose(tmp);

  error = grpc_load_file(tmp_name, 0, &slice);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == strlen(blah));
  GPR_ASSERT(!memcmp(GRPC_SLICE_START_PTR(slice), blah, strlen(blah)));

  error = grpc_load_file(tmp_name, 1, &slice_with_null_term);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice_with_null_term) == (strlen(blah) + 1));
  GPR_ASSERT(strcmp((const char*)GRPC_SLICE_START_PTR(slice_with_null_term),
                    blah) == 0);

  remove(tmp_name);
  gpr_free(tmp_name);
  grpc_slice_unref(slice);
  grpc_slice_unref(slice_with_null_term);
}

static void test_load_big_file(void) {
  FILE* tmp = nullptr;
  grpc_slice slice;
  grpc_error* error;
  char* tmp_name;
  static const size_t buffer_size = 124631;
  unsigned char* buffer = static_cast<unsigned char*>(gpr_malloc(buffer_size));
  unsigned char* current;
  size_t i;

  LOG_TEST_NAME("test_load_big_file");

  memset(buffer, 42, buffer_size);

  tmp = gpr_tmpfile(prefix, &tmp_name);
  GPR_ASSERT(tmp != nullptr);
  GPR_ASSERT(tmp_name != nullptr);
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

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_load_empty_file();
  test_load_failure();
  test_load_small_file();
  test_load_big_file();
  grpc_shutdown();
  return 0;
}
