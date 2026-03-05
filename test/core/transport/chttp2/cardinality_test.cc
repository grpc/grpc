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

  void RunMultipleFramesTest(bool is_unary);
};

using ::testing::HasSubstr;

void CardinalityTest::RunMultipleFramesTest(bool is_unary) {
  ExecCtx exec_ctx;
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto* s = static_cast<grpc_chttp2_stream*>(
      arena->Alloc(sizeof(grpc_chttp2_stream)));
  memset((void*)s, 0, sizeof(grpc_chttp2_stream));
  s->id = 101;
  grpc_slice_buffer_init(&s->frame_storage);
  s->is_unary = is_unary;
  s->received_payload = false;
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
  if (is_unary) {
    EXPECT_FALSE(err2.ok());
    EXPECT_THAT(
        err2.ToString(),
        HasSubstr(
            "More than one DATA frame with payload received for unary RPC"));
  } else {
    EXPECT_TRUE(err2.ok())
        << "Streaming RPC should allow multiple DATA frames!";
  }
  grpc_slice_unref(slice1);
  grpc_slice_unref(slice2);
}

TEST_F(CardinalityTest, TwoDataFramesInUnaryFlow) {
  RunMultipleFramesTest(/*is_unary=*/true);
}

TEST_F(CardinalityTest, StreamingAllowsMultipleDataFrames) {
  RunMultipleFramesTest(/*is_unary=*/false);
}

TEST_F(CardinalityTest, CompressedFrameWithNoDecompressorFails) {
  ExecCtx exec_ctx;
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto* s = static_cast<grpc_chttp2_stream*>(
      arena->Alloc(sizeof(grpc_chttp2_stream)));
  memset((void*)s, 0, sizeof(grpc_chttp2_stream));
  s->id = 202;
  grpc_slice_buffer_init(&s->frame_storage);
  uint8_t compressed_data[] = {1, 0, 0, 0, 5, 'H', 'e', 'l', 'l', 'o'};
  grpc_slice slice = grpc_slice_from_copied_buffer(
      reinterpret_cast<const char*>(compressed_data), sizeof(compressed_data));
  LOG(INFO) << "--- Sending compressed frame ---";
  auto err = grpc_chttp2_data_parser_parse(nullptr, nullptr, s, slice, 0);
  EXPECT_FALSE(err.ok());
  EXPECT_EQ(err.code(), absl::StatusCode::kInternal)
      << "Expected INTERNAL error code as per gRPC spec, but got: "
      << err.ToString();
  EXPECT_THAT(err.ToString(),
              HasSubstr("Compression bit set but no encoding configured"));
  grpc_slice_unref(slice);
}

TEST_F(CardinalityTest, InvalidCompressionFlagFails) {
  ExecCtx exec_ctx;
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto* s = static_cast<grpc_chttp2_stream*>(
      arena->Alloc(sizeof(grpc_chttp2_stream)));
  memset((void*)s, 0, sizeof(grpc_chttp2_stream));
  s->id = 303;
  grpc_slice_buffer_init(&s->frame_storage);
  uint8_t bad_flag_data[] = {2, 0, 0, 0, 5, 'H', 'e', 'l', 'l', 'o'};
  grpc_slice slice = grpc_slice_from_copied_buffer(
      reinterpret_cast<const char*>(bad_flag_data), sizeof(bad_flag_data));
  LOG(INFO) << "--- Sending frame with illegal compression flag ---";
  auto err = grpc_chttp2_data_parser_parse(nullptr, nullptr, s, slice, 0);
  EXPECT_FALSE(err.ok());
  EXPECT_EQ(err.code(), absl::StatusCode::kInternal);
  EXPECT_THAT(err.ToString(),
              HasSubstr("Invalid gRPC compression flag (must be 0 or 1)"));
  grpc_slice_unref(slice);
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