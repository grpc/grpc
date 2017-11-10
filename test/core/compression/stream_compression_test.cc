/*
 *
 * Copyright 2017 gRPC authors.
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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/compression/stream_compression.h"

static void generate_random_payload(char* payload, size_t size) {
  size_t i;
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  for (i = 0; i < size - 1; ++i) {
    payload[i] = chars[rand() % (int)(sizeof(chars) - 1)];
  }
  payload[size - 1] = '\0';
}

static bool slice_buffer_equals_string(grpc_slice_buffer* buf,
                                       const char* str) {
  size_t i;
  if (buf->length != strlen(str)) {
    return false;
  }
  size_t pointer = 0;
  for (i = 0; i < buf->count; i++) {
    size_t slice_len = GRPC_SLICE_LENGTH(buf->slices[i]);
    if (0 != strncmp(str + pointer, (char*)GRPC_SLICE_START_PTR(buf->slices[i]),
                     slice_len)) {
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
  grpc_stream_compression_context* compress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_COMPRESS);
  grpc_stream_compression_context* decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, nullptr,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  bool end_of_context;
  size_t output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(output_size == sizeof(test_str) - 1);
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
  grpc_stream_compression_context* compress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_COMPRESS);
  grpc_stream_compression_context* decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, nullptr,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  grpc_stream_compression_context_destroy(compress_ctx);

  bool end_of_context;
  size_t output_size;
  size_t max_output_size = 2;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    max_output_size, &end_of_context));
  GPR_ASSERT(output_size == max_output_size);
  GPR_ASSERT(end_of_context == false);
  grpc_slice slice_recv = grpc_slice_buffer_take_first(&sink);
  char* str_recv = (char*)GRPC_SLICE_START_PTR(slice_recv);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice_recv) == max_output_size);
  GPR_ASSERT(0 == strncmp(test_str, str_recv, max_output_size));
  grpc_slice_unref(slice_recv);

  size_t remaining_size = sizeof(test_str) - 1 - max_output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    remaining_size, &end_of_context));
  GPR_ASSERT(output_size == remaining_size);
  GPR_ASSERT(end_of_context == true);

  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str + max_output_size));

  grpc_stream_compression_context_destroy(decompress_ctx);
  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
}

#define LARGE_DATA_SIZE (1024 * 1024)
static void
test_stream_compression_simple_compress_decompress_with_large_data() {
  char* test_str =
      static_cast<char*>(gpr_malloc(LARGE_DATA_SIZE * sizeof(char)));
  generate_random_payload(test_str, LARGE_DATA_SIZE);
  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  grpc_stream_compression_context* compress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_COMPRESS);
  grpc_stream_compression_context* decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, nullptr,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  bool end_of_context;
  size_t output_size;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, &output_size,
                                    ~(size_t)0, &end_of_context));
  GPR_ASSERT(output_size == LARGE_DATA_SIZE - 1);
  grpc_stream_compression_context_destroy(compress_ctx);
  grpc_stream_compression_context_destroy(decompress_ctx);

  GPR_ASSERT(slice_buffer_equals_string(&sink, test_str));

  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
  gpr_free(test_str);
}

static void test_stream_compression_drop_context() {
  const char test_str[] = "aaaaaaabbbbbbbccccccc";
  const char test_str2[] = "dddddddeeeeeeefffffffggggg";
  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  grpc_stream_compression_context* compress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_COMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, nullptr,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  grpc_stream_compression_context_destroy(compress_ctx);

  compress_ctx = grpc_stream_compression_context_create(
      GRPC_STREAM_COMPRESSION_GZIP_COMPRESS);
  slice = grpc_slice_from_static_string(test_str2);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, nullptr,
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

  grpc_stream_compression_context* decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
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
      GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
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
  grpc_stream_compression_context* compress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_COMPRESS);
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice_buffer_add(&source, slice);
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, nullptr,
                                  ~(size_t)0,
                                  GRPC_STREAM_COMPRESSION_FLUSH_SYNC));

  grpc_stream_compression_context* decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS);
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
  GPR_ASSERT(grpc_stream_compress(compress_ctx, &source, &relay, nullptr,
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

int main(int argc, char** argv) {
  grpc_init();
  test_stream_compression_simple_compress_decompress();
  test_stream_compression_simple_compress_decompress_with_output_size_constraint();
  test_stream_compression_simple_compress_decompress_with_large_data();
  test_stream_compression_sync_flush();
  test_stream_compression_drop_context();
  grpc_shutdown();

  return 0;
}
