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

#include "src/core/ext/transport/chttp2/transport/header_assembler.h"

#include <grpc/grpc.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"

namespace grpc_core {
namespace http2 {
namespace testing {

//  headers: generated from simple_request.headers
constexpr absl::string_view kSimpleRequestEncoded =
    "\x10\x05:path\x08/foo/bar"
    "\x10\x07:scheme\x04http"
    "\x10\x07:method\x04POST"
    "\x10\x0a:authority\x09localhost"
    "\x10\x0c"
    "content-type\x10"
    "application/grpc"
    "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
    "\x10\x02te\x08trailers"
    "\x10\x0auser-agent\x17grpc-c/0.12.0.0 (linux)";

constexpr size_t kSimpleRequestEncodedLen = 190;

constexpr absl::string_view kSimpleRequestEncodedPart1 =
    "\x10\x05:path\x08/foo/bar"
    "\x10\x07:scheme\x04http"
    "\x10\x07:method\x04POST";

constexpr size_t kSimpleRequestEncodedPart1Len = 44;

constexpr absl::string_view kSimpleRequestEncodedPart2 =
    "\x10\x0a:authority\x09localhost"
    "\x10\x0c"
    "content-type\x10"
    "application/grpc";

constexpr size_t kSimpleRequestEncodedPart2Len = 53;

constexpr absl::string_view kSimpleRequestEncodedPart3 =
    "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
    "\x10\x02te\x08trailers"
    "\x10\x0auser-agent\x17grpc-c/0.12.0.0 (linux)";

constexpr size_t kSimpleRequestEncodedPart3Len = 93;

constexpr absl::string_view kSimpleRequestDecoded =
    "user-agent: grpc-c/0.12.0.0 (linux),"
    " :authority: localhost,"
    " :path: /foo/bar,"
    " grpc-accept-encoding: identity,"
    " deflate, gzip, te: trailers,"
    " content-type: application/grpc,"
    " :scheme: http,"
    " :method: POST,"
    " GrpcStatusFromWire: true";

constexpr size_t kSimpleRequestDecodedLen = 224;

TEST(HeaderAssemblerTest, TestTheTestData) {
  EXPECT_EQ(std::string(kSimpleRequestEncoded).size(),
            kSimpleRequestEncodedLen);
  EXPECT_EQ(std::string(kSimpleRequestEncodedPart1).size(),
            kSimpleRequestEncodedPart1Len);
  EXPECT_EQ(std::string(kSimpleRequestEncodedPart2).size(),
            kSimpleRequestEncodedPart2Len);
  EXPECT_EQ(std::string(kSimpleRequestEncodedPart3).size(),
            kSimpleRequestEncodedPart3Len);
  const size_t sum =
      (kSimpleRequestEncodedPart1Len + kSimpleRequestEncodedPart2Len +
       kSimpleRequestEncodedPart3Len);
  EXPECT_EQ(kSimpleRequestEncodedLen, sum);
  EXPECT_EQ(std::string(kSimpleRequestDecoded).size(),
            kSimpleRequestDecodedLen);
}

///////////////////////////////////////////////////////////////////////////////
// Helpers

Http2HeaderFrame GenerateHeaderFrame(absl::string_view str,
                                     const uint32_t stream_id,
                                     const bool end_headers,
                                     const bool end_stream) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedString(str));
  return Http2HeaderFrame{stream_id, end_headers, end_stream,
                          std::move(buffer)};
}

Http2ContinuationFrame GenerateContinuationFrame(absl::string_view str,
                                                 const uint32_t stream_id,
                                                 const bool end_headers) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedString(str));
  return Http2ContinuationFrame{stream_id, end_headers, std::move(buffer)};
}

///////////////////////////////////////////////////////////////////////////////
// HeaderAssembler - One Header Frame

void ValidateOneHeader(const uint32_t stream_id, HPackParser& parser,
                       HeaderAssembler& assembler, const bool end_headers) {
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), 0u);
  EXPECT_EQ(assembler.IsReady(), false);

  Http2HeaderFrame header = GenerateHeaderFrame(
      kSimpleRequestEncoded, stream_id, /*end_headers=*/end_headers,
      /*end_stream=*/false);
  Http2Status status = assembler.AppendHeaderFrame(std::move(header));
  EXPECT_TRUE(status.IsOk());
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), kSimpleRequestEncodedLen);

  if (end_headers) {
    EXPECT_EQ(assembler.IsReady(), true);
    ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> result =
        assembler.ReadMetadata(parser, /*is_initial_metadata=*/true,
                               /*is_client=*/true);
    EXPECT_TRUE(result.IsOk());
    Arena::PoolPtr<grpc_metadata_batch> metadata = TakeValue(std::move(result));
    EXPECT_STREQ(metadata->DebugString().c_str(),
                 std::string(kSimpleRequestDecoded).c_str());
  }
}

TEST(HeaderAssemblerTest, ValidOneHeaderFrame) {
  // 1. Correctly read a HTTP2 header that is sent in one HTTP2 HEADERS frame.
  // 2. Validate output of GetBufferedHeadersLength
  // 3. Validate the contents of the Metadata.
  const uint32_t stream_id = 0x7fffffff;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  ValidateOneHeader(stream_id, parser, assembler, /*end_headers=*/true);
}

TEST(HeaderAssemblerTest, InvalidAssemblerNotReady1) {
  // Crash on invalid API usage.
  // If we try to read the Header before END_HEADERS is received.
  const uint32_t stream_id = 0x12345678;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  Http2HeaderFrame header = GenerateHeaderFrame(
      kSimpleRequestEncoded, stream_id, /*end_headers=*/false,
      /*end_stream=*/false);
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), 0u);
  EXPECT_EQ(assembler.IsReady(), false);

  Http2Status status = assembler.AppendHeaderFrame(std::move(header));
  EXPECT_TRUE(status.IsOk());

  EXPECT_EQ(assembler.GetBufferedHeadersLength(), kSimpleRequestEncodedLen);
  // MUST be false when end_headers is false.
  EXPECT_EQ(assembler.IsReady(), false);

#ifndef NDEBUG
  ASSERT_DEATH(
      {
        GRPC_UNUSED ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>
            result =
                assembler.ReadMetadata(parser, /*is_initial_metadata=*/true,
                                       /*is_client=*/true);
      },
      "");
#endif
}

///////////////////////////////////////////////////////////////////////////////
// HeaderAssembler - One Header Two Continuation Frames

void ValidateOneHeaderTwoContinuation(const uint32_t stream_id,
                                      HPackParser& parser,
                                      HeaderAssembler& assembler,
                                      const bool end_stream) {
  Http2HeaderFrame header = GenerateHeaderFrame(
      kSimpleRequestEncodedPart1, stream_id, /*end_headers=*/false, end_stream);
  Http2ContinuationFrame continuation1 = GenerateContinuationFrame(
      kSimpleRequestEncodedPart2, stream_id, /*end_headers=*/false);
  Http2ContinuationFrame continuation2 = GenerateContinuationFrame(
      kSimpleRequestEncodedPart3, stream_id, /*end_headers=*/true);

  EXPECT_EQ(assembler.GetBufferedHeadersLength(), 0u);
  EXPECT_EQ(assembler.IsReady(), false);

  size_t expected_size = kSimpleRequestEncodedPart1Len;
  Http2Status status = assembler.AppendHeaderFrame(std::move(header));
  EXPECT_TRUE(status.IsOk());
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), expected_size);
  EXPECT_EQ(assembler.IsReady(), false);

  expected_size += kSimpleRequestEncodedPart2Len;
  Http2Status status1 =
      assembler.AppendContinuationFrame(std::move(continuation1));
  EXPECT_TRUE(status1.IsOk());
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), expected_size);
  EXPECT_EQ(assembler.IsReady(), false);

  expected_size += kSimpleRequestEncodedPart3Len;
  Http2Status status2 =
      assembler.AppendContinuationFrame(std::move(continuation2));
  EXPECT_TRUE(status2.IsOk());
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), expected_size);
  EXPECT_EQ(assembler.IsReady(), true);

  ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> result =
      assembler.ReadMetadata(parser, /*is_initial_metadata=*/true,
                             /*is_client=*/true);

  EXPECT_TRUE(result.IsOk());
  Arena::PoolPtr<grpc_metadata_batch> metadata = TakeValue(std::move(result));
  EXPECT_STREQ(metadata->DebugString().c_str(),
               std::string(kSimpleRequestDecoded).c_str());
}

TEST(HeaderAssemblerTest, ValidOneHeaderTwoContinuationFrame) {
  // 1. Correctly read and parse one Header and two Continuation Frames.
  // 2. Validate output of GetBufferedHeadersLength after each frame.
  // 3. Validate the contents of the Metadata.
  const uint32_t stream_id = 0x78654321;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  ValidateOneHeaderTwoContinuation(stream_id, parser, assembler,
                                   /*end_stream=*/false);
}

TEST(HeaderAssemblerTest, InvalidAssemblerNotReady2) {
  // Crash on invalid API usage.
  // If we try to read the Metadata before END_HEADERS is received.
  const uint32_t stream_id = 1111;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  Http2HeaderFrame header =
      GenerateHeaderFrame(kSimpleRequestEncodedPart1, stream_id,
                          /*end_headers=*/false, /*end_stream=*/false);
  Http2ContinuationFrame continuation1 = GenerateContinuationFrame(
      kSimpleRequestEncodedPart2, stream_id, /*end_headers=*/false);

  EXPECT_EQ(assembler.GetBufferedHeadersLength(), 0u);
  EXPECT_EQ(assembler.IsReady(), false);

  Http2Status status = assembler.AppendHeaderFrame(std::move(header));
  EXPECT_TRUE(status.IsOk());
  EXPECT_EQ(assembler.GetBufferedHeadersLength(),
            kSimpleRequestEncodedPart1Len);
  EXPECT_EQ(assembler.IsReady(), false);

  Http2Status status1 =
      assembler.AppendContinuationFrame(std::move(continuation1));
  EXPECT_TRUE(status1.IsOk());
  EXPECT_EQ(assembler.GetBufferedHeadersLength(),
            kSimpleRequestEncodedPart1Len + kSimpleRequestEncodedPart2Len);
  EXPECT_EQ(assembler.IsReady(), false);

#ifndef NDEBUG
  ASSERT_DEATH(
      {
        GRPC_UNUSED ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>
            result =
                assembler.ReadMetadata(parser, /*is_initial_metadata=*/true,
                                       /*is_client=*/true);
      },
      "");
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Other Valid cases

TEST(HeaderAssemblerTest, ValidTwoHeaderFrames) {
  // This test is Valid only for Server. Not for Client.
  // This scenario represents a case where the sender sends Initial Metadata and
  // Trailing Metadata after that. Without any messages.
  // 1. Correctly read a HTTP2 header that is sent in one HTTP2 HEADERS frame.
  // 2. Validate output of GetBufferedHeadersLength
  // 3. Validate the contents of the Metadata.
  // 4. Do all the above for the second HEADERS frame.
  const uint32_t stream_id = 1111;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  ValidateOneHeader(stream_id, parser, assembler, /*end_headers=*/true);
  ValidateOneHeader(stream_id, parser, assembler, /*end_headers=*/true);
}

TEST(HeaderAssemblerTest, ValidMultipleHeadersAndContinuations) {
  // This test is Valid only for Server. Not for Client.
  // This scenario represents a case where the sender sends Initial Metadata and
  // Trailing Metadata after that. Without any messages.
  // 1. Correctly read all the Header and Continuation frames.
  // 2. Validate output of GetBufferedHeadersLength at each step.
  // 3. Validate the contents of the Metadata.
  // 4. Do all the above for the second set of Header and Continuation frames.
  const uint32_t stream_id = 1111;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  ValidateOneHeaderTwoContinuation(stream_id, parser, assembler,
                                   /*end_stream=*/false);
  ValidateOneHeaderTwoContinuation(stream_id, parser, assembler,
                                   /*end_stream=*/true);
}

///////////////////////////////////////////////////////////////////////////////
// Peer not honouring RFC9113

TEST(HeaderAssemblerTest, InvalidTwoHeaderFrames) {
  // Connection Error if second HEADER frame is received before the first one is
  // END_STREAM.
  const uint32_t stream_id = 0x1111;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  Http2HeaderFrame header1 = GenerateHeaderFrame(
      kSimpleRequestEncoded, stream_id, /*end_headers=*/false,
      /*end_stream=*/false);

  Http2Status status1 = assembler.AppendHeaderFrame(std::move(header1));
  EXPECT_TRUE(status1.IsOk());
  EXPECT_EQ(assembler.IsReady(), false);
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), kSimpleRequestEncodedLen);

  Http2HeaderFrame header2 = GenerateHeaderFrame(
      kSimpleRequestEncoded, stream_id, /*end_headers=*/true,
      /*end_stream=*/false);

  Http2Status status2 = assembler.AppendHeaderFrame(std::move(header2));
  EXPECT_FALSE(status2.IsOk());
  EXPECT_EQ(status2.GetType(), Http2Status::Http2ErrorType::kConnectionError);
  EXPECT_EQ(status2.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), 0u);
}

TEST(HeaderAssemblerTest, InvalidHeaderAndContinuationHaveDifferentStreamID) {
  // Fail if the HEADER and CONTINUATION frame do not have the same stream id.
  const uint32_t stream_id = 0x1111;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  Http2HeaderFrame header1 = GenerateHeaderFrame(
      kSimpleRequestEncoded, stream_id, /*end_headers=*/false,
      /*end_stream=*/false);

  Http2Status status1 = assembler.AppendHeaderFrame(std::move(header1));
  EXPECT_TRUE(status1.IsOk());

  Http2ContinuationFrame continuation1 = GenerateContinuationFrame(
      kSimpleRequestEncodedPart2, stream_id + 1, /*end_headers=*/false);

  Http2Status status2 =
      assembler.AppendContinuationFrame(std::move(continuation1));
  EXPECT_FALSE(status2.IsOk());
  EXPECT_EQ(status2.GetType(), Http2Status::Http2ErrorType::kConnectionError);
  EXPECT_EQ(status2.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
}

TEST(HeaderAssemblerTest, InvalidContinuationBeforeHeaders) {
  // Fail if the CONTINUATION frame is received before HEADERS.
  const uint32_t stream_id = 0x1111;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  Http2ContinuationFrame continuation1 = GenerateContinuationFrame(
      kSimpleRequestEncodedPart2, stream_id, /*end_headers=*/false);
  Http2Status status =
      assembler.AppendContinuationFrame(std::move(continuation1));
  EXPECT_FALSE(status.IsOk());
  EXPECT_EQ(status.GetType(), Http2Status::Http2ErrorType::kConnectionError);
  EXPECT_EQ(status.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
}

TEST(HeaderAssemblerTest, InvalidContinuationAfterEndHeaders) {
  // Fail if the CONTINUATION frame is received after END_HEADERS.
  // Fail if the HEADER and CONTINUATION frame do not have the same stream id.
  const uint32_t stream_id = 0x1111;
  HPackParser parser;
  HeaderAssembler assembler(stream_id);
  Http2HeaderFrame header1 = GenerateHeaderFrame(
      kSimpleRequestEncoded, stream_id, /*end_headers=*/true,
      /*end_stream=*/false);

  Http2Status status1 = assembler.AppendHeaderFrame(std::move(header1));
  EXPECT_TRUE(status1.IsOk());

  Http2ContinuationFrame continuation1 = GenerateContinuationFrame(
      kSimpleRequestEncodedPart2, stream_id + 1, /*end_headers=*/false);

  Http2Status status2 =
      assembler.AppendContinuationFrame(std::move(continuation1));
  EXPECT_FALSE(status2.IsOk());
  EXPECT_EQ(status2.GetType(), Http2Status::Http2ErrorType::kConnectionError);
  EXPECT_EQ(status2.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
}

// TODO(tjagtap) : [PH2][P3] : Validate later. Edge case
//  Is this a valid case?
//  First we receive one HEADER frame with END_HEADER . This is initial metadata
//  The stream has no Messages. Hence no DATA Frames
//  Then we receive one HEADER frame with END_HEADER and END_STREAM.
//  We Append both and parse both because we read them together.
//  Is this a valid case?

///////////////////////////////////////////////////////////////////////////////
// HeaderDisassembler - Helpers

constexpr uint32_t kEncodedMetadataLen = 166;

Arena::PoolPtr<grpc_metadata_batch> GenerateMetadata(const uint32_t stream_id,
                                                     bool is_trailing_metadata,
                                                     HPackParser& parser) {
  HeaderAssembler assembler(stream_id);
  Http2HeaderFrame header = GenerateHeaderFrame(
      kSimpleRequestEncoded, stream_id, /*end_headers=*/true,
      /*end_stream=*/is_trailing_metadata);
  EXPECT_EQ(header.payload.Length(), kSimpleRequestEncodedLen);

  Http2Status status = assembler.AppendHeaderFrame(std::move(header));
  ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> result =
      assembler.ReadMetadata(parser, /*is_initial_metadata=*/true,
                             /*is_client=*/true);
  Arena::PoolPtr<grpc_metadata_batch> metadata = TakeValue(std::move(result));
  EXPECT_EQ(metadata->DebugString().c_str(), kSimpleRequestDecoded);
  return metadata;
}

void ExpectBufferLengths(HeaderDisassembler& disassembler,
                         const size_t expected_len) {
  EXPECT_EQ(disassembler.TestOnlyGetMainBufferLength(), expected_len);
  EXPECT_EQ(disassembler.GetBufferedLength(), expected_len);
}

void ValidateHeaderFrame(Http2Frame&& frame, const bool is_trailing_metadata,
                         const bool is_end_headers,
                         const size_t expected_length) {
  EXPECT_TRUE(std::holds_alternative<Http2HeaderFrame>(frame));
  Http2HeaderFrame& header = std::get<Http2HeaderFrame>(frame);
  EXPECT_EQ(header.end_headers, is_end_headers);
  EXPECT_EQ(header.end_stream, is_trailing_metadata);
  EXPECT_EQ(header.payload.Length(), expected_length);
}

void OneMetadataInOneFrame(const uint32_t stream_id,
                           HeaderDisassembler& disassembler,
                           const bool is_trailing_metadata, HPackParser& parser,
                           HPackCompressor& encoder,
                           const uint32_t expected_len1) {
  Arena::PoolPtr<grpc_metadata_batch> metadata =
      GenerateMetadata(stream_id, is_trailing_metadata, parser);
  disassembler.PrepareForSending(std::move(metadata), encoder);
  ExpectBufferLengths(disassembler, expected_len1);

  uint8_t count = 0;
  while (disassembler.HasMoreData()) {
    ++count;
    bool is_end_headers = false;
    Http2Frame frame = disassembler.GetNextFrame(expected_len1, is_end_headers);
    EXPECT_EQ(is_end_headers, true);

    ValidateHeaderFrame(std::move(frame), is_trailing_metadata,
                        /*is_end_headers=*/true, expected_len1);
    ExpectBufferLengths(disassembler, 0u);

    EXPECT_EQ(count, 1);
  }
}

void OneMetadataInThreeFrames(const uint32_t stream_id,
                              HeaderDisassembler& disassembler,
                              const bool is_trailing_metadata,
                              HPackParser& parser, HPackCompressor& encoder) {
  const uint8_t frame_length = (kEncodedMetadataLen / 2) - 1;
  const uint8_t last_frame_size = 2;
  Arena::PoolPtr<grpc_metadata_batch> metadata =
      GenerateMetadata(stream_id, is_trailing_metadata, parser);
  disassembler.PrepareForSending(std::move(metadata), encoder);
  ExpectBufferLengths(disassembler, kEncodedMetadataLen);

  const uint8_t expected_number_of_frames = 3;
  int8_t remaining_length = kEncodedMetadataLen;
  uint8_t count = 0;
  bool is_end_headers = false;
  if (disassembler.HasMoreData()) {
    ++count;
    Http2Frame frame = disassembler.GetNextFrame(frame_length, is_end_headers);
    EXPECT_EQ(is_end_headers, false);

    ValidateHeaderFrame(std::move(frame), is_trailing_metadata,
                        /*is_end_headers=*/false, frame_length);

    remaining_length -= frame_length;
    ExpectBufferLengths(disassembler, remaining_length);
  }
  while (disassembler.HasMoreData()) {
    ++count;
    remaining_length = std::max(remaining_length - frame_length, 0);
    Http2Frame frame = disassembler.GetNextFrame(frame_length, is_end_headers);
    EXPECT_EQ(is_end_headers, (count == expected_number_of_frames));

    EXPECT_TRUE(std::holds_alternative<Http2ContinuationFrame>(frame));
    Http2ContinuationFrame& continuation =
        std::get<Http2ContinuationFrame>(frame);
    EXPECT_EQ(continuation.end_headers, (count == expected_number_of_frames));
    EXPECT_EQ(
        continuation.payload.Length(),
        (count == expected_number_of_frames ? last_frame_size : frame_length));

    ExpectBufferLengths(disassembler, remaining_length);
  }
  EXPECT_EQ(count, expected_number_of_frames);
}

///////////////////////////////////////////////////////////////////////////////
// HeaderDisassembler Tests Initial Metadata Only

TEST(HeaderDisassemblerTest, OneInitialMetadataInOneFrame) {
  const uint32_t stream_id = 1;
  HeaderDisassembler disassembler(stream_id, /*is_trailing_metadata=*/false);
  HPackParser parser;
  HPackCompressor encoder;
  OneMetadataInOneFrame(stream_id, disassembler,
                        /*is_trailing_metadata=*/false, parser, encoder,
                        kEncodedMetadataLen);
}

TEST(HeaderDisassemblerTest, OneInitialMetadataInThreeFrames) {
  const uint32_t stream_id = 3;
  HeaderDisassembler disassembler(stream_id, /*is_trailing_metadata=*/false);
  HPackParser parser;
  HPackCompressor encoder;
  OneMetadataInThreeFrames(stream_id, disassembler,
                           /*is_trailing_metadata=*/false, parser, encoder);
}

///////////////////////////////////////////////////////////////////////////////
// HeaderDisassembler Tests Trailing Metadata Only

TEST(HeaderDisassemblerTest, OneTrailingMetadataInOneFrame) {
  const uint32_t stream_id = 0x7fffffff;
  HeaderDisassembler disassembler(stream_id, /*is_trailing_metadata=*/true);
  HPackParser parser;
  HPackCompressor encoder;
  OneMetadataInOneFrame(stream_id, disassembler, /*is_trailing_metadata=*/true,
                        parser, encoder, kEncodedMetadataLen);
}

TEST(HeaderDisassemblerTest, OneTrailingMetadataInThreeFrames) {
  const uint32_t stream_id = 0x0fffffff;
  HeaderDisassembler disassembler(stream_id, /*is_trailing_metadata=*/true);
  HPackParser parser;
  HPackCompressor encoder;
  OneMetadataInThreeFrames(stream_id, disassembler,
                           /*is_trailing_metadata=*/true, parser, encoder);
}

///////////////////////////////////////////////////////////////////////////////
// HeaderDisassembler Tests Initial and Trailing Metadata

TEST(HeaderDisassemblerTest, OneInitialAndOneTrailingMetadata) {
  const uint32_t stream_id = 0x1111;
  HeaderDisassembler disassembler_initial(stream_id,
                                          /*is_trailing_metadata=*/false);
  HeaderDisassembler disassembler_trailing(stream_id,
                                           /*is_trailing_metadata=*/true);
  HPackParser parser;
  HPackCompressor encoder;
  OneMetadataInOneFrame(stream_id, disassembler_initial,
                        /*is_trailing_metadata=*/false, parser, encoder,
                        kEncodedMetadataLen);
  // Because we are sending the same metadata payload 2 times, the encoder just
  // compresses it to a 8 byte header
  OneMetadataInOneFrame(stream_id, disassembler_trailing,
                        /*is_trailing_metadata=*/true, parser, encoder, 8);
}

TEST(HeaderDisassemblerTest, OneInitialAndOneTrailingMetadataInFourFrames) {
  const uint32_t stream_id = 0x1111;
  HeaderDisassembler disassembler_initial(stream_id,
                                          /*is_trailing_metadata=*/false);
  HeaderDisassembler disassembler_trailing(stream_id,
                                           /*is_trailing_metadata=*/true);
  HPackParser parser;
  HPackCompressor encoder;
  OneMetadataInThreeFrames(stream_id, disassembler_initial,
                           /*is_trailing_metadata=*/false, parser, encoder);
  // Because we are sending the same metadata payload 2 times, the encoder just
  // compresses it to a 8 byte header
  OneMetadataInOneFrame(stream_id, disassembler_trailing,
                        /*is_trailing_metadata=*/true, parser, encoder, 8);
}

///////////////////////////////////////////////////////////////////////////////
// HeaderAassembler HeaderDisassembler Reversibility Test

TEST(HeaderDisassemblerTest, Reversibility) {
  const uint32_t stream_id = 0x1111;
  HPackParser parser;
  // Generate a metadata object
  Arena::PoolPtr<grpc_metadata_batch> metadata =
      GenerateMetadata(stream_id,
                       /*is_trailing_metadata=*/false, parser);

  // Pass metadata to disassembler for frame generation
  HPackCompressor encoder;
  HeaderDisassembler disassembler(stream_id, /*is_trailing_metadata=*/false);
  disassembler.PrepareForSending(std::move(metadata), encoder);
  EXPECT_EQ(disassembler.TestOnlyGetMainBufferLength(), kEncodedMetadataLen);
  EXPECT_TRUE(disassembler.HasMoreData());
  if (disassembler.HasMoreData()) {
    // Generate Http2HeaderFrame from disassembler
    bool is_end_headers = false;
    Http2Frame frame =
        disassembler.GetNextFrame(kEncodedMetadataLen, is_end_headers);
    EXPECT_EQ(is_end_headers, true);

    // Give the frame back to the assembler
    HeaderAssembler assembler(stream_id);
    Http2HeaderFrame& header = std::get<Http2HeaderFrame>(frame);
    Http2Status status = assembler.AppendHeaderFrame(std::move(header));
    ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> result =
        assembler.ReadMetadata(parser, /*is_initial_metadata=*/true,
                               /*is_client=*/true);
    EXPECT_TRUE(result.IsOk());
    Arena::PoolPtr<grpc_metadata_batch> metadata_new =
        TakeValue(std::move(result));
    EXPECT_STREQ(metadata_new->DebugString().c_str(),
                 std::string(kSimpleRequestDecoded).c_str());
  }
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
