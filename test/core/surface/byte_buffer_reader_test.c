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

#include <grpc/byte_buffer_reader.h>
#include <grpc/byte_buffer.h>
#include <grpc/support/slice.h>
#include <grpc/grpc.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

#include <string.h>

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

static void test_create(void) {
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader *reader;
  gpr_slice empty = gpr_empty_slice();
  LOG_TEST("test_create");
  buffer = grpc_byte_buffer_create(&empty, 1);
  reader = grpc_byte_buffer_reader_create(buffer);
  grpc_byte_buffer_reader_destroy(reader);
  grpc_byte_buffer_destroy(buffer);
}

static void test_read_one_slice(void) {
  gpr_slice slice;
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader *reader;
  gpr_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_one_slice");
  slice = gpr_slice_from_copied_string("test");
  buffer = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);
  reader = grpc_byte_buffer_reader_create(buffer);
  first_code = grpc_byte_buffer_reader_next(reader, &first_slice);
  GPR_ASSERT(first_code != 0);
  GPR_ASSERT(memcmp(GPR_SLICE_START_PTR(first_slice), "test", 4) == 0);
  gpr_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(reader, &second_slice);
  GPR_ASSERT(second_code == 0);
  grpc_byte_buffer_reader_destroy(reader);
  grpc_byte_buffer_destroy(buffer);
}

static void test_read_one_slice_malloc(void) {
  gpr_slice slice;
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader *reader;
  gpr_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_one_slice_malloc");
  slice = gpr_slice_malloc(4);
  memcpy(GPR_SLICE_START_PTR(slice), "test", 4);
  buffer = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);
  reader = grpc_byte_buffer_reader_create(buffer);
  first_code = grpc_byte_buffer_reader_next(reader, &first_slice);
  GPR_ASSERT(first_code != 0);
  GPR_ASSERT(memcmp(GPR_SLICE_START_PTR(first_slice), "test", 4) == 0);
  gpr_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(reader, &second_slice);
  GPR_ASSERT(second_code == 0);
  grpc_byte_buffer_reader_destroy(reader);
  grpc_byte_buffer_destroy(buffer);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_create();
  test_read_one_slice();
  test_read_one_slice_malloc();
  return 0;
}
