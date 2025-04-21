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

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"

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

// Returns all Http2ErrorCode values EXCEPT kNoError
// This is because we want to test only invalid cases.
std::vector<Http2ErrorCode> GetErrorCodes() {
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
std::vector<absl::StatusCode> FewAbslErrorCodes() {
  std::vector<absl::StatusCode> codes;
  codes.push_back(absl::StatusCode::kCancelled);
  codes.push_back(absl::StatusCode::kInvalidArgument);
  codes.push_back(absl::StatusCode::kInternal);
  return codes;
}
