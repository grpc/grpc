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

#include "src/core/lib/gprpp/env.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

TEST(CsmObservabilityBuilderTest, Basic) {
  EXPECT_TRUE(
      experimental::CsmObservabilityBuilder().BuildAndRegister().status().ok());
}

TEST(GsmDependencyTest, GoogleCloudOpenTelemetryDependency) {
  EXPECT_NE(google::cloud::otel::MakeResourceDetector(), nullptr);
}

TEST(CsmChannelTargetSelectorTest, NonXdsTargets) {
  EXPECT_FALSE(internal::CsmChannelTargetSelector("foo.bar.google.com"));
  EXPECT_FALSE(internal::CsmChannelTargetSelector("dns:///foo.bar.google.com"));
  EXPECT_FALSE(
      internal::CsmChannelTargetSelector("dns:///foo.bar.google.com:1234"));
  EXPECT_FALSE(internal::CsmChannelTargetSelector(
      "dns://authority/foo.bar.google.com:1234"));
}

TEST(CsmChannelTargetSelectorTest, XdsTargets) {
  EXPECT_TRUE(internal::CsmChannelTargetSelector("xds:///foo"));
  EXPECT_TRUE(internal::CsmChannelTargetSelector("xds:///foo.bar"));
}

TEST(CsmChannelTargetSelectorTest, XdsTargetsWithNonTDAuthority) {
  EXPECT_FALSE(internal::CsmChannelTargetSelector("xds://authority/foo"));
}

TEST(CsmChannelTargetSelectorTest, XdsTargetsWithTDAuthority) {
  EXPECT_TRUE(internal::CsmChannelTargetSelector(
      "xds://traffic-director-global.xds.googleapis.com/foo"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
