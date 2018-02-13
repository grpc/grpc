/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/compression/stream_compression.h"

static void test_compression_context_manager(void) {
  grpc_chttp2_stream_compression_context_manager* ctx_manager =
      grpc_chttp2_stream_compression_context_manager_create(
          GRPC_STREAM_COMPRESSION_GZIP_COMPRESS);
  grpc_stream_compression_context* decompress_ctx =
      grpc_stream_compression_context_create(
          GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS, false);
  grpc_chttp2_stream_compression_context_manager_add_block(ctx_manager, 10,
                                                           {true});
  grpc_chttp2_stream_compression_context_manager_add_block(ctx_manager, 10,
                                                           {false});
  grpc_chttp2_stream_compression_context_manager_add_block(ctx_manager, 10,
                                                           {true});

  grpc_slice_buffer source, relay, sink;
  grpc_slice_buffer_init(&source);
  grpc_slice_buffer_init(&relay);
  grpc_slice_buffer_init(&sink);
  const char test_str[] = "aaaaaaaaaabbbbbbbbbbcccccccccc";
  const char test_str2[] = "cccccccccceeeeeeeeee";
  const char test_str3[] = "ffffffffffgggggggggg";
  grpc_slice slice = grpc_slice_from_static_string(test_str);
  grpc_slice slice2 = grpc_slice_from_static_string(test_str2);
  grpc_slice slice3 = grpc_slice_from_static_string(test_str3);
  grpc_slice_buffer_add(&source, slice);

  GPR_ASSERT(grpc_chttp2_stream_compression_context_manager_compress(
      ctx_manager, &source, &relay, GRPC_STREAM_COMPRESSION_FLUSH_NONE));

  size_t output_size;
  bool end_of_context;
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(sink.length == 10);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(sink.slices[sink.count - 1]), test_str,
                    10) == 0);
  GPR_ASSERT(end_of_context = true);
  grpc_stream_compression_context_destroy(decompress_ctx);
  decompress_ctx = grpc_stream_compression_context_create(
      GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS, false);

  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(sink.length == 20);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(sink.slices[sink.count - 1]),
                    test_str + 10, 10) == 0);
  GPR_ASSERT(end_of_context = true);

  grpc_stream_compression_context_destroy(decompress_ctx);
  decompress_ctx = grpc_stream_compression_context_create(
      GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS, false);

  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(end_of_context == false);

  grpc_chttp2_stream_compression_context_manager_add_block(ctx_manager, 10,
                                                           {false});
  grpc_chttp2_stream_compression_context_manager_add_block(ctx_manager, 10,
                                                           {false});
  grpc_slice_buffer_add(&source, slice2);
  GPR_ASSERT(grpc_chttp2_stream_compression_context_manager_compress(
      ctx_manager, &source, &relay, GRPC_STREAM_COMPRESSION_FLUSH_SYNC));
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(sink.length == 30);
  GPR_ASSERT(end_of_context == true);
  grpc_stream_compression_context_destroy(decompress_ctx);
  decompress_ctx = grpc_stream_compression_context_create(
      GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS, false);
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(sink.length == 50);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(sink.slices[sink.count - 1]),
                    test_str2, 20) == 0);
  GPR_ASSERT(end_of_context == false);

  grpc_slice_buffer_add(&source, slice3);
  grpc_chttp2_stream_compression_context_manager_add_block(ctx_manager, 10,
                                                           {true});
  grpc_chttp2_stream_compression_context_manager_add_block(ctx_manager, 10,
                                                           {true});

  GPR_ASSERT(grpc_chttp2_stream_compression_context_manager_compress(
      ctx_manager, &source, &relay, GRPC_STREAM_COMPRESSION_FLUSH_SYNC));
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(sink.length == 50);
  GPR_ASSERT(end_of_context == true);
  grpc_stream_compression_context_destroy(decompress_ctx);
  decompress_ctx = grpc_stream_compression_context_create(
      GRPC_STREAM_COMPRESSION_GZIP_DECOMPRESS, false);
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(sink.length == 70);
  GPR_ASSERT(memcmp(GRPC_SLICE_START_PTR(sink.slices[sink.count - 1]),
                    test_str3, 20) == 0);
  GPR_ASSERT(end_of_context == false);

  GPR_ASSERT(grpc_chttp2_stream_compression_context_manager_compress(
      ctx_manager, &source, &relay, GRPC_STREAM_COMPRESSION_FLUSH_FINISH));
  GPR_ASSERT(grpc_stream_decompress(decompress_ctx, &relay, &sink, MAX_SIZE_T,
                                    &output_size, MAX_SIZE_T, &end_of_context));
  GPR_ASSERT(sink.length == 70);
  GPR_ASSERT(end_of_context == true);

  grpc_chttp2_stream_compression_context_manager_destroy(ctx_manager);
  grpc_stream_compression_context_destroy(decompress_ctx);

  grpc_slice_buffer_destroy(&source);
  grpc_slice_buffer_destroy(&relay);
  grpc_slice_buffer_destroy(&sink);
}

int main(int argc, char** argv) {
  grpc_init();
  test_compression_context_manager();
  grpc_shutdown();
  return 0;
}
