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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"

#include "test/core/util/test_config.h"

#include <gtest/gtest.h>

namespace grpc_core {
namespace {

//
// SliceBufferByteStream tests
//

void NotCalledClosure(void* /*arg*/, grpc_error_handle /*error*/) {
  GPR_ASSERT(false);
}

TEST(SliceBufferByteStream, Basic) {
  grpc_core::ExecCtx exec_ctx;
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
  SliceBufferByteStream stream(&buffer, 0);
  grpc_slice_buffer_destroy_internal(&buffer);
  EXPECT_EQ(6U, stream.length());
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, NotCalledClosure, nullptr,
                    grpc_schedule_on_exec_ctx);
  // Read each slice.  Note that Next() always returns synchronously.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    ASSERT_TRUE(stream.Next(~(size_t)0, &closure));
    grpc_slice output;
    grpc_error_handle error = stream.Pull(&output);
    EXPECT_TRUE(error == GRPC_ERROR_NONE);
    EXPECT_TRUE(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(output);
  }
  // Clean up.
  stream.Orphan();
}

TEST(SliceBufferByteStream, Shutdown) {
  grpc_core::ExecCtx exec_ctx;
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
  SliceBufferByteStream stream(&buffer, 0);
  grpc_slice_buffer_destroy_internal(&buffer);
  EXPECT_EQ(6U, stream.length());
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, NotCalledClosure, nullptr,
                    grpc_schedule_on_exec_ctx);
  // Read the first slice.
  ASSERT_TRUE(stream.Next(~(size_t)0, &closure));
  grpc_slice output;
  grpc_error_handle error = stream.Pull(&output);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE(grpc_slice_eq(input[0], output));
  grpc_slice_unref_internal(output);
  // Now shutdown.
  grpc_error_handle shutdown_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("shutdown error");
  stream.Shutdown(GRPC_ERROR_REF(shutdown_error));
  // After shutdown, the next pull() should return the error.
  ASSERT_TRUE(stream.Next(~(size_t)0, &closure));
  error = stream.Pull(&output);
  EXPECT_TRUE(error == shutdown_error);
  GRPC_ERROR_UNREF(error);
  GRPC_ERROR_UNREF(shutdown_error);
  // Clean up.
  stream.Orphan();
}

//
// CachingByteStream tests
//

TEST(CachingByteStream, Basic) {
  grpc_core::ExecCtx exec_ctx;
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
  SliceBufferByteStream underlying_stream(&buffer, 0);
  grpc_slice_buffer_destroy_internal(&buffer);
  // Create cache and caching stream.
  ByteStreamCache cache((OrphanablePtr<ByteStream>(&underlying_stream)));
  ByteStreamCache::CachingByteStream stream(&cache);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, NotCalledClosure, nullptr,
                    grpc_schedule_on_exec_ctx);
  // Read each slice.  Note that next() always returns synchronously,
  // because the underlying byte stream always does.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    ASSERT_TRUE(stream.Next(~(size_t)0, &closure));
    grpc_slice output;
    grpc_error_handle error = stream.Pull(&output);
    EXPECT_TRUE(error == GRPC_ERROR_NONE);
    EXPECT_TRUE(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(output);
  }
  // Clean up.
  stream.Orphan();
  cache.Destroy();
}

TEST(CachingByteStream, Reset) {
  grpc_core::ExecCtx exec_ctx;
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
  SliceBufferByteStream underlying_stream(&buffer, 0);
  grpc_slice_buffer_destroy_internal(&buffer);
  // Create cache and caching stream.
  ByteStreamCache cache((OrphanablePtr<ByteStream>(&underlying_stream)));
  ByteStreamCache::CachingByteStream stream(&cache);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, NotCalledClosure, nullptr,
                    grpc_schedule_on_exec_ctx);
  // Read one slice.
  ASSERT_TRUE(stream.Next(~(size_t)0, &closure));
  grpc_slice output;
  grpc_error_handle error = stream.Pull(&output);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE(grpc_slice_eq(input[0], output));
  grpc_slice_unref_internal(output);
  // Reset the caching stream.  The reads should start over from the
  // first slice.
  stream.Reset();
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    ASSERT_TRUE(stream.Next(~(size_t)0, &closure));
    error = stream.Pull(&output);
    EXPECT_TRUE(error == GRPC_ERROR_NONE);
    EXPECT_TRUE(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(output);
  }
  // Clean up.
  stream.Orphan();
  cache.Destroy();
}

TEST(CachingByteStream, SharedCache) {
  grpc_core::ExecCtx exec_ctx;
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
  SliceBufferByteStream underlying_stream(&buffer, 0);
  grpc_slice_buffer_destroy_internal(&buffer);
  // Create cache and two caching streams.
  ByteStreamCache cache((OrphanablePtr<ByteStream>(&underlying_stream)));
  ByteStreamCache::CachingByteStream stream1(&cache);
  ByteStreamCache::CachingByteStream stream2(&cache);
  grpc_closure closure;
  GRPC_CLOSURE_INIT(&closure, NotCalledClosure, nullptr,
                    grpc_schedule_on_exec_ctx);
  // Read one slice from stream1.
  EXPECT_TRUE(stream1.Next(~(size_t)0, &closure));
  grpc_slice output;
  grpc_error_handle error = stream1.Pull(&output);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE(grpc_slice_eq(input[0], output));
  grpc_slice_unref_internal(output);
  // Read all slices from stream2.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(input); ++i) {
    EXPECT_TRUE(stream2.Next(~(size_t)0, &closure));
    error = stream2.Pull(&output);
    EXPECT_TRUE(error == GRPC_ERROR_NONE);
    EXPECT_TRUE(grpc_slice_eq(input[i], output));
    grpc_slice_unref_internal(output);
  }
  // Now read the second slice from stream1.
  EXPECT_TRUE(stream1.Next(~(size_t)0, &closure));
  error = stream1.Pull(&output);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE(grpc_slice_eq(input[1], output));
  grpc_slice_unref_internal(output);
  // Clean up.
  stream1.Orphan();
  stream2.Orphan();
  cache.Destroy();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
