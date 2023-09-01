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

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "upb/reflection/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_cluster.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"
#include "src/proto/grpc/testing/xds/v3/address.pb.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.pb.h"
#include "src/proto/grpc/testing/xds/v3/base.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.pb.h"
#include "src/proto/grpc/testing/xds/v3/config_source.pb.h"
#include "src/proto/grpc/testing/xds/v3/endpoint.pb.h"
#include "src/proto/grpc/testing/xds/v3/extension.pb.h"
#include "src/proto/grpc/testing/xds/v3/health_check.pb.h"
#include "src/proto/grpc/testing/xds/v3/outlier_detection.pb.h"
#include "src/proto/grpc/testing/xds/v3/round_robin.pb.h"
#include "src/proto/grpc/testing/xds/v3/tls.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "src/proto/grpc/testing/xds/v3/wrr_locality.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"

using envoy::config::cluster::v3::Cluster;
using envoy::extensions::clusters::aggregate::v3::ClusterConfig;
using envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using envoy::extensions::load_balancing_policies::wrr_locality::v3::WrrLocality;
using envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext;
using xds::type::v3::TypedStruct;

namespace grpc_core {
namespace testing {
namespace {

TraceFlag xds_cluster_resource_type_test_trace(
    true, "xds_cluster_resource_type_test");

class XdsClusterTest : public ::testing::Test {
 protected:
  XdsClusterTest()
      : xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(), xds_client_->bootstrap().server(),
                        &xds_cluster_resource_type_test_trace,
                        upb_def_pool_.ptr(), upb_arena_.ptr()} {}

  static RefCountedPtr<XdsClient> MakeXdsClient() {
    grpc_error_handle error;
    auto bootstrap = GrpcXdsBootstrap::Create(
        "{\n"
        "  \"xds_servers\": [\n"
        "    {\n"
        "      \"server_uri\": \"xds.example.com\",\n"
        "      \"channel_creds\": [\n"
        "        {\"type\": \"google_default\"}\n"
        "      ]\n"
        "    }\n"
        "  ],\n"
        "  \"certificate_providers\": {\n"
        "    \"provider1\": {\n"
        "      \"plugin_name\": \"file_watcher\",\n"
        "      \"config\": {\n"
        "        \"certificate_file\": \"/path/to/cert\",\n"
        "        \"private_key_file\": \"/path/to/key\"\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}");
    if (!bootstrap.ok()) {
      Crash(absl::StrFormat("Error parsing bootstrap: %s",
                            bootstrap.status().ToString().c_str()));
    }
    return MakeRefCounted<XdsClient>(std::move(*bootstrap),
                                     /*transport_factory=*/nullptr,
                                     /*event_engine=*/nullptr, "foo agent",
                                     "foo version");
  }

  RefCountedPtr<XdsClient> xds_client_;
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
  XdsResourceType::DecodeContext decode_context_;
};

TEST_F(XdsClusterTest, Definition) {
  auto* resource_type = XdsClusterResourceType::Get();
  ASSERT_NE(resource_type, nullptr);
  EXPECT_EQ(resource_type->type_url(), "envoy.config.cluster.v3.Cluster");
  EXPECT_TRUE(resource_type->AllResourcesRequiredInSotW());
}

TEST_F(XdsClusterTest, UnparseableProto) {
  std::string serialized_resource("\0", 1);
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "Can't parse Cluster resource.")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, MinimumValidConfig) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  auto* eds = absl::get_if<XdsClusterResource::Eds>(&resource.type);
  ASSERT_NE(eds, nullptr);
  EXPECT_EQ(eds->eds_service_name, "");
  // Check defaults.
  EXPECT_EQ(JsonDump(Json::FromArray(resource.lb_policy_config)),
            "[{\"xds_wrr_locality_experimental\":{\"childPolicy\":"
            "[{\"round_robin\":{}}]}}]");
  EXPECT_FALSE(resource.lrs_load_reporting_server.has_value());
  EXPECT_EQ(resource.max_concurrent_requests, 1024);
  EXPECT_FALSE(resource.outlier_detection.has_value());
}

//
// cluster type tests
//

using ClusterTypeTest = XdsClusterTest;

TEST_F(ClusterTypeTest, EdsConfigSourceAds) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_ads();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  auto* eds = absl::get_if<XdsClusterResource::Eds>(&resource.type);
  ASSERT_NE(eds, nullptr);
  EXPECT_EQ(eds->eds_service_name, "");
}

TEST_F(ClusterTypeTest, EdsServiceName) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  auto* eds_cluster_config = cluster.mutable_eds_cluster_config();
  eds_cluster_config->mutable_eds_config()->mutable_self();
  eds_cluster_config->set_service_name("bar");
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  auto* eds = absl::get_if<XdsClusterResource::Eds>(&resource.type);
  ASSERT_NE(eds, nullptr);
  EXPECT_EQ(eds->eds_service_name, "bar");
}

TEST_F(ClusterTypeTest, EdsServiceNameAbsentWithXdstpName) {
  Cluster cluster;
  cluster.set_name("xdstp:foo");
  cluster.set_type(cluster.EDS);
  auto* eds_cluster_config = cluster.mutable_eds_cluster_config();
  eds_cluster_config->mutable_eds_config()->mutable_self();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "xdstp:foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:eds_cluster_config.service_name "
            "error:must be set if Cluster resource has an xdstp name]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, DiscoveryTypeNotPresent) {
  Cluster cluster;
  cluster.set_name("foo");
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:type error:unknown discovery type]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, EdsClusterConfigMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:eds_cluster_config error:field not present]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, EdsConfigSourceMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:eds_cluster_config.eds_config error:field not present]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, EdsConfigSourceWrongType) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->set_path("/whee");
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:eds_cluster_config.eds_config "
            "error:ConfigSource is not ads or self]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsValid) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  auto* socket_address = cluster.mutable_load_assignment()
                             ->add_endpoints()
                             ->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("server.example.com");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  auto* logical_dns =
      absl::get_if<XdsClusterResource::LogicalDns>(&resource.type);
  ASSERT_NE(logical_dns, nullptr);
  EXPECT_EQ(logical_dns->hostname, "server.example.com:443");
}

TEST_F(ClusterTypeTest, LogicalDnsMissingLoadAssignment) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment "
            "error:field not present for LOGICAL_DNS cluster]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsMissingLocalities) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints error:must contain exactly "
            "one locality for LOGICAL_DNS cluster, found 0]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsTooManyLocalities) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints();
  cluster.mutable_load_assignment()->add_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints error:must contain exactly "
            "one locality for LOGICAL_DNS cluster, found 2]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsLocalityMissingEndpoints) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints[0].lb_endpoints error:must "
            "contain exactly one endpoint for LOGICAL_DNS cluster, found 0]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsLocalityTooManyEndpoints) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  auto* locality = cluster.mutable_load_assignment()->add_endpoints();
  locality->add_lb_endpoints();
  locality->add_lb_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints[0].lb_endpoints error:must "
            "contain exactly one endpoint for LOGICAL_DNS cluster, found 2]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsEndpointMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints[0].lb_endpoints[0].endpoint "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsAddressMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints[0].lb_endpoints[0].endpoint"
            ".address error:field not present]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsSocketAddressMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints[0].lb_endpoints[0].endpoint"
            ".address.socket_address error:field not present]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, LogicalDnsSocketAddressInvalid) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address()
      ->set_resolver_name("dns");
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_assignment.endpoints[0].lb_endpoints[0].endpoint"
            ".address.socket_address.address error:field not present; "
            "field:load_assignment.endpoints[0].lb_endpoints[0].endpoint"
            ".address.socket_address.port_value error:field not present; "
            "field:load_assignment.endpoints[0].lb_endpoints[0].endpoint"
            ".address.socket_address.resolver_name error:LOGICAL_DNS "
            "clusters must NOT have a custom resolver name set]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, AggregateClusterValid) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.mutable_cluster_type()->set_name("envoy.clusters.aggregate");
  ClusterConfig cluster_config;
  cluster_config.add_clusters("bar");
  cluster_config.add_clusters("baz");
  cluster_config.add_clusters("quux");
  cluster.mutable_cluster_type()->mutable_typed_config()->PackFrom(
      cluster_config);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  auto* aggregate = absl::get_if<XdsClusterResource::Aggregate>(&resource.type);
  ASSERT_NE(aggregate, nullptr);
  EXPECT_THAT(aggregate->prioritized_cluster_names,
              ::testing::ElementsAre("bar", "baz", "quux"));
}

TEST_F(ClusterTypeTest, AggregateClusterUnparseableProto) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.mutable_cluster_type()->set_name("envoy.clusters.aggregate");
  auto* any = cluster.mutable_cluster_type()->mutable_typed_config();
  any->set_type_url(
      "type.googleapis.com/"
      "envoy.extensions.clusters.aggregate.v3.ClusterConfig");
  any->set_value(std::string("\0", 1));
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:cluster_type.typed_config.value["
            "envoy.extensions.clusters.aggregate.v3.ClusterConfig] "
            "error:can't parse aggregate cluster config]")
      << decode_result.resource.status();
}

TEST_F(ClusterTypeTest, AggregateClusterEmptyClusterList) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.mutable_cluster_type()->set_name("envoy.clusters.aggregate");
  cluster.mutable_cluster_type()->mutable_typed_config()->PackFrom(
      ClusterConfig());
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:cluster_type.typed_config.value["
            "envoy.extensions.clusters.aggregate.v3.ClusterConfig].clusters "
            "error:must be non-empty]")
      << decode_result.resource.status();
}

//
// LB policy tests
//

using LbPolicyTest = XdsClusterTest;

TEST_F(LbPolicyTest, EnumLbPolicyRingHash) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.set_lb_policy(cluster.RING_HASH);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_EQ(JsonDump(Json::FromArray(resource.lb_policy_config)),
            "[{\"ring_hash_experimental\":{"
            "\"maxRingSize\":8388608,\"minRingSize\":1024}}]");
}

TEST_F(LbPolicyTest, EnumLbPolicyRingHashSetMinAndMaxRingSize) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.set_lb_policy(cluster.RING_HASH);
  auto* ring_hash_config = cluster.mutable_ring_hash_lb_config();
  ring_hash_config->mutable_minimum_ring_size()->set_value(2048);
  ring_hash_config->mutable_maximum_ring_size()->set_value(4096);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_EQ(JsonDump(Json::FromArray(resource.lb_policy_config)),
            "[{\"ring_hash_experimental\":{"
            "\"maxRingSize\":4096,\"minRingSize\":2048}}]");
}

TEST_F(LbPolicyTest, EnumLbPolicyRingHashSetMinAndMaxRingSizeToZero) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.set_lb_policy(cluster.RING_HASH);
  auto* ring_hash_config = cluster.mutable_ring_hash_lb_config();
  ring_hash_config->mutable_minimum_ring_size()->set_value(0);
  ring_hash_config->mutable_maximum_ring_size()->set_value(0);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:ring_hash_lb_config.maximum_ring_size "
            "error:must be in the range of 1 to 8388608; "
            "field:ring_hash_lb_config.minimum_ring_size "
            "error:must be in the range of 1 to 8388608]")
      << decode_result.resource.status();
}

TEST_F(LbPolicyTest, EnumLbPolicyRingHashSetMinAndMaxRingSizeTooLarge) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.set_lb_policy(cluster.RING_HASH);
  auto* ring_hash_config = cluster.mutable_ring_hash_lb_config();
  ring_hash_config->mutable_minimum_ring_size()->set_value(8388609);
  ring_hash_config->mutable_maximum_ring_size()->set_value(8388609);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:ring_hash_lb_config.maximum_ring_size "
            "error:must be in the range of 1 to 8388608; "
            "field:ring_hash_lb_config.minimum_ring_size "
            "error:must be in the range of 1 to 8388608]")
      << decode_result.resource.status();
}

TEST_F(LbPolicyTest, EnumLbPolicyRingHashSetMinRingSizeLargerThanMaxRingSize) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.set_lb_policy(cluster.RING_HASH);
  auto* ring_hash_config = cluster.mutable_ring_hash_lb_config();
  ring_hash_config->mutable_minimum_ring_size()->set_value(1025);
  ring_hash_config->mutable_maximum_ring_size()->set_value(1024);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:ring_hash_lb_config.minimum_ring_size "
            "error:cannot be greater than maximum_ring_size]")
      << decode_result.resource.status();
}

TEST_F(LbPolicyTest, EnumLbPolicyRingHashUnsupportedHashFunction) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.set_lb_policy(cluster.RING_HASH);
  cluster.mutable_ring_hash_lb_config()->set_hash_function(
      envoy::config::cluster::v3::Cluster::RingHashLbConfig::MURMUR_HASH_2);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:ring_hash_lb_config.hash_function "
            "error:invalid hash function]")
      << decode_result.resource.status();
}

TEST_F(LbPolicyTest, EnumUnsupportedPolicy) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.set_lb_policy(cluster.MAGLEV);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:lb_policy error:LB policy is not supported]")
      << decode_result.resource.status();
}

TEST_F(LbPolicyTest, LoadBalancingPolicyField) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  WrrLocality wrr_locality;
  wrr_locality.mutable_endpoint_picking_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(RoundRobin());
  cluster.mutable_load_balancing_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(wrr_locality);
  cluster.set_lb_policy(cluster.MAGLEV);  // Will be ignored.
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_EQ(JsonDump(Json::FromArray(resource.lb_policy_config)),
            "[{\"xds_wrr_locality_experimental\":{"
            "\"childPolicy\":[{\"round_robin\":{}}]}}]");
}

// This tests that we're passing along errors from XdsLbPolicyRegistry.
// A complete list of error cases for that class is in
// xds_lb_policy_registry_test.
TEST_F(LbPolicyTest, XdsLbPolicyRegistryConversionFails) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.mutable_load_balancing_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(WrrLocality());
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_balancing_policy.policies[0].typed_extension_config"
            ".typed_config.value[envoy.extensions.load_balancing_policies"
            ".wrr_locality.v3.WrrLocality].endpoint_picking_policy "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(LbPolicyTest, ConvertedCustomPolicyFailsValidation) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  TypedStruct typed_struct;
  typed_struct.set_type_url(
      "type.googleapis.com/xds_wrr_locality_experimental");
  cluster.mutable_load_balancing_policy()
      ->add_policies()
      ->mutable_typed_extension_config()
      ->mutable_typed_config()
      ->PackFrom(typed_struct);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:load_balancing_policy "
            "error:errors validating xds_wrr_locality LB policy config: ["
            "field:childPolicy error:field not present]]")
      << decode_result.resource.status();
}

//
// TLS config tests
//

using TlsConfigTest = XdsClusterTest;

TEST_F(TlsConfigTest, MinimumValidConfig) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  auto* common_tls_context = upstream_tls_context.mutable_common_tls_context();
  auto* validation_context = common_tls_context->mutable_validation_context();
  auto* cert_provider =
      validation_context->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_EQ(resource.common_tls_context.certificate_validation_context
                .ca_certificate_provider_instance.instance_name,
            "provider1");
  EXPECT_EQ(resource.common_tls_context.certificate_validation_context
                .ca_certificate_provider_instance.certificate_name,
            "cert_name");
}

// This is just one example of where CommonTlsContext::Parse() will
// generate an error, to show that we're propagating any such errors
// correctly.  An exhaustive set of tests for CommonTlsContext::Parse()
// is in xds_common_types_test.cc.
TEST_F(TlsConfigTest, UnknownCertificateProviderInstance) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  UpstreamTlsContext upstream_tls_context;
  auto* cert_provider = upstream_tls_context.mutable_common_tls_context()
                            ->mutable_validation_context()
                            ->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("fake");
  cert_provider->set_certificate_name("cert_name");
  transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext]"
            ".common_tls_context.validation_context"
            ".ca_certificate_provider_instance.instance_name "
            "error:unrecognized certificate provider instance name: fake]")
      << decode_result.resource.status();
}

TEST_F(TlsConfigTest, UnknownTransportSocketType) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* transport_socket = cluster.mutable_transport_socket();
  transport_socket->mutable_typed_config()->PackFrom(Cluster());
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:transport_socket.typed_config.value["
            "envoy.config.cluster.v3.Cluster].type_url "
            "error:unsupported transport socket type]")
      << decode_result.resource.status();
}

TEST_F(TlsConfigTest, UnparseableUpstreamTlsContext) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* transport_socket = cluster.mutable_transport_socket();
  auto* typed_config = transport_socket->mutable_typed_config();
  typed_config->PackFrom(UpstreamTlsContext());
  typed_config->set_value(std::string("\0", 1));
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext] "
            "error:can't decode UpstreamTlsContext]")
      << decode_result.resource.status();
}

TEST_F(TlsConfigTest, UpstreamTlsContextInTypedStruct) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* transport_socket = cluster.mutable_transport_socket();
  xds::type::v3::TypedStruct typed_struct;
  typed_struct.set_type_url(
      "types.googleapis.com/"
      "envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext");
  transport_socket->mutable_typed_config()->PackFrom(typed_struct);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:transport_socket.typed_config.value["
            "xds.type.v3.TypedStruct].value["
            "envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext] "
            "error:can't decode UpstreamTlsContext]")
      << decode_result.resource.status();
}

TEST_F(TlsConfigTest, CaCertProviderUnset) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* transport_socket = cluster.mutable_transport_socket();
  auto* typed_config = transport_socket->mutable_typed_config();
  typed_config->PackFrom(UpstreamTlsContext());
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext]"
            ".common_tls_context "
            "error:no CA certificate provider instance configured]")
      << decode_result.resource.status();
}

//
// LRS server tests
//

using LrsTest = XdsClusterTest;

TEST_F(LrsTest, Valid) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.mutable_lrs_server()->mutable_self();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  ASSERT_TRUE(resource.lrs_load_reporting_server.has_value());
  EXPECT_EQ(*resource.lrs_load_reporting_server,
            xds_client_->bootstrap().server());
}

TEST_F(LrsTest, NotSelfConfigSource) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.mutable_lrs_server()->mutable_ads();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:lrs_server error:ConfigSource is not self]")
      << decode_result.resource.status();
}

//
// circuit breaker tests
//

using CircuitBreakingTest = XdsClusterTest;

TEST_F(CircuitBreakingTest, Valid) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(envoy::config::cluster::v3::HIGH);  // Ignored.
  threshold->mutable_max_requests()->set_value(251);
  threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(envoy::config::cluster::v3::DEFAULT);
  threshold->mutable_max_requests()->set_value(1701);
  threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(envoy::config::cluster::v3::HIGH);  // Ignored.
  threshold->mutable_max_requests()->set_value(5049);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_EQ(resource.max_concurrent_requests, 1701);
}

TEST_F(CircuitBreakingTest, NoDefaultThreshold) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(envoy::config::cluster::v3::HIGH);  // Ignored.
  threshold->mutable_max_requests()->set_value(251);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_EQ(resource.max_concurrent_requests, 1024);  // Default.
}

TEST_F(CircuitBreakingTest, DefaultThresholdWithMaxRequestsUnset) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(envoy::config::cluster::v3::DEFAULT);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_EQ(resource.max_concurrent_requests, 1024);  // Default.
}

//
// outlier detection tests
//

using OutlierDetectionTest = XdsClusterTest;

TEST_F(OutlierDetectionTest, DefaultValues) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  cluster.mutable_outlier_detection();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  ASSERT_TRUE(resource.outlier_detection.has_value());
  EXPECT_EQ(*resource.outlier_detection, OutlierDetectionConfig());
}

TEST_F(OutlierDetectionTest, AllFieldsSet) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* outlier_detection = cluster.mutable_outlier_detection();
  outlier_detection->mutable_interval()->set_seconds(1);
  outlier_detection->mutable_base_ejection_time()->set_seconds(2);
  outlier_detection->mutable_max_ejection_time()->set_seconds(3);
  outlier_detection->mutable_max_ejection_percent()->set_value(20);
  outlier_detection->mutable_enforcing_success_rate()->set_value(7);
  outlier_detection->mutable_success_rate_minimum_hosts()->set_value(12);
  outlier_detection->mutable_success_rate_request_volume()->set_value(31);
  outlier_detection->mutable_success_rate_stdev_factor()->set_value(251);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(9);
  outlier_detection->mutable_failure_percentage_minimum_hosts()->set_value(3);
  outlier_detection->mutable_failure_percentage_request_volume()->set_value(75);
  outlier_detection->mutable_failure_percentage_threshold()->set_value(90);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  ASSERT_TRUE(resource.outlier_detection.has_value());
  EXPECT_EQ(resource.outlier_detection->interval, Duration::Seconds(1));
  EXPECT_EQ(resource.outlier_detection->base_ejection_time,
            Duration::Seconds(2));
  EXPECT_EQ(resource.outlier_detection->max_ejection_time,
            Duration::Seconds(3));
  EXPECT_EQ(resource.outlier_detection->max_ejection_percent, 20);
  ASSERT_TRUE(resource.outlier_detection->success_rate_ejection.has_value());
  const auto& success_rate_ejection =
      *resource.outlier_detection->success_rate_ejection;
  EXPECT_EQ(success_rate_ejection.stdev_factor, 251);
  EXPECT_EQ(success_rate_ejection.enforcement_percentage, 7);
  EXPECT_EQ(success_rate_ejection.minimum_hosts, 12);
  EXPECT_EQ(success_rate_ejection.request_volume, 31);
  ASSERT_TRUE(
      resource.outlier_detection->failure_percentage_ejection.has_value());
  const auto& failure_percentage_ejection =
      *resource.outlier_detection->failure_percentage_ejection;
  EXPECT_EQ(failure_percentage_ejection.threshold, 90);
  EXPECT_EQ(failure_percentage_ejection.enforcement_percentage, 9);
  EXPECT_EQ(failure_percentage_ejection.minimum_hosts, 3);
  EXPECT_EQ(failure_percentage_ejection.request_volume, 75);
}

TEST_F(OutlierDetectionTest, InvalidValues) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* outlier_detection = cluster.mutable_outlier_detection();
  outlier_detection->mutable_interval()->set_seconds(-1);
  outlier_detection->mutable_base_ejection_time()->set_seconds(-2);
  outlier_detection->mutable_max_ejection_time()->set_seconds(-3);
  outlier_detection->mutable_max_ejection_percent()->set_value(101);
  outlier_detection->mutable_enforcing_success_rate()->set_value(101);
  outlier_detection->mutable_enforcing_failure_percentage()->set_value(101);
  outlier_detection->mutable_failure_percentage_threshold()->set_value(101);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating Cluster resource: ["
            "field:outlier_detection.base_ejection_time.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:outlier_detection.enforcing_failure_percentage "
            "error:value must be <= 100; "
            "field:outlier_detection.enforcing_success_rate "
            "error:value must be <= 100; "
            "field:outlier_detection.failure_percentage_threshold "
            "error:value must be <= 100; "
            "field:outlier_detection.interval.seconds "
            "error:value must be in the range [0, 315576000000]; "
            "field:outlier_detection.max_ejection_percent "
            "error:value must be <= 100; "
            "field:outlier_detection.max_ejection_time.seconds "
            "error:value must be in the range [0, 315576000000]]")
      << decode_result.resource.status();
}

//
// host override status tests
//

using HostOverrideStatusTest = XdsClusterTest;

TEST_F(HostOverrideStatusTest, IgnoredWhenNotEnabled) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* status_set =
      cluster.mutable_common_lb_config()->mutable_override_host_status();
  status_set->add_statuses(envoy::config::core::v3::UNKNOWN);
  status_set->add_statuses(envoy::config::core::v3::HEALTHY);
  status_set->add_statuses(envoy::config::core::v3::DRAINING);
  status_set->add_statuses(envoy::config::core::v3::UNHEALTHY);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_THAT(resource.override_host_statuses, ::testing::ElementsAre());
}

TEST_F(HostOverrideStatusTest, PassesOnRelevantHealthStatuses) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_ENABLE_OVERRIDE_HOST");
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_self();
  auto* status_set =
      cluster.mutable_common_lb_config()->mutable_override_host_status();
  status_set->add_statuses(envoy::config::core::v3::UNKNOWN);
  status_set->add_statuses(envoy::config::core::v3::HEALTHY);
  status_set->add_statuses(envoy::config::core::v3::DRAINING);
  status_set->add_statuses(envoy::config::core::v3::UNHEALTHY);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<const XdsClusterResource&>(**decode_result.resource);
  EXPECT_THAT(resource.override_host_statuses,
              ::testing::UnorderedElementsAre(
                  XdsHealthStatus(XdsHealthStatus::kUnknown),
                  XdsHealthStatus(XdsHealthStatus::kHealthy),
                  XdsHealthStatus(XdsHealthStatus::kDraining)));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
