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

void ExpectMessageNull(ValueOrHttp2Status<MessageHandle>&& result) {
  EXPECT_TRUE(result.IsOk());
  MessageHandle message = TakeValue(std::move(result));
  EXPECT_EQ(message, nullptr);
}

void ExpectMessagePayload(ValueOrHttp2Status<MessageHandle>&& result,
                          const size_t expect_length, const uint8_t flags) {
  EXPECT_TRUE(result.IsOk());
  MessageHandle message = TakeValue(std::move(result));
  EXPECT_EQ(message->payload()->Length(), expect_length);
  EXPECT_EQ(message->flags(), flags);
}

void ExpectMessagePayload(ValueOrHttp2Status<MessageHandle>&& result,
                          const size_t expect_length, const uint8_t flags,
                          absl::string_view str) {
  EXPECT_TRUE(result.IsOk());
  MessageHandle message = TakeValue(std::move(result));
  EXPECT_EQ(message->payload()->Length(), expect_length);
  EXPECT_EQ(message->flags(), flags);
  EXPECT_STREQ(message->payload()->JoinIntoString().c_str(),
               std::string(str).c_str());
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

  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result1), 0, kFlags0);

  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result2));
}

TEST(GrpcMessageAssemblerTest, OneMessageInOneFrame) {
  SliceBuffer frame1;
  AppendHeaderAndMessage(frame1, kStr1024);
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes + kStr1024.size());

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());

  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result1), kStr1024.size(), kFlags0, kStr1024);

  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result2));
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
  Http2Status append1 = assembler.AppendNewDataFrame(frame1, kNotEndStream);
  EXPECT_TRUE(append1.IsOk());
  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result1));

  Http2Status append2 = assembler.AppendNewDataFrame(frame2, kNotEndStream);
  EXPECT_TRUE(append2.IsOk());
  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result2));

  Http2Status append3 = assembler.AppendNewDataFrame(frame3, kEndStream);
  EXPECT_TRUE(append3.IsOk());
  ValueOrHttp2Status<MessageHandle> result3 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result3), length, kFlags0);

  ValueOrHttp2Status<MessageHandle> result4 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result4));
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

  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result1), kString1.size(), kFlags0, kString1);

  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  EXPECT_TRUE(result2.IsOk());
  ExpectMessagePayload(std::move(result2), kString2.size(), kFlags0, kString2);

  ValueOrHttp2Status<MessageHandle> result3 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result3), kString3.size(), kFlags0, kString3);

  ValueOrHttp2Status<MessageHandle> result4 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result4));
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

  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result1), 0, kFlags0);

  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result2), 0, kFlags0);

  ValueOrHttp2Status<MessageHandle> result3 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result3), 0, kFlags0);

  ValueOrHttp2Status<MessageHandle> result4 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result4));
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

  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result1), kString1.size(), kFlags0, kString1);

  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result2), 0, kFlags0);

  ValueOrHttp2Status<MessageHandle> result3 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result3), kString3.size(), kFlags0, kString3);

  ValueOrHttp2Status<MessageHandle> result4 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result4));
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
  Http2Status append1 = assembler.AppendNewDataFrame(frame1, kNotEndStream);
  EXPECT_TRUE(append1.IsOk());

  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result1), kStr1024.size(), kFlags0, kStr1024);

  ValueOrHttp2Status<MessageHandle> result11 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result11));

  Http2Status append2 = assembler.AppendNewDataFrame(frame2, kNotEndStream);
  EXPECT_TRUE(append2.IsOk());

  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result2), 2 * kStr1024.size(), kFlags0);

  ValueOrHttp2Status<MessageHandle> result22 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result22));

  Http2Status append3 = assembler.AppendNewDataFrame(frame3, kEndStream);
  EXPECT_TRUE(append3.IsOk());

  ValueOrHttp2Status<MessageHandle> result3 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result3), 2 * kStr1024.size(), kFlags5);

  ValueOrHttp2Status<MessageHandle> result4 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result4), kStr1024.size(), kFlags0);

  ValueOrHttp2Status<MessageHandle> result5 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result5));
}

TEST(GrpcMessageAssemblerTest, IncompleteMessageHeader) {
  SliceBuffer frame1;
  AppendGrpcHeaderToSliceBuffer(frame1, kFlags0, kStr1024.size());
  frame1.RemoveLastNBytes(1);

  GrpcMessageAssembler assembler;
  Http2Status append1 = assembler.AppendNewDataFrame(frame1, kNotEndStream);
  EXPECT_TRUE(append1.IsOk());
  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result1));

  SliceBuffer frame2;
  AppendGrpcHeaderToSliceBuffer(frame2, kFlags0, kStr1024.size());
  SliceBuffer discard;
  frame2.MoveFirstNBytesIntoSliceBuffer(kGrpcHeaderSizeInBytes - 1, discard);
  discard.Clear();
  frame2.Append(Slice::FromCopiedString(kStr1024));

  Http2Status append2 = assembler.AppendNewDataFrame(frame2, kEndStream);
  EXPECT_TRUE(append2.IsOk());
  ValueOrHttp2Status<MessageHandle> result2 = assembler.ExtractMessage();
  ExpectMessagePayload(std::move(result2), kStr1024.size(), kFlags0);

  ValueOrHttp2Status<MessageHandle> result3 = assembler.ExtractMessage();
  ExpectMessageNull(std::move(result3));
}

TEST(GrpcMessageAssemblerTest, ErrorIncompleteMessageHeader) {
  SliceBuffer frame1;
  AppendGrpcHeaderToSliceBuffer(frame1, kFlags0, kStr1024.size());
  frame1.RemoveLastNBytes(1);

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());
  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_FALSE(result1.IsOk());
}

TEST(GrpcMessageAssemblerTest, ErrorIncompleteMessagePayload) {
  SliceBuffer frame1;
  const uint32_t length = kString1.size() + 1;
  AppendHeaderAndPartialMessage(frame1, kFlags0, length, kString1);
  EXPECT_EQ(frame1.Length(), kGrpcHeaderSizeInBytes + kString1.size());

  GrpcMessageAssembler assembler;
  Http2Status result = assembler.AppendNewDataFrame(frame1, kEndStream);
  EXPECT_TRUE(result.IsOk());
  ValueOrHttp2Status<MessageHandle> result1 = assembler.ExtractMessage();
  EXPECT_FALSE(result1.IsOk());
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
