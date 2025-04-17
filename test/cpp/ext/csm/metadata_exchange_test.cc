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

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "google/cloud/opentelemetry/resource_detector.h"
#include "gtest/gtest.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/metric_reader.h"
#include "src/core/config/core_configuration.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/env.h"
#include "src/core/util/tmpfile.h"
#include "src/cpp/ext/csm/csm_observability.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/ext/otel/otel_test_library.h"

namespace grpc {
namespace testing {
namespace {

using OptionalLabelKey =
    grpc_core::ClientCallTracer::CallAttemptTracer::OptionalLabelKey;
using ::testing::ElementsAre;
using ::testing::Pair;

opentelemetry::sdk::resource::Resource TestGkeResource() {
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

opentelemetry::sdk::resource::Resource TestGceResource() {
  opentelemetry::sdk::common::AttributeMap attributes;
  attributes.SetAttribute("cloud.platform", "gcp_compute_engine");
  attributes.SetAttribute("cloud.availability_zone", "zone");
  attributes.SetAttribute("cloud.account.id", "id");
  return opentelemetry::sdk::resource::Resource::Create(attributes);
}

opentelemetry::sdk::resource::Resource TestUnknownResource() {
  opentelemetry::sdk::common::AttributeMap attributes;
  attributes.SetAttribute("cloud.platform", "random");
  return opentelemetry::sdk::resource::Resource::Create(attributes);
}

class TestScenario {
 public:
  enum class ResourceType : std::uint8_t { kGke, kGce, kUnknown };

  explicit TestScenario(ResourceType type) : type_(type) {}

  opentelemetry::sdk::resource::Resource GetTestResource() const {
    switch (type_) {
      case ResourceType::kGke:
        return TestGkeResource();
      case ResourceType::kGce:
        return TestGceResource();
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
      case ResourceType::kGce:
        ret_val += "Gce";
        break;
      case ResourceType::kUnknown:
        ret_val += "Unknown";
        break;
    }
    return ret_val;
  }

  ResourceType type() const { return type_; }

 private:
  ResourceType type_;
};

// A PluginOption that injects `ServiceMeshLabelsInjector`. (This is different
// from CsmOpenTelemetryPluginOption since it does not restrict itself to just
// CSM channels and servers.)
class MeshLabelsPluginOption
    : public grpc::internal::InternalOpenTelemetryPluginOption {
 public:
  explicit MeshLabelsPluginOption(
      const opentelemetry::sdk::common::AttributeMap& map)
      : labels_injector_(
            std::make_unique<grpc::internal::ServiceMeshLabelsInjector>(map)) {}

  bool IsActiveOnClientChannel(absl::string_view /*target*/) const override {
    return true;
  }

  bool IsActiveOnServer(const grpc_core::ChannelArgs& /*args*/) const override {
    return true;
  }

  const grpc::internal::LabelsInjector* labels_injector() const override {
    return labels_injector_.get();
  }

 private:
  std::unique_ptr<grpc::internal::ServiceMeshLabelsInjector> labels_injector_;
};

class MetadataExchangeTest
    : public OpenTelemetryPluginEnd2EndTest,
      public ::testing::WithParamInterface<TestScenario> {
 protected:
  void Init(Options options, bool enable_client_side_injector = true) {
    OpenTelemetryPluginEnd2EndTest::Init(std::move(
        options
            .add_plugin_option(std::make_unique<MeshLabelsPluginOption>(
                GetParam().GetTestResource().GetAttributes()))
            .set_channel_scope_filter(
                [enable_client_side_injector](
                    const OpenTelemetryPluginBuilder::ChannelScope& /*scope*/) {
                  return enable_client_side_injector;
                })));
  }

  void VerifyServiceMeshAttributes(
      const std::map<std::string,
                     opentelemetry::sdk::common::OwnedAttributeValue>&
          attributes,
      bool is_client) {
    EXPECT_EQ(
        std::get<std::string>(attributes.at("csm.workload_canonical_service")),
        "canonical_service");
    EXPECT_EQ(std::get<std::string>(attributes.at("csm.mesh_id")), "mesh-id");
    EXPECT_EQ(std::get<std::string>(
                  attributes.at("csm.remote_workload_canonical_service")),
              "canonical_service");
    if (is_client) {
      EXPECT_EQ(std::get<std::string>(attributes.at("csm.service_name")),
                "unknown");
      EXPECT_EQ(
          std::get<std::string>(attributes.at("csm.service_namespace_name")),
          "unknown");
    } else {
      // The CSM optional labels should not be present in server metrics.
      EXPECT_THAT(attributes, ::testing::Not(::testing::Contains(
                                  ::testing::Key("csm.service_name"))));
      EXPECT_THAT(attributes, ::testing::Not(::testing::Contains(::testing::Key(
                                  "csm.service_namespace_name"))));
    }
    switch (GetParam().type()) {
      case TestScenario::ResourceType::kGke:
        EXPECT_EQ(
            std::get<std::string>(attributes.at("csm.remote_workload_type")),
            "gcp_kubernetes_engine");
        EXPECT_EQ(
            std::get<std::string>(attributes.at("csm.remote_workload_name")),
            "workload");
        EXPECT_EQ(std::get<std::string>(
                      attributes.at("csm.remote_workload_namespace_name")),
                  "namespace");
        EXPECT_EQ(std::get<std::string>(
                      attributes.at("csm.remote_workload_cluster_name")),
                  "cluster");
        EXPECT_EQ(std::get<std::string>(
                      attributes.at("csm.remote_workload_location")),
                  "region");
        EXPECT_EQ(std::get<std::string>(
                      attributes.at("csm.remote_workload_project_id")),
                  "id");
        break;
      case TestScenario::ResourceType::kGce:
        EXPECT_EQ(
            std::get<std::string>(attributes.at("csm.remote_workload_type")),
            "gcp_compute_engine");
        EXPECT_EQ(
            std::get<std::string>(attributes.at("csm.remote_workload_name")),
            "workload");
        EXPECT_EQ(std::get<std::string>(
                      attributes.at("csm.remote_workload_location")),
                  "zone");
        EXPECT_EQ(std::get<std::string>(
                      attributes.at("csm.remote_workload_project_id")),
                  "id");
        break;
      case TestScenario::ResourceType::kUnknown:
        EXPECT_EQ(
            std::get<std::string>(attributes.at("csm.remote_workload_type")),
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
};

// Verify that grpc.client.attempt.started does not get service mesh attributes
TEST_P(MetadataExchangeTest, ClientAttemptStarted) {
  Init(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptStartedInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  auto client_started_value = std::get_if<int64_t>(&point_data->value_);
  ASSERT_NE(client_started_value, nullptr);
  EXPECT_EQ(*client_started_value, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.method")), kMethodName);
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.target")),
            canonical_server_address_);
  VerifyNoServiceMeshAttributes(attributes);
}

TEST_P(MetadataExchangeTest, ClientAttemptDuration) {
  Init(std::move(
      Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                      kClientAttemptDurationInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.method")), kMethodName);
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.target")),
            canonical_server_address_);
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.status")), "OK");
  VerifyServiceMeshAttributes(attributes, /*is_client=*/true);
}

// Verify that grpc.server.call.started does not get service mesh attributes
TEST_P(MetadataExchangeTest, ServerCallStarted) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::kServerCallStartedInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.server.call.started";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data = std::get_if<opentelemetry::sdk::metrics::SumPointData>(
      &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(std::get<int64_t>(point_data->value_), 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.method")), kMethodName);
  VerifyNoServiceMeshAttributes(attributes);
}

TEST_P(MetadataExchangeTest, ServerCallDuration) {
  Init(std::move(Options().set_metric_names(
      {grpc::OpenTelemetryPluginBuilder::kServerCallDurationInstrumentName})));
  SendRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.method")), kMethodName);
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.status")), "OK");
  VerifyServiceMeshAttributes(attributes, /*is_client=*/false);
}

// Test that the server records unknown when the client does not send metadata
TEST_P(MetadataExchangeTest, ClientDoesNotSendMetadata) {
  Init(std::move(
           Options().set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                           kServerCallDurationInstrumentName})),
       /*enable_client_side_injector=*/false);
  SendRPC();
  const char* kMetricName = "grpc.server.call.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  auto point_data =
      std::get_if<opentelemetry::sdk::metrics::HistogramPointData>(
          &data[kMetricName][0].point_data);
  ASSERT_NE(point_data, nullptr);
  ASSERT_EQ(point_data->count_, 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.method")), kMethodName);
  EXPECT_EQ(std::get<std::string>(attributes.at("grpc.status")), "OK");
  EXPECT_EQ(
      std::get<std::string>(attributes.at("csm.workload_canonical_service")),
      "canonical_service");
  EXPECT_EQ(std::get<std::string>(attributes.at("csm.mesh_id")), "mesh-id");
  EXPECT_EQ(std::get<std::string>(attributes.at("csm.remote_workload_type")),
            "unknown");
  EXPECT_EQ(std::get<std::string>(
                attributes.at("csm.remote_workload_canonical_service")),
            "unknown");
}

TEST_P(MetadataExchangeTest, VerifyCsmServiceLabels) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptDurationInstrumentName})
          .set_labels_to_inject(
              {{OptionalLabelKey::kXdsServiceName,
                grpc_core::RefCountedStringValue("myservice")},
               {OptionalLabelKey::kXdsServiceNamespace,
                grpc_core::RefCountedStringValue("mynamespace")}})));
  SendRPC();
  const char* kMetricName = "grpc.client.attempt.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) { return !data.contains(kMetricName); });
  ASSERT_EQ(data[kMetricName].size(), 1);
  const auto& attributes = data[kMetricName][0].attributes.GetAttributes();
  EXPECT_EQ(std::get<std::string>(attributes.at("csm.service_name")),
            "myservice");
  EXPECT_EQ(std::get<std::string>(attributes.at("csm.service_namespace_name")),
            "mynamespace");
}

// Test that metadata exchange works and corresponding service mesh labels are
// received and recorded even if the server sends a trailers-only response.
TEST_P(MetadataExchangeTest, Retries) {
  Init(std::move(
      Options()
          .set_metric_names({grpc::OpenTelemetryPluginBuilder::
                                 kClientAttemptDurationInstrumentName})
          .set_service_config(
              "{\n"
              "  \"methodConfig\": [ {\n"
              "    \"name\": [\n"
              "      { \"service\": \"grpc.testing.EchoTestService\" }\n"
              "    ],\n"
              "    \"retryPolicy\": {\n"
              "      \"maxAttempts\": 3,\n"
              "      \"initialBackoff\": \"0.1s\",\n"
              "      \"maxBackoff\": \"120s\",\n"
              "      \"backoffMultiplier\": 1,\n"
              "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
              "    }\n"
              "  } ]\n"
              "}")));
  EchoRequest request;
  request.mutable_param()->mutable_expected_error()->set_code(
      StatusCode::ABORTED);
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub_->Echo(&context, request, &response);
  const char* kMetricName = "grpc.client.attempt.duration";
  auto data = ReadCurrentMetricsData(
      [&](const absl::flat_hash_map<
          std::string,
          std::vector<opentelemetry::sdk::metrics::PointDataAttributes>>&
              data) {
        return !data.contains(kMetricName) ||
               std::get<opentelemetry::sdk::metrics::HistogramPointData>(
                   data.at(kMetricName)[0].point_data)
                       .count_ != 3;
      });
  ASSERT_EQ(std::get<opentelemetry::sdk::metrics::HistogramPointData>(
                data.at(kMetricName)[0].point_data)
                .count_,
            3);
  VerifyServiceMeshAttributes(data.at(kMetricName)[0].attributes,
                              /*is_client=*/true);
}

// Creates a serialized slice with labels for metadata exchange based on \a
// resource.
grpc_core::Slice RemoteMetadataSliceFromResource(
    const opentelemetry::sdk::resource::Resource& resource) {
  return grpc::internal::ServiceMeshLabelsInjector(resource.GetAttributes())
      .TestOnlySerializedLabels()
      .Ref();
}

std::vector<std::pair<absl::string_view, absl::string_view>> LabelsFromIterable(
    grpc::internal::MeshLabelsIterable* iterable) {
  std::vector<std::pair<absl::string_view, absl::string_view>> labels;
  while (true) {
    auto label = iterable->Next();
    if (!label.has_value()) break;
    labels.push_back(*std::move(label));
  }
  EXPECT_EQ(labels.size(), iterable->Size());
  return labels;
}

std::string PrettyPrintLabels(
    const std::vector<std::pair<absl::string_view, absl::string_view>>&
        labels) {
  std::vector<std::string> strings;
  strings.reserve(labels.size());
  for (const auto& pair : labels) {
    strings.push_back(
        absl::StrFormat("{\"%s\" : \"%s\"}", pair.first, pair.second));
  }
  return absl::StrJoin(strings, ", ");
}

TEST(MeshLabelsIterableTest, NoRemoteMetadata) {
  std::vector<std::pair<absl::string_view, std::string>> local_labels = {
      {"csm.workload_canonical_service", "canonical_service"},
      {"csm.mesh_id", "mesh"}};
  grpc::internal::MeshLabelsIterable iterable(local_labels, grpc_core::Slice());
  auto labels = LabelsFromIterable(&iterable);
  EXPECT_FALSE(iterable.GotRemoteLabels());
  EXPECT_THAT(
      labels,
      ElementsAre(Pair("csm.workload_canonical_service", "canonical_service"),
                  Pair("csm.mesh_id", "mesh"),
                  Pair("csm.remote_workload_type", "unknown"),
                  Pair("csm.remote_workload_canonical_service", "unknown")))
      << PrettyPrintLabels(labels);
}

TEST(MeshLabelsIterableTest, RemoteGceTypeMetadata) {
  std::vector<std::pair<absl::string_view, std::string>> local_labels = {
      {"csm.workload_canonical_service", "canonical_service"},
      {"csm.mesh_id", "mesh"}};
  grpc::internal::MeshLabelsIterable iterable(
      local_labels, RemoteMetadataSliceFromResource(TestGceResource()));
  auto labels = LabelsFromIterable(&iterable);
  EXPECT_TRUE(iterable.GotRemoteLabels());
  EXPECT_THAT(
      labels,
      ElementsAre(
          Pair("csm.workload_canonical_service", "canonical_service"),
          Pair("csm.mesh_id", "mesh"),
          Pair("csm.remote_workload_type", "gcp_compute_engine"),
          Pair("csm.remote_workload_canonical_service", "canonical_service"),
          Pair("csm.remote_workload_name", "workload"),
          Pair("csm.remote_workload_location", "zone"),
          Pair("csm.remote_workload_project_id", "id")))
      << PrettyPrintLabels(labels);
}

TEST(MeshLabelsIterableTest, RemoteGkeTypeMetadata) {
  std::vector<std::pair<absl::string_view, std::string>> local_labels = {
      {"csm.workload_canonical_service", "canonical_service"},
      {"csm.mesh_id", "mesh"}};
  grpc::internal::MeshLabelsIterable iterable(
      local_labels, RemoteMetadataSliceFromResource(TestGkeResource()));
  auto labels = LabelsFromIterable(&iterable);
  EXPECT_TRUE(iterable.GotRemoteLabels());
  EXPECT_THAT(
      labels,
      ElementsAre(
          Pair("csm.workload_canonical_service", "canonical_service"),
          Pair("csm.mesh_id", "mesh"),
          Pair("csm.remote_workload_type", "gcp_kubernetes_engine"),
          Pair("csm.remote_workload_canonical_service", "canonical_service"),
          Pair("csm.remote_workload_name", "workload"),
          Pair("csm.remote_workload_namespace_name", "namespace"),
          Pair("csm.remote_workload_cluster_name", "cluster"),
          Pair("csm.remote_workload_location", "region"),
          Pair("csm.remote_workload_project_id", "id")))
      << PrettyPrintLabels(labels);
}

TEST(MeshLabelsIterableTest, RemoteUnknownTypeMetadata) {
  std::vector<std::pair<absl::string_view, std::string>> local_labels = {
      {"csm.workload_canonical_service", "canonical_service"},
      {"csm.mesh_id", "mesh"}};
  grpc::internal::MeshLabelsIterable iterable(
      local_labels, RemoteMetadataSliceFromResource(TestUnknownResource()));
  auto labels = LabelsFromIterable(&iterable);
  EXPECT_TRUE(iterable.GotRemoteLabels());
  EXPECT_THAT(
      labels,
      ElementsAre(
          Pair("csm.workload_canonical_service", "canonical_service"),
          Pair("csm.mesh_id", "mesh"),
          Pair("csm.remote_workload_type", "random"),
          Pair("csm.remote_workload_canonical_service", "canonical_service")))
      << PrettyPrintLabels(labels);
}

TEST(MeshLabelsIterableTest, TestResetIteratorPosition) {
  std::vector<std::pair<absl::string_view, std::string>> local_labels = {
      {"csm.workload_canonical_service", "canonical_service"},
      {"csm.mesh_id", "mesh"}};
  grpc::internal::MeshLabelsIterable iterable(local_labels, grpc_core::Slice());
  auto labels = LabelsFromIterable(&iterable);
  auto expected_labels_matcher = ElementsAre(
      Pair("csm.workload_canonical_service", "canonical_service"),
      Pair("csm.mesh_id", "mesh"), Pair("csm.remote_workload_type", "unknown"),
      Pair("csm.remote_workload_canonical_service", "unknown"));
  EXPECT_THAT(labels, expected_labels_matcher) << PrettyPrintLabels(labels);
  // Resetting the iterable should return the entire list again.
  iterable.ResetIteratorPosition();
  labels = LabelsFromIterable(&iterable);
  EXPECT_THAT(labels, expected_labels_matcher) << PrettyPrintLabels(labels);
}

INSTANTIATE_TEST_SUITE_P(
    MetadataExchange, MetadataExchangeTest,
    ::testing::Values(TestScenario(TestScenario::ResourceType::kGke),
                      TestScenario(TestScenario::ResourceType::kGce),
                      TestScenario(TestScenario::ResourceType::kUnknown)),
    &TestScenario::Name);

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_core::SetEnv("CSM_WORKLOAD_NAME", "workload");
  grpc_core::SetEnv("CSM_CANONICAL_SERVICE_NAME", "canonical_service");
  grpc_core::SetEnv("CSM_MESH_ID", "mesh-id");
  return RUN_ALL_TESTS();
}
