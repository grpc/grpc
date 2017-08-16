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

#include "src/core/lib/transport/byte_stream.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/slice/slice_internal.h"

#include "test/core/util/test_config.h"

//
// grpc_slice_buffer_stream tests
//

static void not_called_closure(grpc_exec_ctx *exec_ctx, void *arg,
                               grpc_error *error) {
  GPR_ASSERT(false);
}

static void test_slice_buffer_stream_basic(void) {
  gpr_log(GPR_DEBUG, "test_slice_buffer_stream_basic");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  // Create and populate slice buffer.
  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);
  grpc_slice input[] = {
      grpc_slice_from_static_string("foo"),
      grpc_slice_from_static_string("bar"),
  };
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    grpc_slice_buffer_add(&buffer, input[i]);
  }
  // Create byte stream.
  grpc_slice_buffer_stream stream;
  grpc_slice_buffer_stream_init(&stream, &buffer, 0);
  GPR_ASSERT(stream.base.length == 6);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, not_called_closure, NULL,
                    grpc_schedule_on_exec_ctx);
  // Read each slice.  Note that next() always returns synchronously.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    GPR_ASSERT(
        grpc_byte_stream_next(&exec_ctx, &stream.base, ~(size_t)0, &closure));
    grpc_slice output;
    grpc_error *error = grpc_byte_stream_pull(&exec_ctx, &stream.base, &output);
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    GPR_ASSERT(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(&exec_ctx, output);
  }
  // Clean up.
  grpc_byte_stream_destroy(&exec_ctx, &stream.base);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_slice_buffer_stream_shutdown(void) {
  gpr_log(GPR_DEBUG, "test_slice_buffer_stream_shutdown");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  // Create and populate slice buffer.
  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);
  grpc_slice input[] = {
      grpc_slice_from_static_string("foo"),
      grpc_slice_from_static_string("bar"),
  };
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    grpc_slice_buffer_add(&buffer, input[i]);
  }
  // Create byte stream.
  grpc_slice_buffer_stream stream;
  grpc_slice_buffer_stream_init(&stream, &buffer, 0);
  GPR_ASSERT(stream.base.length == 6);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, not_called_closure, NULL,
                    grpc_schedule_on_exec_ctx);
  // Read the first slice.
  GPR_ASSERT(
      grpc_byte_stream_next(&exec_ctx, &stream.base, ~(size_t)0, &closure));
  grpc_slice output;
  grpc_error *error = grpc_byte_stream_pull(&exec_ctx, &stream.base, &output);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_slice_eq(input[0], output));
  grpc_slice_unref_internal(&exec_ctx, output);
  // Now shutdown.
  grpc_error *shutdown_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("shutdown error");
  grpc_byte_stream_shutdown(&exec_ctx, &stream.base,
                            GRPC_ERROR_REF(shutdown_error));
  // After shutdown, the next pull() should return the error.
  GPR_ASSERT(
      grpc_byte_stream_next(&exec_ctx, &stream.base, ~(size_t)0, &closure));
  error = grpc_byte_stream_pull(&exec_ctx, &stream.base, &output);
  GPR_ASSERT(error == shutdown_error);
  GRPC_ERROR_UNREF(error);
  GRPC_ERROR_UNREF(shutdown_error);
  // Clean up.
  grpc_byte_stream_destroy(&exec_ctx, &stream.base);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
  grpc_exec_ctx_finish(&exec_ctx);
}

//
// grpc_caching_byte_stream tests
//

static void test_caching_byte_stream_basic(void) {
  gpr_log(GPR_DEBUG, "test_caching_byte_stream_basic");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  // Create and populate slice buffer byte stream.
  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);
  grpc_slice input[] = {
      grpc_slice_from_static_string("foo"),
      grpc_slice_from_static_string("bar"),
  };
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    grpc_slice_buffer_add(&buffer, input[i]);
  }
  grpc_slice_buffer_stream underlying_stream;
  grpc_slice_buffer_stream_init(&underlying_stream, &buffer, 0);
  // Create cache and caching stream.
  grpc_byte_stream_cache cache;
  grpc_byte_stream_cache_init(&cache, &underlying_stream.base);
  grpc_caching_byte_stream stream;
  grpc_caching_byte_stream_init(&stream, &cache);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, not_called_closure, NULL,
                    grpc_schedule_on_exec_ctx);
  // Read each slice.  Note that next() always returns synchronously,
  // because the underlying byte stream always does.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    GPR_ASSERT(
        grpc_byte_stream_next(&exec_ctx, &stream.base, ~(size_t)0, &closure));
    grpc_slice output;
    grpc_error *error = grpc_byte_stream_pull(&exec_ctx, &stream.base, &output);
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    GPR_ASSERT(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(&exec_ctx, output);
  }
  // Clean up.
  grpc_byte_stream_destroy(&exec_ctx, &stream.base);
  grpc_byte_stream_cache_destroy(&exec_ctx, &cache);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_caching_byte_stream_reset(void) {
  gpr_log(GPR_DEBUG, "test_caching_byte_stream_reset");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  // Create and populate slice buffer byte stream.
  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);
  grpc_slice input[] = {
      grpc_slice_from_static_string("foo"),
      grpc_slice_from_static_string("bar"),
  };
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    grpc_slice_buffer_add(&buffer, input[i]);
  }
  grpc_slice_buffer_stream underlying_stream;
  grpc_slice_buffer_stream_init(&underlying_stream, &buffer, 0);
  // Create cache and caching stream.
  grpc_byte_stream_cache cache;
  grpc_byte_stream_cache_init(&cache, &underlying_stream.base);
  grpc_caching_byte_stream stream;
  grpc_caching_byte_stream_init(&stream, &cache);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, not_called_closure, NULL,
                    grpc_schedule_on_exec_ctx);
  // Read one slice.
  GPR_ASSERT(
      grpc_byte_stream_next(&exec_ctx, &stream.base, ~(size_t)0, &closure));
  grpc_slice output;
  grpc_error *error = grpc_byte_stream_pull(&exec_ctx, &stream.base, &output);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_slice_eq(input[0], output));
  grpc_slice_unref_internal(&exec_ctx, output);
  // Reset the caching stream.  The reads should start over from the
  // first slice.
  grpc_caching_byte_stream_reset(&stream);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    GPR_ASSERT(
        grpc_byte_stream_next(&exec_ctx, &stream.base, ~(size_t)0, &closure));
    error = grpc_byte_stream_pull(&exec_ctx, &stream.base, &output);
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    GPR_ASSERT(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(&exec_ctx, output);
  }
  // Clean up.
  grpc_byte_stream_destroy(&exec_ctx, &stream.base);
  grpc_byte_stream_cache_destroy(&exec_ctx, &cache);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_caching_byte_stream_shared_cache(void) {
  gpr_log(GPR_DEBUG, "test_caching_byte_stream_shared_cache");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  // Create and populate slice buffer byte stream.
  grpc_slice_buffer buffer;
  grpc_slice_buffer_init(&buffer);
  grpc_slice input[] = {
      grpc_slice_from_static_string("foo"),
      grpc_slice_from_static_string("bar"),
  };
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    grpc_slice_buffer_add(&buffer, input[i]);
  }
  grpc_slice_buffer_stream underlying_stream;
  grpc_slice_buffer_stream_init(&underlying_stream, &buffer, 0);
  // Create cache and two caching streams.
  grpc_byte_stream_cache cache;
  grpc_byte_stream_cache_init(&cache, &underlying_stream.base);
  grpc_caching_byte_stream stream1;
  grpc_caching_byte_stream_init(&stream1, &cache);
  grpc_caching_byte_stream stream2;
  grpc_caching_byte_stream_init(&stream2, &cache);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, not_called_closure, NULL,
                    grpc_schedule_on_exec_ctx);
  // Read one slice from stream1.
  GPR_ASSERT(
      grpc_byte_stream_next(&exec_ctx, &stream1.base, ~(size_t)0, &closure));
  grpc_slice output;
  grpc_error *error = grpc_byte_stream_pull(&exec_ctx, &stream1.base, &output);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_slice_eq(input[0], output));
  grpc_slice_unref_internal(&exec_ctx, output);
  // Read all slices from stream2.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    GPR_ASSERT(
        grpc_byte_stream_next(&exec_ctx, &stream2.base, ~(size_t)0, &closure));
    error = grpc_byte_stream_pull(&exec_ctx, &stream2.base, &output);
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    GPR_ASSERT(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(&exec_ctx, output);
  }
  // Now read the second slice from stream1.
  GPR_ASSERT(
      grpc_byte_stream_next(&exec_ctx, &stream1.base, ~(size_t)0, &closure));
  error = grpc_byte_stream_pull(&exec_ctx, &stream1.base, &output);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_slice_eq(input[1], output));
  grpc_slice_unref_internal(&exec_ctx, output);
  // Clean up.
  grpc_byte_stream_destroy(&exec_ctx, &stream1.base);
  grpc_byte_stream_destroy(&exec_ctx, &stream2.base);
  grpc_byte_stream_cache_destroy(&exec_ctx, &cache);
  grpc_slice_buffer_destroy_internal(&exec_ctx, &buffer);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_slice_buffer_stream_basic();
  test_slice_buffer_stream_shutdown();
  test_caching_byte_stream_basic();
  test_caching_byte_stream_reset();
  test_caching_byte_stream_shared_cache();
  return 0;
}
