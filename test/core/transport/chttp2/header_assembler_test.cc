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

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/transport/chttp2/http2_common_test_inputs.h"

namespace grpc_core {
namespace http2 {
namespace testing {

#define USUAL_HDR                                                     \
  "\x00\x00\xc9\x01\x04\x00\x00\x00\x01" /* headers: generated from   \
                                            simple_request.headers */ \
  "\x10\x05:path\x08/foo/bar"                                         \
  "\x10\x07:scheme\x04http"                                           \
  "\x10\x07:method\x04POST"                                           \
  "\x10\x0a:authority\x09localhost"                                   \
  "\x10\x0c"                                                          \
  "content-type\x10"                                                  \
  "application/grpc"                                                  \
  "\x10\x14grpc-accept-encoding\x15"                                  \
  "deflate,identity,gzip"                                             \
  "\x10\x02te\x08trailers"                                            \
  "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"

///////////////////////////////////////////////////////////////////////////////
// Helpers

Http2HeaderFrame GenerateHeaderFrame(absl::string_view str,
                                     uint32_t stream_id = 0,
                                     bool end_headers = false,
                                     bool end_stream = false) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedString(str));
  return Http2HeaderFrame{stream_id, end_headers, end_stream,
                          std::move(buffer)};
}

///////////////////////////////////////////////////////////////////////////////
// HeaderAssembler - Constructor

TEST(HeaderAssemblerTest, Constructor) {
  HeaderAssembler assembler;
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), 0u);
  EXPECT_EQ(assembler.IsReady(), false);
}

///////////////////////////////////////////////////////////////////////////////
// HeaderAssembler - One Header Frame

TEST(HeaderAssemblerTest, ValidOneHeaderFrame) {
  // 1. Correctly read a HTTP2 header that is sent in one HTTP2 HEADERS frame.
  // 2. Validate output of GetBufferedHeadersLength
  // 3. Validate the contents of the Metadata.
  const uint32_t stream_id = 1111;
  HPackParser parser;
  absl::BitGen bitgen;
  HeaderAssembler assembler;
  Http2HeaderFrame header =
      GenerateHeaderFrame("USUAL_HDR", stream_id, /*end_headers=*/true,
                          /*end_stream=*/false);
  EXPECT_EQ(assembler.GetBufferedHeadersLength(), 0u);
  EXPECT_EQ(assembler.IsReady(), false);
  assembler.AppendHeaderFrame(std::move(header));
  EXPECT_GE(assembler.GetBufferedHeadersLength(), 0u);
  EXPECT_EQ(assembler.IsReady(), true);
  ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> result =
      assembler.ReadMetadata(parser, /*is_initial_metadata=*/true,
                             /*is_client=*/true, bitgen);
  EXPECT_TRUE(result.IsOk());
  Arena::PoolPtr<grpc_metadata_batch> metadata = TakeValue(std::move(result));
}

// TODO(tjagtap) : [PH2][P0] : Check if all instances of GRPC_UNUSED have been
// removed.

TEST(HeaderAssemblerTest, InvalidAssemblerNotReady1) {
  // Crash on invalid API usage.
  // If we try to read the Header before END_HEADERS is received.
  GRPC_UNUSED HeaderAssembler assembler;
}

///////////////////////////////////////////////////////////////////////////////
// HeaderAssembler - One Header Two Continuation Frames

TEST(HeaderAssemblerTest, ValidOneHeaderTwoContinuationFrame) {
  // 1. Correctly read and parse one Header and two Continuation Frames.
  // 2. Validate output of GetBufferedHeadersLength after each frame.
  // 3. Validate the contents of the Metadata.
  GRPC_UNUSED HeaderAssembler assembler;
}

TEST(HeaderAssemblerTest, InvalidAssemblerNotReady2) {
  // Crash on invalid API usage.
  // If we try to read the Header before END_HEADERS is received.
  GRPC_UNUSED HeaderAssembler assembler;
}

///////////////////////////////////////////////////////////////////////////////
// Other Valid cases

TEST(HeaderAssemblerTest, ValidTwoHeaderFrames) {
  // This scenario represents a case where the sender sends Initial Metadata and
  // Trailing Metadata after that. Without any messages.
  // 1. Correctly read a HTTP2 header that is sent in one HTTP2 HEADERS frame.
  // 2. Validate output of GetBufferedHeadersLength
  // 3. Validate the contents of the Metadata.
  // 4. Do all the above for the second HEADERS frame.
  GRPC_UNUSED HeaderAssembler assembler;
}

TEST(HeaderAssemblerTest, ValidMultipleHeadersAndContinuations) {
  // This scenario represents a case where the sender sends Initial Metadata and
  // Trailing Metadata after that. Without any messages.
  // 1. Correctly read all the Header and Continuation frames.
  // 2. Validate output of GetBufferedHeadersLength at each step.
  // 3. Validate the contents of the Metadata.
  // 4. Do all the above for the second set of Header and Continuation frames.
  GRPC_UNUSED HeaderAssembler assembler;
}

///////////////////////////////////////////////////////////////////////////////
// Peer not honouring RFC9113

TEST(HeaderAssemblerTest, InvalidOneHeaderFrameZeroStreamID) {
  // Assembler receiving a HEADERS frame with zero stream id should crash.
  GRPC_UNUSED HeaderAssembler assembler;
}

TEST(HeaderAssemblerTest, InvalidZeroStreamID) {
  // Assembler receiving a CONTINUATION frame with zero stream id should crash.
  // Second frame has zero stream id.
  GRPC_UNUSED HeaderAssembler assembler;
}

TEST(HeaderAssemblerTest, InvalidTwoHeaderFrames) {
  // Connection Error if second HEADER frame is received before the first one is
  // END_STREAM.
  GRPC_UNUSED HeaderAssembler assembler;
}

TEST(HeaderAssemblerTest, InvalidHeaderAndContinuationHaveDifferentStreamID) {
  // Fail if the HEADER and CONTINUATION frame do not have the same stream id.
  GRPC_UNUSED HeaderAssembler assembler;
}

TEST(HeaderAssemblerTest, InvalidContinuationBeforeHeaders) {
  // Fail if the CONTINUATION frame is received before HEADERS.
  GRPC_UNUSED HeaderAssembler assembler;
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
