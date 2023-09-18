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

#include "src/cpp/ext/csm/metadata_exchange.h"

#include "absl/functional/any_invocable.h"
#include "api/include/opentelemetry/metrics/provider.h"
#include "gmock/gmock.h"
#include "google/cloud/opentelemetry/resource_detector.h"
#include "gtest/gtest.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"

#include <grpcpp/grpcpp.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/gprpp/env.h"
#include "src/cpp/ext/csm/csm_observability.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/ext/otel/otel_test_library.h"

namespace grpc {
namespace testing {
namespace {

class TestScenario {
 public:
  enum class ResourceType : std::uint8_t { kGke, kUnknown };
  enum class XdsBootstrapSource : std::uint8_t { kFromFile, kFromConfig };

  explicit TestScenario(ResourceType type, XdsBootstrapSource bootstrap_source)
      : type_(type), bootstrap_source_(bootstrap_source) {}

  opentelemetry::sdk::resource::Resource GetTestResource() const {
    switch (type_) {
      case ResourceType::kGke:
        return TestGkeResource();
      case ResourceType::kUnknown:
        return TestUnknownResource();
    }
  }

  static std::string Name(const ::testing::TestParamInfo<TestScenario>& info) {
    std::string ret_val;
    switch (info.param.type_) {
      case ResourceType::kGke:
        ret_val += "Gke";
        break;
      case ResourceType::kUnknown:
        ret_val += "Unknown";
        break;
    }
    switch (info.param.bootstrap_source_) {
      case TestScenario::XdsBootstrapSource::kFromFile:
        ret_val += "BootstrapFromFile";
        break;
      case TestScenario::XdsBootstrapSource::kFromConfig:
        ret_val += "BootstrapFromConfig";
        break;
    }
    return ret_val;
  }

  ResourceType type() const { return type_; }

  XdsBootstrapSource bootstrap_source() const { return bootstrap_source_; }

 private:
  static opentelemetry::sdk::resource::Resource TestGkeResource() {
    opentelemetry::sdk::common::AttributeMap attributes;
    attributes.SetAttribute("cloud.platform", "gcp_kubernetes_engine");
    attributes.SetAttribute("k8s.pod.name", "pod");
    attributes.SetAttribute("k8s.container.name", "container");
    attributes.SetAttribute("k8s.namespace.name", "namespace");
    attributes.SetAttribute("k8s.cluster.name", "cluster");
    attributes.SetAttribute("cloud.region", "region");
    attributes.SetAttribute("cloud.account.id", "id");
    return opentelemetry::sdk::resource::Resource::Create(attributes);
  }

  static opentelemetry::sdk::resource::Resource TestUnknownResource() {
    opentelemetry::sdk::common::AttributeMap attributes;
    attributes.SetAttribute("cloud.platform", "random");
    return opentelemetry::sdk::resource::Resource::Create(attributes);
  }

  ResourceType type_;
  XdsBootstrapSource bootstrap_source_;
};

class MetadataExchangeTest
    : public OTelPluginEnd2EndTest,
      public ::testing::WithParamInterface<TestScenario> {
 protected:
  void Init(const absl::flat_hash_set<absl::string_view>& metric_names) {
    const char* kBootstrap =
        "{\"node\": {\"id\": "
        "\"projects/1234567890/networks/mesh:mesh-id/nodes/"
        "01234567-89ab-4def-8123-456789abcdef\"}}";
    switch (GetParam().bootstrap_source()) {
      case TestScenario::XdsBootstrapSource::kFromFile: {
        ASSERT_EQ(bootstrap_file_name_, nullptr);
        FILE* bootstrap_file =
            gpr_tmpfile("gcp_observability_config", &bootstrap_file_name_);
        fputs(kBootstrap, bootstrap_file);
        fclose(bootstrap_file);
        grpc_core::SetEnv("GRPC_XDS_BOOTSTRAP", bootstrap_file_name_);
        break;
      }
      case TestScenario::XdsBootstrapSource::kFromConfig:
        grpc_core::SetEnv("GRPC_XDS_BOOTSTRAP_CONFIG", kBootstrap);
        break;
    }
    OTelPluginEnd2EndTest::Init(
        metric_names, /*resource=*/GetParam().GetTestResource(),
        /*labels_injector=*/
        std::make_unique<grpc::internal::ServiceMeshLabelsInjector>(
            GetParam().GetTestResource().GetAttributes()));
  }

  ~MetadataExchangeTest() override {
    grpc_core::UnsetEnv("GRPC_GCP_OBSERVABILITY_CONFIG");
    grpc_core::UnsetEnv("GRPC_XDS_BOOTSTRAP");
    if (bootstrap_file_name_ != nullptr) {
      remove(bootstrap_file_name_);
      gpr_free(bootstrap_file_name_);
    }
  }

  void VerifyServiceMeshAttributes(
      const std::map<std::string,
                     opentelemetry::sdk::common::OwnedAttributeValue>&
          attributes) {
    EXPECT_EQ(absl::get<std::string>(attributes.at("csm.mesh_id")), "mesh-id");
    switch (GetParam().type()) {
      case TestScenario::ResourceType::kGke:
        EXPECT_EQ(
            absl::get<std::string>(attributes.at("csm.remote_workload_type")),
            "gcp_kubernetes_engine");
        EXPECT_EQ(absl::get<std::string>(
                      attributes.at("csm.remote_workload_pod_name")),
                  "pod");
        EXPECT_EQ(absl::get<std::string>(
                      attributes.at("csm.remote_workload_container_name")),
                  "container");
        EXPECT_EQ(absl::get<std::string>(
                      attributes.at("csm.remote_workload_namespace_name")),
                  "namespace");
        EXPECT_EQ(absl::get<std::string>(
                      attributes.at("csm.remote_workload_cluster_name")),
                  "cluster");
        EXPECT_EQ(absl::get<std::string>(
                      attributes.at("csm.remote_workload_location")),
                  "region");
        EXPECT_EQ(absl::get<std::string>(
                      attributes.at("csm.remote_workload_project_id")),
                  "id");
        EXPECT_EQ(absl::get<std::string>(
                      attributes.at("csm.remote_workload_canonical_service")),
                  "canonical_service");
        break;
      case TestScenario::ResourceType::kUnknown:
        EXPECT_EQ(
            absl::get<std::string>(attributes.at("csm.remote_workload_type")),
            "random");
        break;
    }
  }

  void VerifyNoServiceMeshAttributes(
      const std::map<std::string,
                     opentelemetry::sdk::common::OwnedAttributeValue>&
          attributes) {
    EXPECT_EQ(attributes.find("csm.remote_workload_type"), attributes.end());
  }

 private:
  char* bootstrap_file_name_ = nullptr;
};

TEST_P(MetadataExchangeTest, ClientAttemptStarted) {
  Init(/*metric_names=*/{
      grpc::internal::OTelClientAttemptStartedInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = absl::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.method")), kMethodName);
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.target")),
            canonical_server_address_);
  VerifyNoServiceMeshAttributes(attributes);
}

TEST_P(MetadataExchangeTest, ClientAttemptDuration) {
  Init(/*metric_names=*/{
      grpc::internal::OTelClientAttemptDurationInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.method")), kMethodName);
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.target")),
            canonical_server_address_);
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.status")), "OK");
  VerifyServiceMeshAttributes(attributes);
}

TEST_P(MetadataExchangeTest, ServerCallStarted) {
  Init(
      /*metric_names=*/{grpc::internal::OTelServerCallStartedInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.server.call.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = absl::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(absl::get<int64_t>(point_data->value_), 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.method")), kMethodName);
  VerifyNoServiceMeshAttributes(attributes);
}

TEST_P(MetadataExchangeTest, ServerCallDuration) {
  Init(/*metric_names=*/{
      grpc::internal::OTelServerCallDurationInstrumentName()});
  SendRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      absl::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.method")), kMethodName);
  EXPECT_EQ(absl::get<std::string>(attributes.at("grpc.status")), "OK");
  VerifyServiceMeshAttributes(attributes);
}

INSTANTIATE_TEST_SUITE_P(
    MetadataExchange, MetadataExchangeTest,
    ::testing::Values(
        TestScenario(TestScenario::ResourceType::kGke,
                     TestScenario::XdsBootstrapSource::kFromConfig),
        TestScenario(TestScenario::ResourceType::kGke,
                     TestScenario::XdsBootstrapSource::kFromFile),
        TestScenario(TestScenario::ResourceType::kUnknown,
                     TestScenario::XdsBootstrapSource::kFromConfig),
        TestScenario(TestScenario::ResourceType::kUnknown,
                     TestScenario::XdsBootstrapSource::kFromFile)),
    &TestScenario::Name);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::SetEnv("GSM_CANONICAL_SERVICE_NAME", "canonical_service");
  return RUN_ALL_TESTS();
}
