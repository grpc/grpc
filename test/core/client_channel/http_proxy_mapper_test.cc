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

#include <gmock/gmock.h>

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/http_proxy.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

// Test that an empty no_proxy works as expected, i.e., proxy is used.
TEST(NoProxyTest, EmptyList) {
  gpr_setenv("no_proxy", "");
  grpc_arg proxy_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_HTTP_PROXY),
      const_cast<char*>("http://proxy.google.com"));
  grpc_channel_args args = {1, &proxy_arg};
  grpc_channel_args* new_args = nullptr;
  char* name_to_resolve = nullptr;
  EXPECT_TRUE(HttpProxyMapper().MapName("dns:///test.google.com:443", &args,
                                        &name_to_resolve, &new_args));
  EXPECT_STREQ(name_to_resolve, "proxy.google.com");
  EXPECT_STREQ(grpc_channel_args_find_string(
                   new_args, const_cast<char*>(GRPC_ARG_HTTP_CONNECT_SERVER)),
               "test.google.com:443");
  gpr_free(name_to_resolve);
  grpc_channel_args_destroy(new_args);
  gpr_unsetenv("no_proxy");
}

// Test basic usage of 'no_proxy' to avoid using proxy for certain domain names.
TEST(NoProxyTest, Basic) {
  gpr_setenv("no_proxy", "google.com");
  grpc_arg proxy_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_HTTP_PROXY),
      const_cast<char*>("http://proxy.google.com"));
  grpc_channel_args args = {1, &proxy_arg};
  grpc_channel_args* new_args = nullptr;
  char* name_to_resolve = nullptr;
  EXPECT_FALSE(HttpProxyMapper().MapName("dns:///test.google.com:443", &args,
                                         &name_to_resolve, &new_args));
  EXPECT_EQ(name_to_resolve, nullptr);
  EXPECT_EQ(grpc_channel_args_find_string(
                new_args, const_cast<char*>(GRPC_ARG_HTTP_CONNECT_SERVER)),
            nullptr);
  gpr_free(name_to_resolve);
  grpc_channel_args_destroy(new_args);
  gpr_unsetenv("no_proxy");
}

// Test empty entries in 'no_proxy' list.
TEST(NoProxyTest, EmptyEntries) {
  gpr_setenv("no_proxy", "foo.com,,google.com,,");
  grpc_arg proxy_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_HTTP_PROXY),
      const_cast<char*>("http://proxy.google.com"));
  grpc_channel_args args = {1, &proxy_arg};
  grpc_channel_args* new_args = nullptr;
  char* name_to_resolve = nullptr;
  EXPECT_FALSE(HttpProxyMapper().MapName("dns:///test.google.com:443", &args,
                                         &name_to_resolve, &new_args));
  EXPECT_EQ(name_to_resolve, nullptr);
  EXPECT_EQ(grpc_channel_args_find_string(
                new_args, const_cast<char*>(GRPC_ARG_HTTP_CONNECT_SERVER)),
            nullptr);
  gpr_free(name_to_resolve);
  grpc_channel_args_destroy(new_args);
  gpr_unsetenv("no_proxy");
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
