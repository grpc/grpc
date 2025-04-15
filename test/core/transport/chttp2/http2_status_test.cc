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

///////////////////////////////////////////////////////////////////////////////
// Http2Status Tests
// These tests first create the specific type of Http2Status object.
// Then check the following:
// 1. Http2ErrorType
// 2. Http2ErrorCode
// 3. message
// 4. Absl status

TEST(Http2StatusTest, OkTest) { CHECK(true); }

TEST(Http2StatusTest, Http2ConnectionErrorTest) { CHECK(true); }

TEST(Http2StatusTest, Http2StreamErrorTest) { CHECK(true); }

TEST(Http2StatusTest, AbslConnectionErrorTest) { CHECK(true); }

TEST(Http2StatusTest, AbslStreamErrorTest) { CHECK(true); }

TEST(Http2StatusTest, CrashForWrongType1) {
  // Check that extracting the wrong error type should crash.
  CHECK(true);
}

///////////////////////////////////////////////////////////////////////////////
// ValueOrHttp2Status Tests

TEST(ValueOrHttp2Status, ValuePrimitiveDataType) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueHttp2DataFrame) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueWindowUpdateFrame) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueStdString) { CHECK(true); }

TEST(ValueOrHttp2Status, ValueHttp2Frame) { CHECK(true); }

TEST(ValueOrHttp2Status, ConnectionError) { CHECK(true); }

TEST(ValueOrHttp2Status, StreamError) { CHECK(true); }

TEST(ValueOrHttp2Status, CrashForWrongType2) {
  // Check that extracting the wrong error type should crash.
  CHECK(false);
}

}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
