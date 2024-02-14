//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/ext/transport/chttp2/alpn/alpn.h"

#include <memory>

#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

TEST(AlpnTest, TestAlpnSuccess) {
  ASSERT_TRUE(grpc_chttp2_is_alpn_version_supported("h2", 2));
}

TEST(AlpnTest, TestAlpnFailure) {
  ASSERT_FALSE(grpc_chttp2_is_alpn_version_supported("h2-155", 6));
  ASSERT_FALSE(grpc_chttp2_is_alpn_version_supported("h1-15", 5));
  ASSERT_FALSE(grpc_chttp2_is_alpn_version_supported("grpc-exp", 8));
  ASSERT_FALSE(grpc_chttp2_is_alpn_version_supported("h", 1));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
