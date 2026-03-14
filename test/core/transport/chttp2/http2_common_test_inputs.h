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

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "test/core/test_util/postmortem.h"
#include "test/core/transport/chttp2/http2_frame_test_helper.h"
#include "test/core/transport/util/transport_test.h"
#include "absl/strings/string_view.h"

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

inline Http2HeaderFrame GenerateHeaderFrame(absl::string_view str,
                                            const uint32_t stream_id,
                                            const bool end_headers,
                                            const bool end_stream) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedString(str));
  return Http2HeaderFrame{stream_id, end_headers, end_stream,
                          std::move(buffer)};
}

inline Http2ContinuationFrame GenerateContinuationFrame(
    absl::string_view str, const uint32_t stream_id, const bool end_headers) {
  SliceBuffer buffer;
  buffer.Append(Slice::FromCopiedString(str));
  return Http2ContinuationFrame{stream_id, end_headers, std::move(buffer)};
}

class Http2TransportTest : public util::testing::TransportTest {
 public:
  explicit Http2TransportTest() {
    grpc_tracer_set_enabled("http2_ph2_transport", true);
  }

  void SetUp() override {
    endpoint_ = util::testing::EventSequenceEndpoint::Create(event_engine());
  }
  void TearDown() override {
    endpoint_.reset();
    event_engine()->TickUntilIdle();
    event_engine()->UnsetGlobalHooks();
  }

 protected:
  std::shared_ptr<util::testing::EventSequenceEndpoint> endpoint() const {
    return endpoint_;
  }

  uint64_t VerifyPingFrameAndReturnOpaqueId(SliceBuffer& buffer,
                                            const bool is_ack) const {
    char out_buffer[kFrameHeaderSize + 1] = {};
    // Extract the header (first 9 bytes)
    buffer.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, out_buffer);

    // Verify it looks like a PING frame.
    // Construct a PING frame with opaque=0 and compare the frame header.
    auto expect_slice = helper_.EventEngineSliceFromHttp2PingFrame(
        /*ack=*/is_ack, /*opaque=*/0);
    char expect_buffer[kFrameHeaderSize + 1] = {};
    SliceBuffer sb;
    sb.Append(Slice(grpc_slice_copy(expect_slice.c_slice())));
    sb.CopyFirstNBytesIntoBuffer(kFrameHeaderSize, expect_buffer);
    EXPECT_EQ(absl::string_view(out_buffer, kFrameHeaderSize),
              absl::string_view(expect_buffer, kFrameHeaderSize));

    // Extract Opaque ID from payload
    auto mutable_slice = buffer.JoinIntoSlice().TakeMutable();
    uint8_t* opaque_id_ptr = mutable_slice.data();
    return Read8b(opaque_id_ptr + kFrameHeaderSize);
  }

  ClientMetadataHandle TestInitialMetadata() {
    ClientMetadataHandle md = Arena::MakePooledForOverwrite<ClientMetadata>();
    md->Set(HttpPathMetadata(), Slice::FromStaticString("/demo.Service/Step"));
    return md;
  }

  transport::testing::Http2FrameTestHelper helper_;
  std::shared_ptr<util::testing::EventSequenceEndpoint> endpoint_;
  PostMortem postmortem_;

 private:
  uint64_t Read8b(const uint8_t* input) const {
    return static_cast<uint64_t>(input[0]) << 56 |
           static_cast<uint64_t>(input[1]) << 48 |
           static_cast<uint64_t>(input[2]) << 40 |
           static_cast<uint64_t>(input[3]) << 32 |
           static_cast<uint64_t>(input[4]) << 24 |
           static_cast<uint64_t>(input[5]) << 16 |
           static_cast<uint64_t>(input[6]) << 8 |
           static_cast<uint64_t>(input[7]);
  }
};

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_COMMON_TEST_INPUTS_H
