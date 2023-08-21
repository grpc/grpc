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

#include "src/cpp/ext/gsm/gsm_observability.h"

#include "google/cloud/opentelemetry/resource_detector.h"
#include "gtest/gtest.h"

#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

TEST(GsmCustomObservabilityBuilderTest, Basic) {
  EXPECT_EQ(
      internal::GsmCustomObservabilityBuilder().BuildAndRegister().status(),
      absl::UnimplementedError("Not Implemented"));
}

TEST(GsmDependencyTest, GoogleCloudOpenTelemetryDependency) {
  EXPECT_NE(google::cloud::otel::MakeResourceDetector(), nullptr);
}

TEST(ResourceDetectionTest, GkeResourceDetection) {
  auto resource = google::cloud::otel::MakeResourceDetector()->Detect();
  const auto& attributes = resource.GetAttributes().GetAttributes();
  EXPECT_EQ(absl::get<std::string>(attributes.at("cloud.provider")), "gcp");
  EXPECT_EQ(absl::get<std::string>(attributes.at("cloud.platform")),
            "gcp_kubernetes_engine");
  // We don't know what the value will be here
  EXPECT_FALSE(
      absl::get<std::string>(attributes.at("cloud.account.id")).empty());
  EXPECT_EQ(absl::get<std::string>(attributes.at("k8s.pod.name")), "pod");
  EXPECT_EQ(absl::get<std::string>(attributes.at("k8s.namespace.name")),
            "namespace");
  EXPECT_EQ(absl::get<std::string>(attributes.at("k8s.container.name")),
            "container");
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::SetEnv("KUBERNETES_SERVICE_HOST", "service_host");
  grpc_core::SetEnv("OTEL_RESOURCE_ATTRIBUTES",
                    "k8s.pod.name=pod,k8s.namespace.name=namespace,k8s."
                    "container.name=container");
  return RUN_ALL_TESTS();
}
