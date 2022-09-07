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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/ext/xds/xds_cluster.h"
#include "src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "test/core/util/test_config.h"

using envoy::config::cluster::v3::Cluster;
using envoy::extensions::clusters::aggregate::v3::ClusterConfig;

namespace grpc_core {
namespace testing {
namespace {

TraceFlag xds_cluster_resource_type_test_trace(
    true, "xds_cluster_resource_type_test");

class XdsClusterTest : public ::testing::Test {
 protected:
  XdsBootstrap::XdsServer xds_server_ = {"xds.example.com", "", Json(), {}};
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
  XdsResourceType::DecodeContext decode_context_ = {
      nullptr,  // XdsClient
      xds_server_, &xds_cluster_resource_type_test_trace, upb_def_pool_.ptr(),
      upb_arena_.ptr()};
};

TEST_F(XdsClusterTest, Definition) {
  auto* resource_type = XdsClusterResourceType::Get();
  ASSERT_NE(resource_type, nullptr);
  EXPECT_EQ(resource_type->type_url(), "envoy.config.cluster.v3.Cluster");
  EXPECT_EQ(resource_type->v2_type_url(), "envoy.api.v2.Cluster");
  EXPECT_TRUE(resource_type->AllResourcesRequiredInSotW());
}

TEST_F(XdsClusterTest, UnparseableProto) {
  std::string serialized_resource("\0", 1);
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
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
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsClusterResourceType::ResourceDataSubclass*>(
                       decode_result.resource->get())
                       ->resource;
  EXPECT_EQ(resource.cluster_type, resource.EDS);
  EXPECT_EQ(resource.eds_service_name, "");
}

TEST_F(XdsClusterTest, EdsConfigSourceAds) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_ads();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsClusterResourceType::ResourceDataSubclass*>(
                       decode_result.resource->get())
                       ->resource;
  EXPECT_EQ(resource.cluster_type, resource.EDS);
  EXPECT_EQ(resource.eds_service_name, "");
}

TEST_F(XdsClusterTest, EdsServiceName) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  auto* eds_cluster_config = cluster.mutable_eds_cluster_config();
  eds_cluster_config->mutable_eds_config()->mutable_self();
  eds_cluster_config->set_service_name("bar");
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsClusterResourceType::ResourceDataSubclass*>(
                       decode_result.resource->get())
                       ->resource;
  EXPECT_EQ(resource.cluster_type, resource.EDS);
  EXPECT_EQ(resource.eds_service_name, "bar");
}

TEST_F(XdsClusterTest, DiscoveryTypeNotPresent) {
  Cluster cluster;
  cluster.set_name("foo");
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: [DiscoveryType not found.]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, EdsClusterConfigMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: [eds_cluster_config not present]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, EdsConfigSourceMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: ["
            "eds_cluster_config.eds_config not present]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, EdsConfigSourceWrongType) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->set_path("/whee");
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      "errors parsing CDS resource: [EDS ConfigSource is not ADS or SELF.]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsValid) {
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
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsClusterResourceType::ResourceDataSubclass*>(
                       decode_result.resource->get())
                       ->resource;
  EXPECT_EQ(resource.cluster_type, resource.LOGICAL_DNS);
  EXPECT_EQ(resource.dns_hostname, "server.example.com:443");
}

TEST_F(XdsClusterTest, LogicalDnsMissingLoadAssignment) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: ["
            "load_assignment not present for LOGICAL_DNS cluster]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsMissingLocalities) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: ["
            "load_assignment for LOGICAL_DNS cluster must have exactly one "
            "locality, found 0]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsTooManyLocalities) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints();
  cluster.mutable_load_assignment()->add_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: ["
            "load_assignment for LOGICAL_DNS cluster must have exactly one "
            "locality, found 2]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsLocalityMissingEndpoints) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: ["
            "locality for LOGICAL_DNS cluster must have exactly one "
            "endpoint, found 0]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsLocalityTooManyEndpoints) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  auto* locality = cluster.mutable_load_assignment()->add_endpoints();
  locality->add_lb_endpoints();
  locality->add_lb_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: ["
            "locality for LOGICAL_DNS cluster must have exactly one "
            "endpoint, found 2]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsEndpointMissing) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()->add_endpoints()->add_lb_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: [LbEndpoint endpoint field not set]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsAddressMissing) {
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
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: [Endpoint address field not set]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsSocketAddressMissing) {
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
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      "errors parsing CDS resource: [Address socket_address field not set]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsSocketAddressAddressMissing) {
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
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      "errors parsing CDS resource: [Address socket_address field not set]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsResolverNameSet) {
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
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing CDS resource: ["
            "LOGICAL_DNS clusters must NOT have a custom resolver name set]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsSocketAddressAddressNotSet) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.set_type(cluster.LOGICAL_DNS);
  cluster.mutable_load_assignment()
      ->add_endpoints()
      ->add_lb_endpoints()
      ->mutable_endpoint()
      ->mutable_address()
      ->mutable_socket_address();
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      "errors parsing CDS resource: [SocketAddress address field not set]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, LogicalDnsSocketAddressPortNotSet) {
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
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      "errors parsing CDS resource: [SocketAddress port_value field not set]")
      << decode_result.resource.status();
}

TEST_F(XdsClusterTest, AggregateClusterValid) {
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
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsClusterResourceType::ResourceDataSubclass*>(
                       decode_result.resource->get())
                       ->resource;
  EXPECT_EQ(resource.cluster_type, resource.AGGREGATE);
  EXPECT_THAT(resource.prioritized_cluster_names,
              ::testing::ElementsAre("bar", "baz", "quux"));
}

TEST_F(XdsClusterTest, AggregateClusterUnparseableProto) {
  Cluster cluster;
  cluster.set_name("foo");
  cluster.mutable_cluster_type()->set_name("envoy.clusters.aggregate");
  auto* any = cluster.mutable_cluster_type()->mutable_typed_config();
  any->set_type_url("type.googleapis.com/"
                    "envoy.extensions.clusters.aggregate.v3.ClusterConfig");
  any->set_value(std::string("\0", 1));
  std::string serialized_resource;
  ASSERT_TRUE(cluster.SerializeToString(&serialized_resource));
  auto* resource_type = XdsClusterResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      "errors parsing CDS resource: [Can't parse aggregate cluster.]")
      << decode_result.resource.status();
}

// FIXME: add tests for OD validation described in
// https://github.com/grpc/proposal/blob/master/A50-xds-outlier-detection.md#validation

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
