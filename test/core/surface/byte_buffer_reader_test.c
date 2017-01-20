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

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

#include <string.h>

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

static void test_read_one_slice(void) {
  grpc_slice slice;
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_one_slice");
  slice = grpc_slice_from_copied_string("test");
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, buffer) &&
             "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_next(&reader, &first_slice);
  GPR_ASSERT(first_code != 0);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(first_slice), "test", 4) == 0);
  grpc_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(&reader, &second_slice);
  GPR_ASSERT(second_code == 0);
  grpc_byte_buffer_destroy(buffer);
}

static void test_read_one_slice_malloc(void) {
  grpc_slice slice;
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_one_slice_malloc");
  slice = grpc_slice_malloc(4);
  memcpy(GRPC_SLICE_START_PTR(slice), "test", 4);
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, buffer) &&
             "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_next(&reader, &first_slice);
  GPR_ASSERT(first_code != 0);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(first_slice), "test", 4) == 0);
  grpc_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(&reader, &second_slice);
  GPR_ASSERT(second_code == 0);
  grpc_byte_buffer_destroy(buffer);
}

static void test_read_none_compressed_slice(void) {
  grpc_slice slice;
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice first_slice, second_slice;
  int first_code, second_code;

  LOG_TEST("test_read_none_compressed_slice");
  slice = grpc_slice_from_copied_string("test");
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, buffer) &&
             "Couldn't init byte buffer reader");
  first_code = grpc_byte_buffer_reader_next(&reader, &first_slice);
  GPR_ASSERT(first_code != 0);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(first_slice), "test", 4) == 0);
  grpc_slice_unref(first_slice);
  second_code = grpc_byte_buffer_reader_next(&reader, &second_slice);
  GPR_ASSERT(second_code == 0);
  grpc_byte_buffer_destroy(buffer);
}

static void test_read_corrupted_slice(void) {
  grpc_slice slice;
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader reader;

  LOG_TEST("test_read_corrupted_slice");
  slice = grpc_slice_from_copied_string("test");
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  buffer->data.raw.compression = GRPC_COMPRESS_GZIP; /* lies! */
  grpc_slice_unref(slice);
  GPR_ASSERT(!grpc_byte_buffer_reader_init(&reader, buffer));
  grpc_byte_buffer_destroy(buffer);
}

static void read_compressed_slice(grpc_compression_algorithm algorithm,
                                  size_t input_size) {
  grpc_slice input_slice;
  grpc_slice_buffer sliceb_in;
  grpc_slice_buffer sliceb_out;
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice read_slice;
  size_t read_count = 0;

  grpc_slice_buffer_init(&sliceb_in);
  grpc_slice_buffer_init(&sliceb_out);

  input_slice = grpc_slice_malloc(input_size);
  memset(GRPC_SLICE_START_PTR(input_slice), 'a', input_size);
  grpc_slice_buffer_add(&sliceb_in, input_slice); /* takes ownership */
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GPR_ASSERT(
        grpc_msg_compress(&exec_ctx, algorithm, &sliceb_in, &sliceb_out));
    grpc_exec_ctx_finish(&exec_ctx);
  }

  buffer = grpc_raw_compressed_byte_buffer_create(sliceb_out.slices,
                                                  sliceb_out.count, algorithm);
  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, buffer) &&
             "Couldn't init byte buffer reader");

  while (grpc_byte_buffer_reader_next(&reader, &read_slice)) {
    GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(read_slice),
                      GRPC_SLICE_START_PTR(input_slice) + read_count,
                      GRPC_SLICE_LENGTH(read_slice)) == 0);
    read_count += GRPC_SLICE_LENGTH(read_slice);
    grpc_slice_unref(read_slice);
  }
  GPR_ASSERT(read_count == input_size);
  grpc_byte_buffer_reader_destroy(&reader);
  grpc_byte_buffer_destroy(buffer);
  grpc_slice_buffer_destroy(&sliceb_out);
  grpc_slice_buffer_destroy(&sliceb_in);
}

static void test_read_gzip_compressed_slice(void) {
  const size_t INPUT_SIZE = 2048;
  LOG_TEST("test_read_gzip_compressed_slice");
  read_compressed_slice(GRPC_COMPRESS_GZIP, INPUT_SIZE);
}

static void test_read_deflate_compressed_slice(void) {
  const size_t INPUT_SIZE = 2048;
  LOG_TEST("test_read_deflate_compressed_slice");
  read_compressed_slice(GRPC_COMPRESS_DEFLATE, INPUT_SIZE);
}

static void test_byte_buffer_from_reader(void) {
  grpc_slice slice;
  grpc_byte_buffer *buffer, *buffer_from_reader;
  grpc_byte_buffer_reader reader;

  LOG_TEST("test_byte_buffer_from_reader");
  slice = grpc_slice_malloc(4);
  memcpy(GRPC_SLICE_START_PTR(slice), "test", 4);
  buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, buffer) &&
             "Couldn't init byte buffer reader");

  buffer_from_reader = grpc_raw_byte_buffer_from_reader(&reader);
  GPR_ASSERT(buffer->type == buffer_from_reader->type);
  GPR_ASSERT(buffer_from_reader->data.raw.compression == GRPC_COMPRESS_NONE);
  GPR_ASSERT(buffer_from_reader->data.raw.slice_buffer.count == 1);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(
                        buffer_from_reader->data.raw.slice_buffer.slices[0]),
                    "test", 4) == 0);

  grpc_byte_buffer_destroy(buffer);
  grpc_byte_buffer_destroy(buffer_from_reader);
}

static void test_readall(void) {
  char *lotsa_as[512];
  char *lotsa_bs[1024];
  grpc_slice slices[2];
  grpc_byte_buffer *buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice slice_out;

  LOG_TEST("test_readall");

  memset(lotsa_as, 'a', 512);
  memset(lotsa_bs, 'b', 1024);
  /* use slices large enough to overflow inlining */
  slices[0] = grpc_slice_malloc(512);
  memcpy(GRPC_SLICE_START_PTR(slices[0]), lotsa_as, 512);
  slices[1] = grpc_slice_malloc(1024);
  memcpy(GRPC_SLICE_START_PTR(slices[1]), lotsa_bs, 1024);

  buffer = grpc_raw_byte_buffer_create(slices, 2);
  grpc_slice_unref(slices[0]);
  grpc_slice_unref(slices[1]);

  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, buffer) &&
             "Couldn't init byte buffer reader");
  slice_out = grpc_byte_buffer_reader_readall(&reader);

  GPR_ASSERT(GRPC_SLICE_LENGTH(slice_out) == 512 + 1024);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(slice_out), lotsa_as, 512) == 0);
  GPR_ASSERT(memcmp(&(GRPC_SLICE_START_PTR(slice_out)[512]), lotsa_bs, 1024) ==
             0);
  grpc_slice_unref(slice_out);
  grpc_byte_buffer_destroy(buffer);
}

static void test_byte_buffer_copy(void) {
  char *lotsa_as[512];
  char *lotsa_bs[1024];
  grpc_slice slices[2];
  grpc_byte_buffer *buffer;
  grpc_byte_buffer *copied_buffer;
  grpc_byte_buffer_reader reader;
  grpc_slice slice_out;

  LOG_TEST("test_byte_buffer_copy");

  memset(lotsa_as, 'a', 512);
  memset(lotsa_bs, 'b', 1024);
  /* use slices large enough to overflow inlining */
  slices[0] = grpc_slice_malloc(512);
  memcpy(GRPC_SLICE_START_PTR(slices[0]), lotsa_as, 512);
  slices[1] = grpc_slice_malloc(1024);
  memcpy(GRPC_SLICE_START_PTR(slices[1]), lotsa_bs, 1024);

  buffer = grpc_raw_byte_buffer_create(slices, 2);
  grpc_slice_unref(slices[0]);
  grpc_slice_unref(slices[1]);
  copied_buffer = grpc_byte_buffer_copy(buffer);

  GPR_ASSERT(grpc_byte_buffer_reader_init(&reader, buffer) &&
             "Couldn't init byte buffer reader");
  slice_out = grpc_byte_buffer_reader_readall(&reader);

  GPR_ASSERT(GRPC_SLICE_LENGTH(slice_out) == 512 + 1024);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(slice_out), lotsa_as, 512) == 0);
  GPR_ASSERT(memcmp(&(GRPC_SLICE_START_PTR(slice_out)[512]), lotsa_bs, 1024) ==
             0);
  grpc_slice_unref(slice_out);
  grpc_byte_buffer_destroy(buffer);
  grpc_byte_buffer_destroy(copied_buffer);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_read_one_slice();
  test_read_one_slice_malloc();
  test_read_none_compressed_slice();
  test_read_gzip_compressed_slice();
  test_read_deflate_compressed_slice();
  test_read_corrupted_slice();
  test_byte_buffer_from_reader();
  test_byte_buffer_copy();
  test_readall();
  return 0;
}
