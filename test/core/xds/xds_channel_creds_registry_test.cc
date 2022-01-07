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
#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_channel_creds.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class TestXdsChannelCredsImpl : public XdsChannelCredsImpl {
 public:
  RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  absl::string_view creds_type() const override { return "test"; }
};

TEST(XdsChannelCredsRegistryTest, DefaultCreds) {  // Default creds.
  EXPECT_TRUE(XdsChannelCredsRegistry::IsSupported("google_default"));
  EXPECT_TRUE(XdsChannelCredsRegistry::IsSupported("insecure"));
  EXPECT_TRUE(XdsChannelCredsRegistry::IsSupported("fake"));

  // Non-default creds.
  EXPECT_EQ(XdsChannelCredsRegistry::CreateXdsChannelCreds("test", Json()),
            nullptr);
  EXPECT_EQ(XdsChannelCredsRegistry::CreateXdsChannelCreds("", Json()),
            nullptr);
}

TEST(XdsChannelCredsRegistryTest, Register) {
  // Before registration.
  EXPECT_FALSE(XdsChannelCredsRegistry::IsSupported("test"));
  EXPECT_EQ(XdsChannelCredsRegistry::CreateXdsChannelCreds("test", Json()),
            nullptr);

  // Registration.
  XdsChannelCredsRegistry::RegisterXdsChannelCreds(
      absl::make_unique<TestXdsChannelCredsImpl>());
  EXPECT_NE(XdsChannelCredsRegistry::CreateXdsChannelCreds("test", Json()),
            nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  return result;
}
