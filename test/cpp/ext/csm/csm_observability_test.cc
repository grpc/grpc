//
//
// Copyright 2023 gRPC authors.
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

#include "src/cpp/ext/csm/csm_observability.h"

#include "google/cloud/opentelemetry/resource_detector.h"
#include "gtest/gtest.h"

#include <grpcpp/ext/csm_observability.h>
#include <grpcpp/ext/otel_plugin.h>

#include "src/core/ext/xds/xds_enabled_server.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/env.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

TEST(CsmObservabilityBuilderTest, Basic) {
  EXPECT_EQ(experimental::CsmObservabilityBuilder().BuildAndRegister().status(),
            absl::OkStatus());
}

TEST(GsmDependencyTest, GoogleCloudOpenTelemetryDependency) {
  EXPECT_NE(google::cloud::otel::MakeResourceDetector(), nullptr);
}

class CsmChannelTargetSelectorTest : public ::testing::Test {
 protected:
  ~CsmChannelTargetSelectorTest() override {
    grpc_core::CoreConfiguration::Reset();
  }
};

TEST_F(CsmChannelTargetSelectorTest, NonXdsTargets) {
  auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister();
  EXPECT_FALSE(internal::CsmChannelTargetSelector("foo.bar.google.com"));
  EXPECT_FALSE(internal::CsmChannelTargetSelector("dns:///foo.bar.google.com"));
  EXPECT_FALSE(
      internal::CsmChannelTargetSelector("dns:///foo.bar.google.com:1234"));
  EXPECT_FALSE(internal::CsmChannelTargetSelector(
      "dns://authority/foo.bar.google.com:1234"));
}

TEST_F(CsmChannelTargetSelectorTest, XdsTargets) {
  auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister();
  EXPECT_TRUE(internal::CsmChannelTargetSelector("xds:///foo"));
  EXPECT_TRUE(internal::CsmChannelTargetSelector("xds:///foo.bar"));
}

TEST_F(CsmChannelTargetSelectorTest, XdsTargetsWithNonTDAuthority) {
  auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister();
  EXPECT_FALSE(internal::CsmChannelTargetSelector("xds://authority/foo"));
}

TEST_F(CsmChannelTargetSelectorTest, XdsTargetsWithTDAuthority) {
  auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister();
  EXPECT_TRUE(internal::CsmChannelTargetSelector(
      "xds://traffic-director-global.xds.googleapis.com/foo"));
}

TEST_F(CsmChannelTargetSelectorTest, CsmObservabilityOutOfScope) {
  { auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister(); }
  // When CsmObservability goes out of scope, the target selector should return
  // false as well.
  EXPECT_FALSE(internal::CsmChannelTargetSelector("foo.bar.google.com"));
  EXPECT_FALSE(internal::CsmChannelTargetSelector("xds:///foo"));
  EXPECT_FALSE(internal::CsmChannelTargetSelector(
      "xds://traffic-director-global.xds.googleapis.com/foo"));
}

using CsmServerSelectorTest = CsmChannelTargetSelectorTest;

TEST_F(CsmServerSelectorTest, ChannelArgsWithoutXdsServerArg) {
  auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister();
  EXPECT_FALSE(internal::CsmServerSelector(grpc_core::ChannelArgs()));
}

TEST_F(CsmServerSelectorTest, ChannelArgsWithXdsServerArg) {
  auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister();
  EXPECT_TRUE(internal::CsmServerSelector(
      grpc_core::ChannelArgs().Set(GRPC_ARG_XDS_ENABLED_SERVER, true)));
}

TEST_F(CsmServerSelectorTest, CsmObservabilityOutOfScope) {
  { auto obs = experimental::CsmObservabilityBuilder().BuildAndRegister(); }
  // When CsmObservability goes out of scope, the server selector should return
  // false as well.
  EXPECT_FALSE(internal::CsmServerSelector(grpc_core::ChannelArgs()));
  EXPECT_FALSE(internal::CsmServerSelector(
      grpc_core::ChannelArgs().Set(GRPC_ARG_XDS_ENABLED_SERVER, true)));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
