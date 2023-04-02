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

#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

TEST(AlpnTest, TestAlpnSuccess) {
  ASSERT_TRUE(grpc_chttp2_is_alpn_version_supported("h2", 2));
  ASSERT_TRUE(grpc_chttp2_is_alpn_version_supported("grpc-exp", 8));
}

TEST(AlpnTest, TestAlpnFailure) {
  ASSERT_FALSE(grpc_chttp2_is_alpn_version_supported("h2-155", 6));
  ASSERT_FALSE(grpc_chttp2_is_alpn_version_supported("h1-15", 5));
}

// First index in ALPN supported version list of a given protocol. Returns a
// value one beyond the last valid element index if not found.
static size_t alpn_version_index(const char* version, size_t size) {
  size_t i;
  for (i = 0; i < grpc_chttp2_num_alpn_versions(); ++i) {
    if (!strncmp(version, grpc_chttp2_get_alpn_version_index(i), size)) {
      return i;
    }
  }
  return i;
}

TEST(AlpnTest, TestAlpnGrpcBeforeH2) {
  // grpc-exp is preferred over h2.
  ASSERT_LT(alpn_version_index("grpc-exp", 8), alpn_version_index("h2", 2));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
