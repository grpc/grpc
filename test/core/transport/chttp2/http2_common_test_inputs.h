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

constexpr absl::string_view kString1 = "One Hello World!";
constexpr absl::string_view kString2 = "Two Hello World!";
constexpr absl::string_view kString3 = "Three Hello World!";

constexpr uint8_t kFlags0 = 0;
constexpr uint8_t kFlags5 = 5;

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

//  partial headers: generated from simple_request.headers
constexpr absl::string_view kSimpleRequestEncodedPart1 =
    "\x10\x05:path\x08/foo/bar"
    "\x10\x07:scheme\x04http"
    "\x10\x07:method\x04POST";

constexpr size_t kSimpleRequestEncodedPart1Len = 44;

//  partial headers: generated from simple_request.headers
constexpr absl::string_view kSimpleRequestEncodedPart2 =
    "\x10\x0a:authority\x09localhost"
    "\x10\x0c"
    "content-type\x10"
    "application/grpc";

constexpr size_t kSimpleRequestEncodedPart2Len = 53;

//  partial headers: generated from simple_request.headers
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

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_COMMON_TEST_INPUTS_H
