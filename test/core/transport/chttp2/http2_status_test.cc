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
// 3. DebugString
// 4. Return of IsOk() function
// 5. Absl status

TEST(Http2StatusTest, OkTest) {
  Http2Status status = Http2Status::Ok();

  // 1. Http2ErrorType
  Http2Status::Http2ErrorType type = status.GetType();
  EXPECT_EQ(type, Http2Status::Http2ErrorType::kOk);

  // 2. Http2ErrorCode
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code1 = status.GetConnectionErrorCode(); },
      "");
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code2 = status.GetStreamErrorCode(); }, "");

  // 3. DebugString
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

    // 3. DebugString
    EXPECT_STREQ(status.DebugString().c_str(), "Connection Error: Message1");

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslConnectionError();
    EXPECT_FALSE(absl_status.ok());
    EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");
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

    // 3. DebugString
    EXPECT_STREQ(status.DebugString().c_str(), "Stream Error: Message1");

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslStreamError();
    EXPECT_FALSE(absl_status.ok());
    EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");
  }
}

TEST(Http2StatusTest, Http2ConnectionCrashOnOk) {
  ASSERT_DEATH(
      {
        GRPC_UNUSED Http2Status status = Http2Status::Http2ConnectionError(
            Http2ErrorCode::kNoError, "Message1");
      },
      "");
}

TEST(Http2StatusTest, Http2StreamCrashOnOk) {
  ASSERT_DEATH(
      {
        GRPC_UNUSED Http2Status status =
            Http2Status::Http2StreamError(Http2ErrorCode::kNoError, "Message1");
      },
      "");
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

    // 3. DebugString
    EXPECT_STREQ(status.DebugString().c_str(), "Connection Error: Message1");

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslConnectionError();
    EXPECT_FALSE(absl_status.ok());
    EXPECT_EQ(absl_status.code(), code);
    EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");
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

    // 3. DebugString
    EXPECT_STREQ(status.DebugString().c_str(), "Stream Error: Message1");

    // 4. Return of IsOk() function
    EXPECT_FALSE(status.IsOk());

    // 5. Absl status
    absl::Status absl_status = status.GetAbslStreamError();
    EXPECT_FALSE(absl_status.ok());
    EXPECT_EQ(absl_status.code(), code);
    EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");
  }
}

///////////////////////////////////////////////////////////////////////////////
// ValueOrHttp2Status Tests - Values
// These tests first create the specific type of ValueOrHttp2Status object.
// Then check the following:
// 1. IsOk() is false
// 2. Value

TEST(ValueOrHttp2StatusTest, ValuePrimitiveDataType) {
  auto test_lambda = []() -> ValueOrHttp2Status<int> {
    return ValueOrHttp2Status<int>(100);
  };
  ValueOrHttp2Status<int> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_TRUE(result.IsOk());

  // 2. Value
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

  // 2. Value
  SliceBuffer result3 = TakeValue(std::move(result));
  EXPECT_EQ(result3.Length(), 1024);
  EXPECT_STREQ(result3.JoinIntoString().c_str(), std::string(kStr1024).c_str());
}

TEST(ValueOrHttp2StatusTest, ValueHttp2WindowUpdateFrame) {
  auto test_lambda = []() -> ValueOrHttp2Status<Http2WindowUpdateFrame> {
    return ValueOrHttp2Status<Http2WindowUpdateFrame>(
        Http2WindowUpdateFrame{0, 100});
  };
  ValueOrHttp2Status<Http2WindowUpdateFrame> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_TRUE(result.IsOk());

  // 2. Value
  Http2WindowUpdateFrame result3 = result.value();
  EXPECT_EQ(result3.stream_id, 0);
  EXPECT_EQ(result3.increment, 100);
}

TEST(ValueOrHttp2StatusTest, ValueHttp2DataFrame) {
  auto test_lambda = []() -> ValueOrHttp2Status<Http2DataFrame> {
    SliceBuffer temp;
    temp.Append(Slice::FromCopiedString(kStr1024));
    Http2DataFrame frame = Http2DataFrame{1, false, std::move(temp)};
    return ValueOrHttp2Status<Http2DataFrame>(std::move(frame));
  };
  ValueOrHttp2Status<Http2DataFrame> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_TRUE(result.IsOk());

  // 2. Value
  Http2DataFrame result3 = TakeValue(std::move(result));
  EXPECT_EQ(result3.stream_id, 1);
  EXPECT_EQ(result3.end_stream, false);
  EXPECT_STREQ(result3.payload.JoinIntoString().c_str(),
               std::string(kStr1024).c_str());
}

///////////////////////////////////////////////////////////////////////////////
// ValueOrHttp2Status Tests - Errors
// These tests first create the specific type of ValueOrHttp2Status object.
// Then check the following:
// 1. IsOk() is false
// 2. Http2ErrorType
// 3. Http2ErrorCode
// 4. Absl status
// 5. DebugString

TEST(ValueOrHttp2StatusTest, Http2ConnectionError) {
  auto test_lambda = []() -> ValueOrHttp2Status<int> {
    return ValueOrHttp2Status<int>(Http2Status::Http2ConnectionError(
        Http2ErrorCode::kProtocolError, "Message1"));
  };
  ValueOrHttp2Status<int> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_FALSE(result.IsOk());

  // 2. Http2ErrorType
  EXPECT_EQ(result.GetErrorType(),
            Http2Status::Http2ErrorType::kConnectionError);

  // 3. Http2ErrorCode
  EXPECT_EQ(result.GetConnectionErrorCode(), Http2ErrorCode::kProtocolError);
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code1 = result.GetStreamErrorCode(); }, "");

  // 4. Absl status
  absl::Status absl_status = result.GetAbslConnectionError();
  EXPECT_FALSE(absl_status.ok());
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslStreamError(); }, "");
  EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");

  // 5. DebugString
  std::string message = result.DebugString();
  EXPECT_STREQ(message.c_str(), "Connection Error: Message1");
}

TEST(ValueOrHttp2StatusTest, Http2StreamError) {
  auto test_lambda = []() -> ValueOrHttp2Status<std::string> {
    return ValueOrHttp2Status<std::string>(Http2Status::Http2StreamError(
        Http2ErrorCode::kProtocolError, "Message1"));
  };
  ValueOrHttp2Status<std::string> result = test_lambda();

  // 1. IsOk() is false
  EXPECT_FALSE(result.IsOk());

  // 2. Http2ErrorType
  EXPECT_EQ(result.GetErrorType(), Http2Status::Http2ErrorType::kStreamError);

  // 3. Http2ErrorCode
  EXPECT_EQ(result.GetStreamErrorCode(), Http2ErrorCode::kProtocolError);
  ASSERT_DEATH(
      { GRPC_UNUSED Http2ErrorCode code1 = result.GetConnectionErrorCode(); },
      "");

  // 4. Absl status
  absl::Status absl_status = result.GetAbslStreamError();
  EXPECT_FALSE(absl_status.ok());
  ASSERT_DEATH(
      { GRPC_UNUSED absl::Status result1 = result.GetAbslConnectionError(); },
      "");
  EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");

  // 5. DebugString
  std::string message = result.DebugString();
  EXPECT_STREQ(message.c_str(), "Stream Error: Message1");
}

TEST(ValueOrHttp2StatusTest, AbslConnectionError) {
  auto test_lambda = []() -> ValueOrHttp2Status<std::string> {
    return ValueOrHttp2Status<std::string>(Http2Status::AbslConnectionError(
        absl::StatusCode::kCancelled, "Message1"));
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
  EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");

  // 5. DebugString
  std::string message = result.DebugString();
  EXPECT_STREQ(message.c_str(), "Connection Error: Message1");
}

TEST(ValueOrHttp2StatusTest, AbslStreamError) {
  auto test_lambda = []() -> ValueOrHttp2Status<std::string> {
    return ValueOrHttp2Status<std::string>(
        Http2Status::AbslStreamError(absl::StatusCode::kCancelled, "Message1"));
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
  EXPECT_STREQ(std::string(absl_status.message()).c_str(), "Message1");

  // 5. DebugString
  std::string message = result.DebugString();
  EXPECT_STREQ(message.c_str(), "Stream Error: Message1");
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
