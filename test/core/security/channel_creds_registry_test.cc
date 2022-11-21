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

#include "src/core/lib/security/credentials/channel_creds_registry.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class TestChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "test"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }
};

class ChannelCredsRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CoreConfiguration::Reset();
    grpc_init();
  }
};

TEST_F(ChannelCredsRegistryTest, DefaultCreds) {
  // Default creds.
  EXPECT_TRUE(CoreConfiguration::Get().channel_creds_registry().IsSupported(
      "google_default"));
  EXPECT_TRUE(CoreConfiguration::Get().channel_creds_registry().IsSupported(
      "insecure"));
  EXPECT_TRUE(
      CoreConfiguration::Get().channel_creds_registry().IsSupported("fake"));

  // Non-default creds.
  EXPECT_EQ(
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          "test", Json()),
      nullptr);
  EXPECT_EQ(
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          "", Json()),
      nullptr);
}

TEST_F(ChannelCredsRegistryTest, Register) {
  // Before registration.
  EXPECT_FALSE(
      CoreConfiguration::Get().channel_creds_registry().IsSupported("test"));
  EXPECT_EQ(
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          "test", Json()),
      nullptr);

  // Registration.
  CoreConfiguration::WithSubstituteBuilder builder(
      [](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->channel_creds_registry()->RegisterChannelCredsFactory(
            std::make_unique<TestChannelCredsFactory>());
      });

  RefCountedPtr<grpc_channel_credentials> test_cred(
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          "test", Json()));
  EXPECT_TRUE(
      CoreConfiguration::Get().channel_creds_registry().IsSupported("test"));
  EXPECT_NE(test_cred.get(), nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  return result;
}
