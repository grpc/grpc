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
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class TestXdsChannelCredsFactory : public XdsChannelCredsFactory {
 public:
  absl::string_view creds_type() const override { return "test"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  grpc_channel_credentials* CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return grpc_fake_transport_security_credentials_create();
  }
};

TEST(XdsChannelCredsRegistry2Test, DefaultCreds) {
  // Default creds.
  EXPECT_TRUE(CoreConfiguration::Get().xds_channel_creds_registry().IsSupported(
      "google_default"));
  EXPECT_TRUE(CoreConfiguration::Get().xds_channel_creds_registry().IsSupported(
      "insecure"));
  EXPECT_TRUE(CoreConfiguration::Get().xds_channel_creds_registry().IsSupported(
      "fake"));

  // Non-default creds.
  EXPECT_EQ(CoreConfiguration::Get()
                .xds_channel_creds_registry()
                .CreateXdsChannelCreds("test", Json()),
            nullptr);
  EXPECT_EQ(CoreConfiguration::Get()
                .xds_channel_creds_registry()
                .CreateXdsChannelCreds("", Json()),
            nullptr);
}

TEST(XdsChannelCredsRegistry2Test, Register) {
  CoreConfiguration::Reset();
  grpc_init();

  // Before registration.
  EXPECT_FALSE(
      CoreConfiguration::Get().xds_channel_creds_registry().IsSupported(
          "test"));
  EXPECT_EQ(CoreConfiguration::Get()
                .xds_channel_creds_registry()
                .CreateXdsChannelCreds("test", Json()),
            nullptr);

  // Registration.
  CoreConfiguration::BuildSpecialConfiguration(
      [](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->xds_channel_creds_registry()->RegisterXdsChannelCredsFactory(
            absl::make_unique<TestXdsChannelCredsFactory>());
      });

  RefCountedPtr<grpc_channel_credentials> test_cred(
      CoreConfiguration::Get()
          .xds_channel_creds_registry()
          .CreateXdsChannelCreds("test", Json()));
  EXPECT_TRUE(CoreConfiguration::Get().xds_channel_creds_registry().IsSupported(
      "test"));
  EXPECT_NE(test_cred.get(), nullptr);
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
