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

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"

namespace grpc_core {
namespace http2 {
namespace testing {

// Constructor tests first create the specific Http2Status object.
// Then check the following
// 1. Http2ErrorType
// 2. Http2ErrorCode
// 3. message
// 4. Absl status
// 5. For a small subset of tests, check that extracting the wrong
TEST(Http2StatusTest, ConstructorOkTest) {}

TEST(Http2StatusTest, ConstructorHttp2ConnectionErrorTest) {}

TEST(Http2StatusTest, ConstructorHttp2StreamErrorTest) {}

TEST(Http2StatusTest, ConstructorAbslConnectionErrorTest) {}

TEST(Http2StatusTest, ConstructorAbslStreamErrorTest) {}


}  // namespace testing
}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
