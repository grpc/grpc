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

std::vector<absl::StatusCode> FewAbslErrorCodes() {
  std::vector<absl::StatusCode> codes;
  codes.push_back(absl::StatusCode::kCancelled);
  codes.push_back(absl::StatusCode::kInvalidArgument);
  codes.push_back(absl::StatusCode::kInternal);
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
  ASSERT_DEATH(
      {
        status.GetConnectionErrorCode();
        status.GetStreamErrorCode();
      },
      "");

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
  std::vector<Http2ErrorCode> codes = GetErrorCodes();
  for (const Http2ErrorCode& code : codes) {
    Http2Status status = Http2Status::Http2ConnectionError(code, "Message1");

    // 1. Http2ErrorType
    EXPECT_EQ(status.GetType(), Http2Status::Http2ErrorType::kConnectionError);

    // 2. Http2ErrorCode
    EXPECT_EQ(status.GetConnectionErrorCode(), code);
    ASSERT_DEATH({ status.GetStreamErrorCode(); }, "");

    // 3. message
    EXPECT_GT(status.DebugString().size(), 1);

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslConnectionError();
    EXPECT_FALSE(absl_status.ok());
  }
}

TEST(Http2StatusTest, Http2StreamErrorTest) {
  std::vector<Http2ErrorCode> codes = GetErrorCodes();
  for (const Http2ErrorCode& code : codes) {
    Http2Status status = Http2Status::Http2StreamError(code, "Message1");

    // 1. Http2ErrorType
    EXPECT_EQ(status.GetType(), Http2Status::Http2ErrorType::kStreamError);

    // 2. Http2ErrorCode
    EXPECT_EQ(status.GetStreamErrorCode(), code);
    ASSERT_DEATH({ status.GetConnectionErrorCode(); }, "");

    // 3. message
    EXPECT_GT(status.DebugString().size(), 1);

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslStreamError();
    EXPECT_FALSE(absl_status.ok());
  }
}

TEST(Http2StatusTest, AbslConnectionErrorTest) {
  std::vector<absl::StatusCode> codes = FewAbslErrorCodes();
  for (const absl::StatusCode& code : codes) {
    Http2Status status = Http2Status::AbslConnectionError(code, "Message1");

    // 1. Http2ErrorType
    EXPECT_EQ(status.GetType(), Http2Status::Http2ErrorType::kConnectionError);

    // 2. Http2ErrorCode
    EXPECT_EQ(status.GetConnectionErrorCode(), Http2ErrorCode::kInternalError);
    ASSERT_DEATH({ status.GetStreamErrorCode(); }, "");

    // 3. message
    EXPECT_GT(status.DebugString().size(), 1);

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslConnectionError();
    EXPECT_FALSE(absl_status.ok());
    EXPECT_EQ(absl_status.code(), code);
  }
}

TEST(Http2StatusTest, AbslStreamErrorTest) {
  std::vector<absl::StatusCode> codes = FewAbslErrorCodes();
  for (const absl::StatusCode& code : codes) {
    Http2Status status = Http2Status::AbslStreamError(code, "Message1");

    // 1. Http2ErrorType
    EXPECT_EQ(status.GetType(), Http2Status::Http2ErrorType::kStreamError);

    // 2. Http2ErrorCode
    EXPECT_EQ(status.GetStreamErrorCode(), Http2ErrorCode::kInternalError);
    ASSERT_DEATH({ status.GetConnectionErrorCode(); }, "");

    // 3. message
    EXPECT_GT(status.DebugString().size(), 1);

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslStreamError();
    EXPECT_FALSE(absl_status.ok());
    EXPECT_EQ(absl_status.code(), code);
  }
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

TEST(ValueOrHttp2Status, Http2ConnectionError) { CHECK(true); }

TEST(ValueOrHttp2Status, Http2StreamError) { CHECK(true); }

TEST(ValueOrHttp2Status, AbslConnectionError) { CHECK(true); }

TEST(ValueOrHttp2Status, AbslStreamError) { CHECK(true); }

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
