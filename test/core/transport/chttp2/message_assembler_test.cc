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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/call/message.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"

namespace grpc_core {
namespace http2 {
namespace testing {

constexpr bool kNotEndStream = false;
constexpr bool kEndStream = true;

///////////////////////////////////////////////////////////////////////////////
// Helper functions

void AppendEmptyMessage(SliceBuffer& payload) {
  AppendGrpcHeaderToSliceBuffer(payload, kFlags0, 0);
}

void AppendHeaderAndMessage(SliceBuffer& payload, absl::string_view str) {
  AppendGrpcHeaderToSliceBuffer(payload, kFlags0, str.size());
  payload.Append(Slice::FromCopiedString(str));
}

void AppendHeaderAndPartialMessage(SliceBuffer& payload, const uint8_t flag,
                                   const uint32_t length,
                                   absl::string_view str) {
  AppendGrpcHeaderToSliceBuffer(payload, flag, length);
  payload.Append(Slice::FromCopiedString(str));
}

void AppendPartialMessage(SliceBuffer& payload, absl::string_view str) {
  payload.Append(Slice::FromCopiedString(str));
}

///////////////////////////////////////////////////////////////////////////////
// GrpcMessageAssembler Tests

TEST(GrpcMessageAssemblerTest, MustMakeSliceBufferEmpty) {
  SliceBuffer frame1;
  AppendHeaderAndMessage(frame1, kStr1024);
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes + kStr1024.size());

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());
  // AppendNewDataFrame must empty the original buffer
  EXPECT_EQ(frame1.Length(), 0);
}

TEST(GrpcMessageAssemblerTest, OneEmptyMessageInOneFrame) {
  SliceBuffer frame1;
  AppendEmptyMessage(frame1);
  // An empty message has the gRPC header
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes);

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());

  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get()->payload()->Length(), 0);

  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, OneMessageInOneFrame) {
  SliceBuffer frame1;
  AppendHeaderAndMessage(frame1, kStr1024);
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes + kStr1024.size());

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());

  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  SliceBuffer* payload1 = result1->get()->payload();
  EXPECT_EQ(payload1->Length(), kStr1024.size());
  EXPECT_STREQ(payload1->JoinIntoString().c_str(),
               std::string(kStr1024).c_str());

  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, OneMessageInThreeFrames) {
  SliceBuffer frame1;
  const uint32_t length = kString1.size() + kString2.size() + kString3.size();
  AppendHeaderAndPartialMessage(frame1, kFlags0, length, kString1);
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes + kString1.size());
  SliceBuffer frame2;
  AppendPartialMessage(frame2, kString2);
  SliceBuffer frame3;
  AppendPartialMessage(frame3, kString3);

  GrpcMessageAssembler assembler;
  Http2Status append_result1 =
      assembler.AppendNewDataFrame(frame1, kNotEndStream);
  EXPECT_TRUE(append_result1.IsOk());
  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get(), nullptr);

  Http2Status append_result2 =
      assembler.AppendNewDataFrame(frame2, kNotEndStream);
  EXPECT_TRUE(append_result2.IsOk());
  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get(), nullptr);

  Http2Status append_result3 = assembler.AppendNewDataFrame(frame3, kEndStream);
  EXPECT_TRUE(append_result3.IsOk());
  absl::StatusOr<MessageHandle> result3 = assembler.ExtractMessage();
  EXPECT_TRUE(result3.ok());
  EXPECT_EQ(result3->get()->payload()->Length(), length);
  EXPECT_EQ(result3->get()->flags(), kFlags0);

  absl::StatusOr<MessageHandle> result4 = assembler.ExtractMessage();
  EXPECT_TRUE(result4.ok());
  EXPECT_EQ(result4->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, ThreeMessageInOneFrame) {
  SliceBuffer frame1;
  const uint32_t length = kString1.size() + kString2.size() + kString3.size();
  AppendHeaderAndMessage(frame1, kString1);
  AppendHeaderAndMessage(frame1, kString2);
  AppendHeaderAndMessage(frame1, kString3);
  EXPECT_EQ(frame1.Length(), length + (3 * kGrpcHeaderSizeInBytes));

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());

  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  SliceBuffer* payload1 = result1->get()->payload();
  EXPECT_EQ(payload1->Length(), kString1.size());
  EXPECT_STREQ(payload1->JoinIntoString().c_str(),
               std::string(kString1).c_str());

  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  SliceBuffer* payload2 = result2->get()->payload();
  EXPECT_EQ(payload2->Length(), kString2.size());
  EXPECT_STREQ(payload2->JoinIntoString().c_str(),
               std::string(kString2).c_str());

  absl::StatusOr<MessageHandle> result3 = assembler.ExtractMessage();
  EXPECT_TRUE(result3.ok());
  SliceBuffer* payload3 = result3->get()->payload();
  EXPECT_EQ(payload3->Length(), kString3.size());
  EXPECT_STREQ(payload3->JoinIntoString().c_str(),
               std::string(kString3).c_str());

  absl::StatusOr<MessageHandle> result4 = assembler.ExtractMessage();
  EXPECT_TRUE(result4.ok());
  EXPECT_EQ(result4->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, ThreeEmptyMessagesInOneFrame) {
  SliceBuffer frame1;
  AppendEmptyMessage(frame1);
  AppendEmptyMessage(frame1);
  AppendEmptyMessage(frame1);
  EXPECT_EQ(frame1.Length(), 3 * kGrpcHeaderSizeInBytes);

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());

  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get()->payload()->Length(), 0);

  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get()->payload()->Length(), 0);

  absl::StatusOr<MessageHandle> result3 = assembler.ExtractMessage();
  EXPECT_TRUE(result3.ok());
  EXPECT_EQ(result3->get()->payload()->Length(), 0);

  absl::StatusOr<MessageHandle> result4 = assembler.ExtractMessage();
  EXPECT_TRUE(result4.ok());
  EXPECT_EQ(result4->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, ThreeMessageInOneFrameMiddleMessageEmpty) {
  SliceBuffer frame1;
  const uint32_t length = kString1.size() + kString3.size();
  AppendHeaderAndMessage(frame1, kString1);
  AppendEmptyMessage(frame1);
  AppendHeaderAndMessage(frame1, kString3);
  EXPECT_EQ(frame1.Length(), length + (3 * kGrpcHeaderSizeInBytes));

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());

  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  SliceBuffer* payload1 = result1->get()->payload();
  EXPECT_EQ(payload1->Length(), kString1.size());
  EXPECT_STREQ(payload1->JoinIntoString().c_str(),
               std::string(kString1).c_str());

  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get()->payload()->Length(), 0);

  absl::StatusOr<MessageHandle> result3 = assembler.ExtractMessage();
  EXPECT_TRUE(result3.ok());
  SliceBuffer* payload3 = result3->get()->payload();
  EXPECT_EQ(payload3->Length(), kString3.size());
  EXPECT_STREQ(payload3->JoinIntoString().c_str(),
               std::string(kString3).c_str());

  absl::StatusOr<MessageHandle> result4 = assembler.ExtractMessage();
  EXPECT_TRUE(result4.ok());
  EXPECT_EQ(result4->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, FourMessageInThreeFrames) {
  SliceBuffer frame1;
  AppendHeaderAndMessage(frame1, kStr1024);  // Message 1 complete
  AppendHeaderAndPartialMessage(frame1, kFlags0, (2 * kStr1024.size()),
                                kStr1024);

  SliceBuffer frame2;
  AppendPartialMessage(frame2, kStr1024);  // Message 2 complete
  AppendHeaderAndPartialMessage(frame2, kFlags5, (2 * kStr1024.size()),
                                kStr1024);

  SliceBuffer frame3;
  AppendPartialMessage(frame3, kStr1024);    // Message 3 complete
  AppendHeaderAndMessage(frame3, kStr1024);  // Message 4 complete

  GrpcMessageAssembler assembler;
  Http2Status append_result1 =
      assembler.AppendNewDataFrame(frame1, kNotEndStream);
  EXPECT_TRUE(append_result1.IsOk());

  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get()->payload()->Length(), kStr1024.size());

  absl::StatusOr<MessageHandle> result11 = assembler.ExtractMessage();
  EXPECT_TRUE(result11.ok());
  EXPECT_EQ(result11->get(), nullptr);

  Http2Status append_result2 =
      assembler.AppendNewDataFrame(frame2, kNotEndStream);
  EXPECT_TRUE(append_result2.IsOk());

  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get()->payload()->Length(), 2 * kStr1024.size());
  EXPECT_EQ(result2->get()->flags(), kFlags0);

  absl::StatusOr<MessageHandle> result22 = assembler.ExtractMessage();
  EXPECT_TRUE(result22.ok());
  EXPECT_EQ(result22->get(), nullptr);

  Http2Status append_result3 = assembler.AppendNewDataFrame(frame3, kEndStream);
  EXPECT_TRUE(append_result3.IsOk());

  absl::StatusOr<MessageHandle> result3 = assembler.ExtractMessage();
  EXPECT_TRUE(result3.ok());
  EXPECT_EQ(result3->get()->payload()->Length(), 2 * kStr1024.size());
  EXPECT_EQ(result3->get()->flags(), kFlags5);

  absl::StatusOr<MessageHandle> result4 = assembler.ExtractMessage();
  EXPECT_TRUE(result4.ok());
  EXPECT_EQ(result4->get()->payload()->Length(), kStr1024.size());

  absl::StatusOr<MessageHandle> result5 = assembler.ExtractMessage();
  EXPECT_TRUE(result5.ok());
  EXPECT_EQ(result5->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, IncompleteMessageHeader) {
  SliceBuffer frame1;
  AppendGrpcHeaderToSliceBuffer(frame1, kFlags0, kStr1024.size());
  frame1.RemoveLastNBytes(1);

  GrpcMessageAssembler assembler;
  Http2Status append_result1 =
      assembler.AppendNewDataFrame(frame1, kNotEndStream);
  EXPECT_TRUE(append_result1.IsOk());
  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_TRUE(result1.ok());
  EXPECT_EQ(result1->get(), nullptr);

  SliceBuffer frame2;
  AppendGrpcHeaderToSliceBuffer(frame2, kFlags0, kStr1024.size());
  SliceBuffer discard;
  frame2.MoveFirstNBytesIntoSliceBuffer(kGrpcHeaderSizeInBytes - 1, discard);
  discard.Clear();
  frame2.Append(Slice::FromCopiedString(kStr1024));

  Http2Status append_result2 = assembler.AppendNewDataFrame(frame2, kEndStream);
  EXPECT_TRUE(append_result2.IsOk());
  absl::StatusOr<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.ok());
  EXPECT_EQ(result2->get()->payload()->Length(), kStr1024.size());
  EXPECT_EQ(result2->get()->flags(), kFlags0);

  absl::StatusOr<MessageHandle> result3 = assembler.ExtractMessage();
  EXPECT_TRUE(result3.ok());
  EXPECT_EQ(result3->get(), nullptr);
}

TEST(GrpcMessageAssemblerTest, ErrorIncompleteMessageHeader) {
  SliceBuffer frame1;
  AppendGrpcHeaderToSliceBuffer(frame1, kFlags0, kStr1024.size());
  frame1.RemoveLastNBytes(1);

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());
  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_FALSE(result1.ok());
}

TEST(GrpcMessageAssemblerTest, ErrorIncompleteMessagePayload) {
  SliceBuffer frame1;
  const uint32_t length = kString1.size() + 1;
  AppendHeaderAndPartialMessage(frame1, kFlags0, length, kString1);
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes + kString1.size());

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());
  absl::StatusOr<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_FALSE(result1.ok());
}

///////////////////////////////////////////////////////////////////////////////
// GrpcMessageDisassembler Tests

MessageHandle MakeMessage() {
  SliceBuffer payload;
  payload.Append(Slice::FromCopiedString(kStr1024));
  return Arena::MakePooled<Message>(std::move(payload), kFlags0);
}

TEST(GrpcMessageDisassemblerTest, OneMessageToOneFrame) {
  const size_t expected_size = kGrpcHeaderSizeInBytes + kStr1024.size();
  auto message = MakeMessage();

  const uint32_t stream_id = 1;
  GrpcMessageDisassembler disassembler;
  EXPECT_EQ(disassembler.GetBufferedLength(), 0);
  disassembler.PrepareSingleMessageForSending(std::move(message));
  EXPECT_EQ(disassembler.GetBufferedLength(), expected_size);

  const uint32_t max_length = 1024 * 2;
  Http2DataFrame frame1 =
      disassembler.GenerateNextFrame(stream_id, max_length, kNotEndStream);
  EXPECT_EQ(frame1.stream_id, stream_id);
  EXPECT_EQ(frame1.end_stream, kNotEndStream);
  EXPECT_EQ(frame1.payload.Length(), expected_size);
  EXPECT_EQ(disassembler.GetBufferedLength(), 0);
}

TEST(GrpcMessageDisassemblerTest, TwoMessageOneFrame) {
  auto message1 = MakeMessage();
  auto message2 = MakeMessage();
  const size_t expected_size = kGrpcHeaderSizeInBytes + kStr1024.size();

  const uint32_t stream_id = 1;
  GrpcMessageDisassembler disassembler;
  EXPECT_EQ(disassembler.GetBufferedLength(), 0);
  disassembler.PrepareBatchedMessageForSending(std::move(message1));
  EXPECT_EQ(disassembler.GetBufferedLength(), expected_size);
  disassembler.PrepareBatchedMessageForSending(std::move(message2));
  EXPECT_EQ(disassembler.GetBufferedLength(), expected_size * 2);

  const uint32_t max_length = expected_size * 3;
  Http2DataFrame frame1 =
      disassembler.GenerateNextFrame(stream_id, max_length, kNotEndStream);
  EXPECT_EQ(frame1.stream_id, stream_id);
  EXPECT_EQ(frame1.end_stream, kNotEndStream);
  EXPECT_EQ(frame1.payload.Length(), expected_size * 2);
  EXPECT_EQ(disassembler.GetBufferedLength(), 0);
}

TEST(GrpcMessageDisassemblerTest, TwoMessageThreeFrames) {
  auto message1 = MakeMessage();
  auto message2 = MakeMessage();
  const size_t expected_size = kGrpcHeaderSizeInBytes + kStr1024.size();

  const uint32_t stream_id = 1;
  GrpcMessageDisassembler disassembler;
  EXPECT_EQ(disassembler.GetBufferedLength(), 0);
  disassembler.PrepareBatchedMessageForSending(std::move(message1));
  EXPECT_EQ(disassembler.GetBufferedLength(), expected_size);
  disassembler.PrepareBatchedMessageForSending(std::move(message2));
  EXPECT_EQ(disassembler.GetBufferedLength(), expected_size * 2);

  const uint32_t max_length = 1024;
  const uint8_t num_iterations = 3;
  uint32_t counter = 0;
  size_t total_bytes_counter = 0;
  while (disassembler.GetBufferedLength() > 0) {
    ++counter;
    Http2DataFrame frame1 =
        disassembler.GenerateNextFrame(stream_id, max_length, kNotEndStream);
    EXPECT_EQ(frame1.stream_id, stream_id);
    EXPECT_EQ(frame1.end_stream, kNotEndStream);
    EXPECT_LE(frame1.payload.Length(), max_length);
    total_bytes_counter += frame1.payload.Length();
  }
  EXPECT_EQ(counter, num_iterations);
  EXPECT_EQ(total_bytes_counter, expected_size * 2);
}

TEST(GrpcMessageDisassemblerTest, GenerateEmptyEndFrame) {
  const uint32_t stream_id = 1;
  GrpcMessageDisassembler disassembler;
  EXPECT_EQ(disassembler.GetBufferedLength(), 0);

  Http2DataFrame frame = disassembler.GenerateEmptyEndFrame(stream_id);
  EXPECT_EQ(frame.stream_id, stream_id);
  EXPECT_EQ(frame.end_stream, kEndStream);
  EXPECT_EQ(frame.payload.Length(), 0);
  EXPECT_EQ(disassembler.GetBufferedLength(), 0);
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
