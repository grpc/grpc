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

#ifndef GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_COMMON_TEST_INPUTS_H
#define GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_COMMON_TEST_INPUTS_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"

namespace grpc_core {
namespace http2 {
namespace testing {

constexpr absl::string_view kStr1024 =
    "1000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "2000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "3000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "4000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "5000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "6000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "7000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "8000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "1000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "2000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "3000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "4000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "5000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "6000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "7000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 "
    "8000001 0000002 0000003 0000004 0000005 0000006 0000007 0000008 ";

// Encoded string of header ":path: /demo.Service/Step".
const std::vector<uint8_t> kPathDemoServiceStep = {
    0x40, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x12, 0x2f,
    0x64, 0x65, 0x6d, 0x6f, 0x2e, 0x53, 0x65, 0x72, 0x76,
    0x69, 0x63, 0x65, 0x2f, 0x53, 0x74, 0x65, 0x70};

// Encoded string of header ":path: /demo.Service/Step2".
const std::vector<uint8_t> kPathDemoServiceStep2 = {
    0x40, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x13, 0x2f,
    0x64, 0x65, 0x6d, 0x6f, 0x2e, 0x53, 0x65, 0x72, 0x76,
    0x69, 0x63, 0x65, 0x2f, 0x53, 0x74, 0x65, 0x70, 0x32};

// Encoded string of header ":path: /demo.Service/Step3".
const std::vector<uint8_t> kPathDemoServiceStep3 = {
    0x40, 0x05, 0x3a, 0x70, 0x61, 0x74, 0x68, 0x13, 0x2f,
    0x64, 0x65, 0x6d, 0x6f, 0x2e, 0x53, 0x65, 0x72, 0x76,
    0x69, 0x63, 0x65, 0x2f, 0x53, 0x74, 0x65, 0x70, 0x33};

constexpr absl::string_view kString1 = "One Hello World!";
constexpr absl::string_view kString2 = "Two Hello World!";
constexpr absl::string_view kString3 = "Three Hello World!";

constexpr uint8_t kFlags0 = 0;
constexpr uint8_t kFlags5 = 5;

// Returns all Http2ErrorCode values EXCEPT kNoError
// This is because we want to test only invalid cases.
inline std::vector<Http2ErrorCode> GetErrorCodes() {
  std::vector<Http2ErrorCode> codes;
  // codes.push_back(Http2ErrorCode::kNoError);
  codes.push_back(Http2ErrorCode::kProtocolError);
  codes.push_back(Http2ErrorCode::kInternalError);
  codes.push_back(Http2ErrorCode::kFlowControlError);
  codes.push_back(Http2ErrorCode::kSettingsTimeout);
  codes.push_back(Http2ErrorCode::kStreamClosed);
  codes.push_back(Http2ErrorCode::kFrameSizeError);
  codes.push_back(Http2ErrorCode::kRefusedStream);
  codes.push_back(Http2ErrorCode::kCancel);
  codes.push_back(Http2ErrorCode::kCompressionError);
  codes.push_back(Http2ErrorCode::kConnectError);
  codes.push_back(Http2ErrorCode::kEnhanceYourCalm);
  codes.push_back(Http2ErrorCode::kInadequateSecurity);
  return codes;
}

// Returns a small subset of available absl::StatusCode values.
// These are the values that we expect to use in the HTTP2 transport.
inline std::vector<absl::StatusCode> FewAbslErrorCodes() {
  std::vector<absl::StatusCode> codes;
  codes.push_back(absl::StatusCode::kCancelled);
  codes.push_back(absl::StatusCode::kInvalidArgument);
  codes.push_back(absl::StatusCode::kInternal);
  return codes;
}

// Helper function to append Header and Continuation frames to the expected
// frames vector based on max_frame_length. The encoded_data is the byte array
// representation of the encoded metadata.
inline void GetExpectedHeaderAndContinuationFrames(
    const uint32_t max_frame_length, std::vector<Http2Frame>& expected_frames,
    const std::vector<uint8_t>& encoded_data, const bool end_stream) {
  DCHECK_LE(encoded_data.size(), std::numeric_limits<uint32_t>::max());
  SliceBuffer encoded_metadata(
      Slice::FromCopiedBuffer(encoded_data.data(), encoded_data.size()));

  if (encoded_metadata.Length() > 0) {
    uint32_t frame_length = std::min(
        static_cast<uint32_t>(encoded_metadata.Length()), max_frame_length);
    SliceBuffer payload;
    encoded_metadata.MoveFirstNBytesIntoSliceBuffer(frame_length, payload);
    bool end_headers = (encoded_metadata.Length() == 0);

    expected_frames.emplace_back(Http2HeaderFrame{/*stream_id=*/1, end_headers,
                                                  /*end_stream=*/end_stream,
                                                  /*payload=*/
                                                  std::move(payload)});

    while (encoded_metadata.Length() > 0) {
      uint32_t frame_length = std::min(
          static_cast<uint32_t>(encoded_metadata.Length()), max_frame_length);
      SliceBuffer payload;
      encoded_metadata.MoveFirstNBytesIntoSliceBuffer(frame_length, payload);
      bool end_headers = (encoded_metadata.Length() == 0);
      expected_frames.emplace_back(Http2ContinuationFrame{/*stream_id=*/1,
                                                          end_headers,
                                                          /*payload=*/
                                                          std::move(payload)});
    }
  }
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_COMMON_TEST_INPUTS_H
