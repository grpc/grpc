//
// Copyright 2026 gRPC authors.
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
//
//

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace {

class CardinalityTest : public ::testing::Test {
 protected:
  void SetUp() override { grpc_init(); }

  void TearDown() override { grpc_shutdown(); }
};

TEST_F(CardinalityTest, StreamingAllowsMultipleDataFrames) {
  ExecCtx exec_ctx;
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto* s = static_cast<grpc_chttp2_stream*>(
      arena->Alloc(sizeof(grpc_chttp2_stream)));
  memset((void*)s, 0, sizeof(grpc_chttp2_stream));
  s->id = 101;
  grpc_slice_buffer_init(&s->frame_storage);
  uint8_t frame1[] = {0, 0, 0, 0, 4, 'm', 's', 'g', '1'};
  grpc_slice slice1 =
      grpc_slice_from_copied_buffer((const char*)frame1, sizeof(frame1));
  uint8_t frame2[] = {0, 0, 0, 0, 4, 'm', 's', 'g', '2'};
  grpc_slice slice2 =
      grpc_slice_from_copied_buffer((const char*)frame2, sizeof(frame2));
  LOG(INFO) << "--- Sending first frame ---";
  auto err1 = grpc_chttp2_data_parser_parse(nullptr, nullptr, s, slice1, 0);
  ASSERT_TRUE(err1.ok()) << err1.ToString();
  LOG(INFO) << "--- Sending second frame ---";
  auto err2 = grpc_chttp2_data_parser_parse(nullptr, nullptr, s, slice2, 0);
  EXPECT_TRUE(err2.ok()) << "Streaming RPC should allow multiple DATA frames!";
  grpc_slice_unref(slice1);
  grpc_slice_unref(slice2);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}