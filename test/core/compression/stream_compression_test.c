/*
 *
 * Copyright 2017, Google Inc.
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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>
#include <grpc/support/alloc.h>

#include "src/core/ext/transport/chttp2/transport/stream_compression.h"

static void generate_random_payload(char *payload, size_t size) {
  size_t i;
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  for (i = 0; i < size - 1; ++i) {
    payload[i] = chars[rand() % (int)(sizeof(chars) - 1)];
  }
  payload[size - 1] = '\0';
}

static bool slice_buffer_equals_string(grpc_slice_buffer *buf, const char *str) {
  size_t i;
  if (buf->length != strlen(str)) {
    return false;
  }
  size_t pointer = 0;
  for (i = 0; i < buf->count; i++) {
    size_t slice_len = GRPC_SLICE_LENGTH(buf->slices[i]);
    if (0 != strncmp(str + pointer, (char *)GRPC_SLICE_START_PTR(buf->slices[i]), slice_len)) {
      return false;
    }
    pointer += slice_len;
  }
  return true;
}

static void test_stream_compression_simple_compress_decompress() {
  const char test_str[] = "aaaaaaabbbbbbbccccccctesttesttest";
  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  grpc_stream_compression_context *compress_ctx =
      grpc_stream_compression_context_create(GRPC_STREAM_COMPRESSION_COMPRESS);
  grpc_stream_compression_context *decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_DECOMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, NULL,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  bool end_of_context;
  size_t output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(output_size = sizeof(test_str) - 1);
  grpc_stream_compression_context_destroy(compress_ctx);
  grpc_stream_compression_context_destroy(decompress_ctx);

  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str));

  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
}

static void
test_stream_compression_simple_compress_decompress_with_output_size_constraint() {
  const char test_str[] = "aaaaaaabbbbbbbccccccctesttesttest";
  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  grpc_stream_compression_context *compress_ctx =
      grpc_stream_compression_context_create(GRPC_STREAM_COMPRESSION_COMPRESS);
  grpc_stream_compression_context *decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_DECOMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, NULL,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  grpc_stream_compression_context_destroy(compress_ctx);

  bool end_of_context;
  size_t output_size;
  size_t max_output_size = 2;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    max_output_size, &end_of_context));
  GPR_ASSERT(output_size = max_output_size);
  GPR_ASSERT(end_of_context == false);
  grpc_slice slice_recv = grpc_slice_buffer_take_first(&sink);
  char *str_recv = (char *)GRPC_SLICE_START_PTR(slice_recv);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice_recv) == max_output_size);
  GPR_ASSERT(0 == strncmp(test_str, str_recv, max_output_size));
  grpc_slice_unref(slice_recv);

  size_t remaining_size = sizeof(test_str) - 1 - max_output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    remaining_size, &end_of_context));
  GPR_ASSERT(output_size = remaining_size);
  GPR_ASSERT(end_of_context = true);

  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str + max_output_size));

  grpc_stream_compression_context_destroy(decompress_ctx);
  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
}

#define LARGE_DATA_SIZE 1024 * 1024
static void test_stream_compression_simple_compress_decompress_with_large_data() {
  char test_str[LARGE_DATA_SIZE];
  generate_random_payload(test_str, LARGE_DATA_SIZE);
  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  grpc_stream_compression_context *compress_ctx =
      grpc_stream_compression_context_create(GRPC_STREAM_COMPRESSION_COMPRESS);
  grpc_stream_compression_context *decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_DECOMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, NULL,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  bool end_of_context;
  size_t output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(output_size = sizeof(test_str) - 1);
  grpc_stream_compression_context_destroy(compress_ctx);
  grpc_stream_compression_context_destroy(decompress_ctx);

  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str));

  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
}

static void test_stream_compression_drop_context() {
  const char test_str[] = "aaaaaaabbbbbbbccccccc";
  const char test_str2[] = "dddddddeeeeeeefffffffggggg";
  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  grpc_stream_compression_context *compress_ctx =
      grpc_stream_compression_context_create(GRPC_STREAM_COMPRESSION_COMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, NULL,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  grpc_stream_compression_context_destroy(compress_ctx);

  compress_ctx =
      grpc_stream_compression_context_create(GRPC_STREAM_COMPRESSION_COMPRESS);
  slice = grpc_slice_from_static_string(test_str2);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, NULL,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  grpc_stream_compression_context_destroy(compress_ctx);

  /* Concatenate the two compressed sliced into one to test decompressing two
   * contexts */
  grpc_slice slice1 = grpc_slice_buffer_take_first(&relay);
  grpc_slice slice2 = grpc_slice_buffer_take_first(&relay);
  grpc_slice slice3 =
      grpc_slice_malloc(GRPC_SLICE_LENGTH(slice1) + GRPC_SLICE_LENGTH(slice2));
  memcpy(GRPC_SLICE_START_PTR(slice3), GRPC_SLICE_START_PTR(slice1),
         GRPC_SLICE_LENGTH(slice1));
  memcpy(GRPC_SLICE_START_PTR(slice3) + GRPC_SLICE_LENGTH(slice1),
         GRPC_SLICE_START_PTR(slice2), GRPC_SLICE_LENGTH(slice2));
  grpc_slice_unref(slice1);
  grpc_slice_unref(slice2);
  grpc_slice_buffer_add(&relay, slice3);

  grpc_stream_compression_context *decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_DECOMPRESS);
  bool end_of_context;
  size_t output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(end_of_context == true);
  GPR_ASSERT(output_size == sizeof(test_str) - 1);

  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str));
  grpc_stream_compression_context_destroy(decompress_ctx);
  grpc_slice_buffer_destroy(&sink);

  grpc_slice_buffer_init(&sink);
  decompress_ctx = grpc_stream_compression_context_create(
      GRPC_STREAM_COMPRESSION_DECOMPRESS);
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(end_of_context == true);
  GPR_ASSERT(output_size == sizeof(test_str2) - 1);
  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str2));
  grpc_stream_compression_context_destroy(decompress_ctx);

  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
}

static void test_stream_compression_sync_flush() {
  const char test_str[] = "aaaaaaabbbbbbbccccccc";
  const char test_str2[] = "dddddddeeeeeeefffffffggggg";
  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  grpc_stream_compression_context *compress_ctx =
      grpc_stream_compression_context_create(GRPC_STREAM_COMPRESSION_COMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, NULL,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_SYNC));

  grpc_stream_compression_context *decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_DECOMPRESS);
  bool end_of_context;
  size_t output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(end_of_context == false);
  GPR_ASSERT(output_size == sizeof(test_str) - 1);
  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str));
  grpc_slice_buffer_destroy(&sink);

  grpc_slice_buffer_init(&sink);
  slice = grpc_slice_from_static_string(test_str2);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, NULL,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  grpc_stream_compression_context_destroy(compress_ctx);

  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(end_of_context == true);
  GPR_ASSERT(output_size == sizeof(test_str2) - 1);
  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str2));
  grpc_stream_compression_context_destroy(decompress_ctx);

  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
}

int main(int argc, char **argv) {
  grpc_init();
  test_stream_compression_simple_compress_decompress();
  test_stream_compression_simple_compress_decompress_with_output_size_constraint();
  test_stream_compression_simple_compress_decompress_with_large_data();
  test_stream_compression_sync_flush();
  test_stream_compression_drop_context();
  grpc_shutdown();

  return 0;
}
