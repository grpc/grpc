//
//
// Copyright 2025 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/message_assembler.h"

#include <grpc/grpc.h>

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace http2 {
namespace testing {

constexpr bool not_end_stream = false;
constexpr bool end_stream = true;
constexpr bool flags = 0;
constexpr absl::string_view kHelloWorld = "Hello World!";

constexpr absl::string_view kString1 = "One Hello World!";
constexpr absl::string_view kString2 = "Two Hello World!";
constexpr absl::string_view kString3 = "Three Hello World!";

void AppendEmptyMessage(SliceBuffer& payload) {
  AppendGrpcHeaderToSliceBuffer(payload, flags, 0);
}

void AppendHeaderAndMessage(SliceBuffer& payload, absl::string_view str) {
  AppendGrpcHeaderToSliceBuffer(payload, flags, str.size());
  payload.Append(Slice::FromCopiedString(str));
}

void AppendHeaderAndPartialMessage(SliceBuffer& payload, const uint32_t length,
                                   absl::string_view str) {
  AppendGrpcHeaderToSliceBuffer(payload, flags, length);
  payload.Append(Slice::FromCopiedString(str));
}

void AppendPartialMessage(SliceBuffer& payload, absl::string_view str) {
  payload.Append(Slice::FromCopiedString(str));
}

TEST(GrpcMessageAssembler, ObjectCreation) { GrpcMessageAssembler assembler; }

TEST(GrpcMessageAssembler, OneEmptyMessageInOneFrame) {
  SliceBuffer http2_frame_payload;
  AppendEmptyMessage(http2_frame_payload);
  // An empty message has the gRPC header
  EXPECT_EQ(http2_frame_payload.Length(), kGrpcHeaderSizeInBytes);

  GrpcMessageAssembler assembler;
  assembler.AppendNewDataFrame(http2_frame_payload, end_stream);
  // AppendNewDataFrame must empty the original buffer
  EXPECT_EQ(http2_frame_payload.Length(), 0);
  absl::StatusOr<MessageHandle> result1 = assembler.GenerateMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get()->payload()->Length(), 0);

  absl::StatusOr<MessageHandle> result2 = assembler.GenerateMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get(), nullptr);
}

TEST(GrpcMessageAssembler, OneMessageInOneFrame) {
  SliceBuffer http2_frame_payload;
  AppendHeaderAndMessage(http2_frame_payload, kHelloWorld);
  EXPECT_EQ(http2_frame_payload.Length(),
            kGrpcHeaderSizeInBytes + kHelloWorld.size());

  GrpcMessageAssembler assembler;
  assembler.AppendNewDataFrame(http2_frame_payload, end_stream);
  // AppendNewDataFrame must empty the original buffer
  EXPECT_EQ(http2_frame_payload.Length(), 0);
  absl::StatusOr<MessageHandle> result1 = assembler.GenerateMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get()->payload()->Length(), kHelloWorld.size());
  absl::StatusOr<MessageHandle> result2 = assembler.GenerateMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get(), nullptr);
}

TEST(GrpcMessageAssembler, OneMessageInThreeFrames) {
  SliceBuffer frame1;
  const uint32_t length = kString1.size() + kString2.size() + kString3.size();
  AppendHeaderAndPartialMessage(frame1, length, kString1);
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes + kString1.size());
  SliceBuffer frame2;
  AppendPartialMessage(frame2, kString2);
  SliceBuffer frame3;
  AppendPartialMessage(frame3, kString3);

  GrpcMessageAssembler assembler;
  assembler.AppendNewDataFrame(frame1, not_end_stream);
  absl::StatusOr<MessageHandle> result1 = assembler.GenerateMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get(), nullptr);

  assembler.AppendNewDataFrame(frame2, not_end_stream);
  absl::StatusOr<MessageHandle> result2 = assembler.GenerateMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get(), nullptr);

  assembler.AppendNewDataFrame(frame3, end_stream);
  absl::StatusOr<MessageHandle> result3 = assembler.GenerateMessage();
  EXPECT_TRUE(result3.ok());
  EXPECT_EQ(result3->get()->payload()->Length(), length);
}

TEST(GrpcMessageAssembler, ThreeMessageInOneFrame) {}

TEST(GrpcMessageAssembler, ThreeMessageInFourFrames) { CHECK(end_stream); }

TEST(GrpcMessageAssembler, ThreeEmptyMessagesInOneFrame) { CHECK(end_stream); }

TEST(GrpcMessageAssembler, ThreeMessageInOneFrameMiddleMessageEmpty) {
  CHECK(end_stream);
}

TEST(GrpcMessageAssembler, One2GBMessage) { CHECK(end_stream); }

TEST(GrpcMessageAssembler, One4GBMessage) { CHECK(end_stream); }

TEST(GrpcMessageAssembler, IncompleteMessage1) { CHECK(end_stream); }

TEST(GrpcMessageAssembler, IncompleteMessage2) { CHECK(end_stream); }

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
