//
//
// Copyright 2016 gRPC authors.
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

#include "gtest/gtest.h"

#include "test/core/handshake/server_ssl_common.h"
#include "test/core/util/test_config.h"

TEST(ServerSslTest, MainTest) {
  // Handshake succeeeds when the client supplies only h2 as the ALPN list. This
  // covers legacy gRPC clients which don't support grpc-exp.
  const char* h2_only_alpn_list[] = {"h2"};
  ASSERT_TRUE(server_ssl_test(h2_only_alpn_list, 1, "h2"));
  // Handshake succeeds when the client supplies superfluous ALPN entries and
  // also when h2 is included.
  const char* extra_alpn_list[] = {"foo", "h2", "bar"};
  ASSERT_TRUE(server_ssl_test(extra_alpn_list, 3, "h2"));
  // Handshake fails when the client uses a fake protocol as its only ALPN
  // preference. This validates the server is correctly validating ALPN
  // and sanity checks the server_ssl_test.
  const char* fake_alpn_list[] = {"foo"};
  ASSERT_FALSE(server_ssl_test(fake_alpn_list, 1, "foo"));
  CleanupSslLibrary();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
