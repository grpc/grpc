//
//
// Copyright 2022 gRPC authors.
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

#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/http_proxy.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/transport/http_connect_handshaker.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class ScopedSetEnv {
 public:
  explicit ScopedSetEnv(const char* value) { SetEnv("no_proxy", value); }
  ScopedSetEnv(const ScopedSetEnv&) = delete;
  ScopedSetEnv& operator=(const ScopedSetEnv&) = delete;
  ~ScopedSetEnv() { UnsetEnv("no_proxy"); }
};

// Test that an empty no_proxy works as expected, i.e., proxy is used.
TEST(NoProxyTest, EmptyList) {
  ScopedSetEnv no_proxy("");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            "proxy.google.com");
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER),
            "test.google.com:443");
}

// Test basic usage of 'no_proxy' to avoid using proxy for certain domain names.
TEST(NoProxyTest, Basic) {
  ScopedSetEnv no_proxy("google.com");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
}

// Test empty entries in 'no_proxy' list.
TEST(NoProxyTest, EmptyEntries) {
  ScopedSetEnv no_proxy("foo.com,,google.com,,");
  auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, "http://proxy.google.com");
  EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
            absl::nullopt);
  EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER), absl::nullopt);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
