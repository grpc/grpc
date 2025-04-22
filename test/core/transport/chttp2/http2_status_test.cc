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
#include "test/core/transport/chttp2/http2_common_test_inputs.h"

namespace grpc_core {
namespace http2 {
namespace testing {

TEST(Http2StatusTest, MoveTest) {
  Http2Status old_status = Http2Status::Http2ConnectionError(
      Http2ErrorCode::kProtocolError, "Message1");
  EXPECT_GT(old_status.DebugString().size(), 1);
  auto test_lambda = [new_status = std::move(old_status)]() {
    LOG(INFO) << new_status.DebugString();
    EXPECT_GT(new_status.DebugString().size(), 1);
  };
  test_lambda();
}

TEST(Http2StatusTest, ReturnTest) {
  auto test_lambda = []() -> Http2Status {
    constexpr absl::string_view kMessage = "Message2";
    Http2Status status1 = Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError, std::string(kMessage));
    EXPECT_GT(status1.DebugString().size(), 1);
    return status1;
  };

  Http2Status status2 = test_lambda();
  EXPECT_GT(status2.DebugString().size(), 1);
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
      { GRPC_UNUSED Http2ErrorCode code1 = status.GetConnectionErrorCode(); },
      "");
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code2 = status.GetStreamErrorCode(); }, "");

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
    ASSERT_DEATH(
        { GRPC_UNUSED Http2ErrorCode code1 = status.GetStreamErrorCode(); },
        "");

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
    ASSERT_DEATH(
        { GRPC_UNUSED Http2ErrorCode code1 = status.GetConnectionErrorCode(); },
        "");

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
    ASSERT_DEATH(
        { GRPC_UNUSED Http2ErrorCode code1 = status.GetStreamErrorCode(); },
        "");

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
    ASSERT_DEATH(
        { GRPC_UNUSED Http2ErrorCode code1 = status.GetConnectionErrorCode(); },
        "");

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
// ValueOrHttp2Status Tests - Values
// These tests first create the specific type of ValueOrHttp2Status object.
// Then check the following:
// 1. Status is ok
// 2. Extract the value either via value() or TakeValue
// 3. Get message
// 4. Get absl status

TEST(ValueOrHttp2StatusTest, ValuePrimitiveDataType) {
  auto test_lambda = []() -> ValueOrHttp2Status<int> {
    return ValueOrHttp2Status<int>(100);
  };
  ValueOrHttp2Status<int> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_TRUE(result.IsOk());

  // 2. Http2ErrorType
  ASSERT_DEATH(
      { GRPC_UNUSED Http2Status::Http2ErrorType type = result.GetErrorType(); },
      "");

  // 3. Http2ErrorCode
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code1 = result.GetConnectionErrorCode(); },
      "");
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code2 = result.GetStreamErrorCode(); }, "");

  // 4. Absl status
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslConnectionError(); },
      "");
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result2 = result.GetAbslStreamError(); }, "");

  // 5. Value
  int result1 = result.value();
  EXPECT_EQ(result1, 100);
}

TEST(ValueOrHttp2StatusTest, ValueSliceBuffer) {
  auto test_lambda = []() -> ValueOrHttp2Status<SliceBuffer> {
    SliceBuffer payload;
    payload.Append(Slice::FromCopiedString(kStr1024));
    return ValueOrHttp2Status<SliceBuffer>(std::move(payload));
  };
  ValueOrHttp2Status<SliceBuffer> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_TRUE(result.IsOk());

  // 2. Http2ErrorType
  ASSERT_DEATH(
      { GRPC_UNUSED Http2Status::Http2ErrorType type = result.GetErrorType(); },
      "");

  // 3. Http2ErrorCode
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code1 = result.GetConnectionErrorCode(); },
      "");
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code2 = result.GetStreamErrorCode(); }, "");

  // 4. Absl status
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslConnectionError(); },
      "");
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result2 = result.GetAbslStreamError(); }, "");

  // 5. Value
  SliceBuffer result3 = TakeValue(std::move(result));
  EXPECT_EQ(result3.Length(), 1024);
  EXPECT_STREQ(result3.JoinIntoString().c_str(), std::string(kStr1024).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// ValueOrHttp2Status Tests - Errors
// These tests first create the specific type of ValueOrHttp2Status object.
// Then check the following:
// 1. IsOk() is false
// 2. Http2ErrorType
// 3. Http2ErrorCode
// 4. Absl status

// TODO(tjagtap): [PH2][P0] : some http2 frame types used to give some
// compile issue with std::move. Check with tests.

TEST(ValueOrHttp2StatusTest, Http2ConnectionError) {
  const Http2ErrorCode code = Http2ErrorCode::kProtocolError;

  auto test_lambda = []() -> ValueOrHttp2Status<int> {
    return ValueOrHttp2Status<int>(
        Http2Status::Http2ConnectionError(code, "Message1"));
  };
  ValueOrHttp2Status<int> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_FALSE(result.IsOk());

  // 2. Http2ErrorType
  EXPECT_EQ(result.GetErrorType(),
            Http2Status::Http2ErrorType::kConnectionError);

  // 3. Http2ErrorCode
  EXPECT_EQ(result.GetConnectionErrorCode(), code);
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code1 = result.GetStreamErrorCode(); }, "");

  // 4. Absl status
  absl::Status absl_status = result.GetAbslConnectionError();
  EXPECT_FALSE(absl_status.ok());
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslStreamError(); }, "");

  // 5. message
  std::string message = result.DebugString();
  EXPECT_GT(message.size(), 1);
  // EXPECT_STREQ(message, "Message1");
}

TEST(ValueOrHttp2StatusTest, Http2StreamError) {
  const Http2ErrorCode code = Http2ErrorCode::kProtocolError;
  auto test_lambda = []() -> ValueOrHttp2Status<std::string> {
    return ValueOrHttp2Status<std::string>(
        Http2Status::Http2StreamError(code, "Message1"));
  };
  ValueOrHttp2Status<std::string> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_FALSE(result.IsOk());

  // 2. Http2ErrorType
  EXPECT_EQ(result.GetErrorType(), Http2Status::Http2ErrorType::kStreamError);

  // 3. Http2ErrorCode
  EXPECT_EQ(result.GetStreamErrorCode(), code);
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code1 = result.GetConnectionErrorCode(); },
      "");

  // 4. Absl status
  absl::Status absl_status = result.GetAbslStreamError();
  EXPECT_FALSE(absl_status.ok());
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslConnectionError(); },
      "");

  // 5. message
  std::string message = result.DebugString();
  EXPECT_GT(message.size(), 1);
  // EXPECT_STREQ(message, "Message1");
}

TEST(ValueOrHttp2StatusTest, AbslConnectionError) {
  constexpr absl::StatusCode code = absl::StatusCode::kCancelled;
  auto test_lambda = []() -> ValueOrHttp2Status<std::string> {
    return ValueOrHttp2Status<std::string>(
        Http2Status::AbslConnectionError(code, "Message1"));
  };
  ValueOrHttp2Status<std::string> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_FALSE(result.IsOk());

  // 2. Http2ErrorType
  EXPECT_EQ(result.GetErrorType(),
            Http2Status::Http2ErrorType::kConnectionError);

  // 3. Http2ErrorCode
  EXPECT_EQ(result.GetConnectionErrorCode(), Http2ErrorCode::kInternalError);
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code2 = result.GetStreamErrorCode(); }, "");

  // 4. Absl status
  absl::Status absl_status = result.GetAbslConnectionError();
  EXPECT_FALSE(absl_status.ok());
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslStreamError(); }, "");

  // 5. message
  std::string message = result.DebugString();
  EXPECT_GT(message.size(), 1);
  // EXPECT_STREQ(message, "Message1");
}

TEST(ValueOrHttp2StatusTest, AbslStreamError) {
  constexpr absl::StatusCode code = absl::StatusCode::kCancelled;
  auto test_lambda = []() -> ValueOrHttp2Status<std::string> {
    return ValueOrHttp2Status<std::string>(
        Http2Status::AbslStreamError(code, "Message1"));
  };
  ValueOrHttp2Status<std::string> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_FALSE(result.IsOk());

  // 2. Http2ErrorType
  EXPECT_EQ(result.GetErrorType(), Http2Status::Http2ErrorType::kStreamError);

  // 3. Http2ErrorCode
  EXPECT_EQ(result.GetStreamErrorCode(), Http2ErrorCode::kInternalError);
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code2 = result.GetConnectionErrorCode(); },
      "");

  // 4. Absl status
  absl::Status absl_status = result.GetAbslStreamError();
  EXPECT_FALSE(absl_status.ok());
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslConnectionError(); },
      "");

  // 5. message
  std::string message = result.DebugString();
  EXPECT_GT(message.size(), 1);
  // EXPECT_STREQ(message, "Message1");
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
