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

#include "src/core/lib/slice/slice_string_helpers.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

static void expect_slice_dump(grpc_slice slice, uint32_t flags,
                              const char *result) {
  char *got = grpc_dump_slice(slice, flags);
  GPR_ASSERT(0 == strcmp(got, result));
  gpr_free(got);
  grpc_slice_unref(slice);
}

static void test_dump_slice(void) {
  static const char *text = "HELLO WORLD!";
  static const char *long_text =
      "It was a bright cold day in April, and the clocks were striking "
      "thirteen. Winston Smith, his chin nuzzled into his breast in an effort "
      "to escape the vile wind, slipped quickly through the glass doors of "
      "Victory Mansions, though not quickly enough to prevent a swirl of "
      "gritty dust from entering along with him.";

  LOG_TEST_NAME("test_dump_slice");

  expect_slice_dump(grpc_slice_from_copied_string(text), GPR_DUMP_ASCII, text);
  expect_slice_dump(grpc_slice_from_copied_string(long_text), GPR_DUMP_ASCII,
                    long_text);
  expect_slice_dump(grpc_slice_from_copied_buffer("\x01", 1), GPR_DUMP_HEX,
                    "01");
  expect_slice_dump(grpc_slice_from_copied_buffer("\x01", 1),
                    GPR_DUMP_HEX | GPR_DUMP_ASCII, "01 '.'");
}

static void test_strsplit(void) {
  grpc_slice_buffer *parts;
  grpc_slice str;

  LOG_TEST_NAME("test_strsplit");

  parts = gpr_malloc(sizeof(grpc_slice_buffer));
  grpc_slice_buffer_init(parts);

  str = grpc_slice_from_copied_string("one, two, three, four");
  grpc_slice_split(str, ", ", parts);
  GPR_ASSERT(4 == parts->count);
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[0], "one"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[1], "two"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[2], "three"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[3], "four"));
  grpc_slice_buffer_reset_and_unref(parts);
  grpc_slice_unref(str);

  /* separator not present in string */
  str = grpc_slice_from_copied_string("one two three four");
  grpc_slice_split(str, ", ", parts);
  GPR_ASSERT(1 == parts->count);
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[0], "one two three four"));
  grpc_slice_buffer_reset_and_unref(parts);
  grpc_slice_unref(str);

  /* separator at the end */
  str = grpc_slice_from_copied_string("foo,");
  grpc_slice_split(str, ",", parts);
  GPR_ASSERT(2 == parts->count);
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[0], "foo"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[1], ""));
  grpc_slice_buffer_reset_and_unref(parts);
  grpc_slice_unref(str);

  /* separator at the beginning */
  str = grpc_slice_from_copied_string(",foo");
  grpc_slice_split(str, ",", parts);
  GPR_ASSERT(2 == parts->count);
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[0], ""));
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[1], "foo"));
  grpc_slice_buffer_reset_and_unref(parts);
  grpc_slice_unref(str);

  /* standalone separator */
  str = grpc_slice_from_copied_string(",");
  grpc_slice_split(str, ",", parts);
  GPR_ASSERT(2 == parts->count);
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[0], ""));
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[1], ""));
  grpc_slice_buffer_reset_and_unref(parts);
  grpc_slice_unref(str);

  /* empty input */
  str = grpc_slice_from_copied_string("");
  grpc_slice_split(str, ", ", parts);
  GPR_ASSERT(1 == parts->count);
  GPR_ASSERT(0 == grpc_slice_str_cmp(parts->slices[0], ""));
  grpc_slice_buffer_reset_and_unref(parts);
  grpc_slice_unref(str);

  grpc_slice_buffer_destroy(parts);
  gpr_free(parts);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_dump_slice();
  test_strsplit();
  return 0;
}
