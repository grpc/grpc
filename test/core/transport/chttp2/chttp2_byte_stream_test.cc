// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/util/mock_endpoint.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

void discard_write(grpc_slice /*slice*/) {}

/**
 * Tests the error ref-counting when Chttp2 stream is shutdown with a
 * non-special error.
 */
TEST(Chttp2ByteStream, ShutdownTest) {
  ExecCtx exec_ctx;
  grpc_stream_refcount ref;
  GRPC_STREAM_REF_INIT(&ref, 1, nullptr, nullptr, "phony ref");
  grpc_endpoint* mock_endpoint = grpc_mock_endpoint_create(discard_write);
  const grpc_channel_args* args = CoreConfiguration::Get()
                                      .channel_args_preconditioning()
                                      .PreconditionChannelArgs(nullptr);
  grpc_transport* t = grpc_create_chttp2_transport(args, mock_endpoint, true);
  grpc_chttp2_transport_start_reading(t, nullptr, nullptr, nullptr);
  grpc_channel_args_destroy(args);
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(
      gpr_malloc(grpc_transport_stream_size(t)));
  s->id = 1;
  s->byte_stream_error = GRPC_ERROR_NONE;
  grpc_transport_init_stream(reinterpret_cast<grpc_transport*>(t),
                             reinterpret_cast<grpc_stream*>(s), &ref, nullptr,
                             nullptr);

  // Create a Chttp2 stream and immediately shut it down
  Chttp2IncomingByteStream* chttp2_byte_stream = new Chttp2IncomingByteStream(
      reinterpret_cast<grpc_chttp2_transport*>(t), s, 1000, 0);

  grpc_error_handle shutdown_error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("shutdown error");
  // Chttp2ByteStream->Shutdown must be called with a ref of the non-special
  // error. The Shutdown method implementation will unref this error. If the
  // ref is not taken here, the subsequent unref will lead to a double free.
  chttp2_byte_stream->Shutdown(GRPC_ERROR_REF(shutdown_error));
  exec_ctx.Flush();

  EXPECT_EQ(s->read_closed_error, shutdown_error);
  EXPECT_EQ(s->write_closed_error, shutdown_error);
  ASSERT_TRUE(s->read_closed);
  ASSERT_TRUE(s->write_closed);

  // Clean up.
  chttp2_byte_stream->Orphan();
  grpc_transport_destroy_stream(reinterpret_cast<grpc_transport*>(t),
                                reinterpret_cast<grpc_stream*>(s), nullptr);
  exec_ctx.Flush();
  gpr_free(s);
  grpc_transport_destroy(t);
  exec_ctx.Flush();

  GRPC_ERROR_UNREF(shutdown_error);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
