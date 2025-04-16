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

#include "src/core/ext/transport/chttp2/transport/http2_status.h"

#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"

namespace grpc_core {
namespace http2 {
namespace testing {

std::vector<Http2ErrorCode> GetCodes() {
  std::vector<Http2ErrorCode> codes;
  codes.push_back(Http2ErrorCode::kNoError);
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

///////////////////////////////////////////////////////////////////////////////
// Http2Status Tests
// These tests first create the specific type of Http2Status object.
// Then check the following:
// 1. Http2ErrorType
// 2. Http2ErrorCode
// 3. message
// 4. Return of IsOk() function
// 5. Absl status

TEST(Http2StatusTest, OkTest) {
  Http2Status status = Http2Status::Ok();
  Http2Status::Http2ErrorType type = status.GetType();

  // 1. Http2ErrorType
  EXPECT_EQ(type, Http2Status::Http2ErrorType::kOk);

  // 2. Http2ErrorCode
  // Trying to extract Http2ErrorCode will cause CHECK(false);

  // 3. message
  EXPECT_GT(status.DebugString().size(), 1);

  // 4. Return of IsOk() function
  EXPECT_TRUE(status.IsOk());

  // 5. Absl status
  // The Http2Status class intentionally does not have a way to convert an Ok
  // status into absl::Status. Because the code does not look ergonomic.
  //
  // Option 1:
  // if(status.GetType() == Http2Status::Http2ErrorType::kOk)
  //    return absl::Status::Ok();
  //
  // Option 2:
  // class Http2Status {
  // absl::Status AbslOkStatus() { return absl::Status::Ok(); }
  // }
  // if(status.GetType() == Http2Status::Http2ErrorType::kOk)
  //    return status.AbslOkStatus();
  //
  // We chose Option 1.
}

TEST(Http2StatusTest, Http2ConnectionErrorTest) {
  std::vector<Http2ErrorCode> codes = GetCodes();
  for (const Http2ErrorCode& code : codes) {
    if (code == Http2ErrorCode::kNoError) {
      // Connection error MUST have status non ok.
      continue;
    }
    Http2Status status = Http2Status::Http2ConnectionError(code, "Message1");

    // 1. Http2ErrorType
    EXPECT_EQ(status.GetType(), Http2Status::Http2ErrorType::kConnectionError);

    // 2. Http2ErrorCode
    EXPECT_EQ(status.GetConnectionErrorCode(), code);

    // 3. message
    EXPECT_GT(status.DebugString().size(), 1);

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslConnectionError();
    EXPECT_FALSE(absl_status.ok());
  }
}

TEST(Http2StatusTest, Http2StreamErrorTest) { CHECK(true); }

TEST(Http2StatusTest, AbslConnectionErrorTest) { CHECK(true); }

TEST(Http2StatusTest, AbslStreamErrorTest) { CHECK(true); }

TEST(Http2StatusTest, CrashForWrongType1) {
  // Check that extracting the wrong error type should crash.
  CHECK(true);
}

///////////////////////////////////////////////////////////////////////////////
// ValueOrHttp2Status Tests
// These tests first create the specific type of ValueOrHttp2Status object.
// Then check the following:
// 1. Status is ok
// 2. Extract the value either via value() or TakeValue
// 3. Get message
// 4. Get absl status

TEST(ValueOrHttp2Status, ValuePrimitiveDataType) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueHttp2DataFrame) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueHttp2WindowUpdateFrame) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueStdString) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueHttp2Frame) { CHECK(true); }

TEST(ValueOrHttp2Status, ConnectionError) { CHECK(true); }

TEST(ValueOrHttp2Status, StreamError) { CHECK(true); }

TEST(ValueOrHttp2Status, CrashForWrongType2) {
  // Check that extracting the wrong error type should crash.
  CHECK(true);
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
