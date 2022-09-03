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

#include <string>

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

// Test that userinfo of proxy is correctly parsed
// https://github.com/grpc/grpc/issues/26548
TEST(ProxyTest, UserInfo) {
  std::map<std::string, std::string> test_cases = {
    // echo -n user:pass | base64
    {"user:pass", "dXNlcjpwYXNz"},
    // echo -n user@google.com:pass | base64
    {"user%40google.com:pass", "dXNlckBnb29nbGUuY29tOnBhc3M="},
  };
  for (const auto& test_case : test_cases) {
    auto args = ChannelArgs().Set(GRPC_ARG_HTTP_PROXY, absl::StrCat("http://", test_case.first, "@proxy.google.com"));
    EXPECT_EQ(HttpProxyMapper().MapName("dns:///test.google.com:443", &args),
              "proxy.google.com");
    EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER),
              "test.google.com:443");
    EXPECT_EQ(args.GetString(GRPC_ARG_HTTP_CONNECT_HEADERS),
              absl::StrCat("Proxy-Authorization:Basic ", test_case.second));
  }
}

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
  absl::optional<std::string> name_to_resolve;
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
