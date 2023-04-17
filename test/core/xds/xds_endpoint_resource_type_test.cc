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

#include <stdint.h>

#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"
#include "upb/reflection/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_endpoint.h"
#include "src/core/ext/xds/xds_health_status.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/proto/grpc/testing/xds/v3/address.pb.h"
#include "src/proto/grpc/testing/xds/v3/base.pb.h"
#include "src/proto/grpc/testing/xds/v3/endpoint.pb.h"
#include "src/proto/grpc/testing/xds/v3/health_check.pb.h"
#include "src/proto/grpc/testing/xds/v3/percent.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"

using envoy::config::endpoint::v3::ClusterLoadAssignment;

namespace grpc_core {
namespace testing {
namespace {

TraceFlag xds_endpoint_resource_type_test_trace(
    true, "xds_endpoint_resource_type_test");

class XdsEndpointTest : public ::testing::Test {
 protected:
  XdsEndpointTest()
      : xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(), xds_client_->bootstrap().server(),
                        &xds_endpoint_resource_type_test_trace,
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
        "  ]\n"
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

TEST_F(XdsEndpointTest, Definition) {
  auto* resource_type = XdsEndpointResourceType::Get();
  ASSERT_NE(resource_type, nullptr);
  EXPECT_EQ(resource_type->type_url(),
            "envoy.config.endpoint.v3.ClusterLoadAssignment");
  EXPECT_FALSE(resource_type->AllResourcesRequiredInSotW());
}

TEST_F(XdsEndpointTest, UnparseableProto) {
  std::string serialized_resource("\0", 1);
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "Can't parse ClusterLoadAssignment resource.")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, MinimumValidConfig) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_EQ(resource.priorities.size(), 1);
  const auto& priority = resource.priorities[0];
  ASSERT_EQ(priority.localities.size(), 1);
  const auto& p = *priority.localities.begin();
  ASSERT_EQ(p.first, p.second.name.get());
  EXPECT_EQ(p.first->region(), "myregion");
  EXPECT_EQ(p.first->zone(), "myzone");
  EXPECT_EQ(p.first->sub_zone(), "mysubzone");
  EXPECT_EQ(p.second.lb_weight, 1);
  ASSERT_EQ(p.second.endpoints.size(), 1);
  const auto& address = p.second.endpoints.front();
  auto addr = grpc_sockaddr_to_string(&address.address(), /*normalize=*/false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "127.0.0.1:443");
  EXPECT_EQ(address.args(), ChannelArgs());
  const auto* attribute =
      static_cast<const ServerAddressWeightAttribute*>(address.GetAttribute(
          ServerAddressWeightAttribute::kServerAddressWeightAttributeKey));
  ASSERT_NE(attribute, nullptr);
  EXPECT_EQ(attribute->weight(), 1);
  ASSERT_NE(resource.drop_config, nullptr);
  EXPECT_TRUE(resource.drop_config->drop_category_list().empty());
}

TEST_F(XdsEndpointTest, EndpointWeight) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* endpoint = locality->add_lb_endpoints();
  endpoint->mutable_load_balancing_weight()->set_value(3);
  auto* socket_address =
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_EQ(resource.priorities.size(), 1);
  const auto& priority = resource.priorities[0];
  ASSERT_EQ(priority.localities.size(), 1);
  const auto& p = *priority.localities.begin();
  ASSERT_EQ(p.first, p.second.name.get());
  EXPECT_EQ(p.first->region(), "myregion");
  EXPECT_EQ(p.first->zone(), "myzone");
  EXPECT_EQ(p.first->sub_zone(), "mysubzone");
  EXPECT_EQ(p.second.lb_weight, 1);
  ASSERT_EQ(p.second.endpoints.size(), 1);
  const auto& address = p.second.endpoints.front();
  auto addr = grpc_sockaddr_to_string(&address.address(), /*normalize=*/false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "127.0.0.1:443");
  EXPECT_EQ(address.args(), ChannelArgs());
  const auto* attribute =
      static_cast<const ServerAddressWeightAttribute*>(address.GetAttribute(
          ServerAddressWeightAttribute::kServerAddressWeightAttributeKey));
  ASSERT_NE(attribute, nullptr);
  EXPECT_EQ(attribute->weight(), 3);
  ASSERT_NE(resource.drop_config, nullptr);
  EXPECT_TRUE(resource.drop_config->drop_category_list().empty());
}

TEST_F(XdsEndpointTest, IgnoresLocalityWithNoWeight) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  locality = cla.add_endpoints();
  *locality = cla.endpoints(0);
  locality->mutable_load_balancing_weight()->set_value(1);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_EQ(resource.priorities.size(), 1);
  const auto& priority = resource.priorities[0];
  ASSERT_EQ(priority.localities.size(), 1);
  const auto& p = *priority.localities.begin();
  ASSERT_EQ(p.first, p.second.name.get());
  EXPECT_EQ(p.first->region(), "myregion");
  EXPECT_EQ(p.first->zone(), "myzone");
  EXPECT_EQ(p.first->sub_zone(), "mysubzone");
  EXPECT_EQ(p.second.lb_weight, 1);
  ASSERT_EQ(p.second.endpoints.size(), 1);
  const auto& address = p.second.endpoints.front();
  auto addr = grpc_sockaddr_to_string(&address.address(), /*normalize=*/false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "127.0.0.1:443");
  EXPECT_EQ(address.args(), ChannelArgs());
  const auto* attribute =
      static_cast<const ServerAddressWeightAttribute*>(address.GetAttribute(
          ServerAddressWeightAttribute::kServerAddressWeightAttributeKey));
  ASSERT_NE(attribute, nullptr);
  EXPECT_EQ(attribute->weight(), 1);
  ASSERT_NE(resource.drop_config, nullptr);
  EXPECT_TRUE(resource.drop_config->drop_category_list().empty());
}

TEST_F(XdsEndpointTest, IgnoresLocalityWithZeroWeight) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(0);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  locality = cla.add_endpoints();
  *locality = cla.endpoints(0);
  locality->mutable_load_balancing_weight()->set_value(1);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_EQ(resource.priorities.size(), 1);
  const auto& priority = resource.priorities[0];
  ASSERT_EQ(priority.localities.size(), 1);
  const auto& p = *priority.localities.begin();
  ASSERT_EQ(p.first, p.second.name.get());
  EXPECT_EQ(p.first->region(), "myregion");
  EXPECT_EQ(p.first->zone(), "myzone");
  EXPECT_EQ(p.first->sub_zone(), "mysubzone");
  EXPECT_EQ(p.second.lb_weight, 1);
  ASSERT_EQ(p.second.endpoints.size(), 1);
  const auto& address = p.second.endpoints.front();
  auto addr = grpc_sockaddr_to_string(&address.address(), /*normalize=*/false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "127.0.0.1:443");
  EXPECT_EQ(address.args(), ChannelArgs());
  const auto* attribute =
      static_cast<const ServerAddressWeightAttribute*>(address.GetAttribute(
          ServerAddressWeightAttribute::kServerAddressWeightAttributeKey));
  ASSERT_NE(attribute, nullptr);
  EXPECT_EQ(attribute->weight(), 1);
  ASSERT_NE(resource.drop_config, nullptr);
  EXPECT_TRUE(resource.drop_config->drop_category_list().empty());
}

TEST_F(XdsEndpointTest, LocalityWithNoEndpoints) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_EQ(resource.priorities.size(), 1);
  const auto& priority = resource.priorities[0];
  ASSERT_EQ(priority.localities.size(), 1);
  const auto& p = *priority.localities.begin();
  ASSERT_EQ(p.first, p.second.name.get());
  EXPECT_EQ(p.first->region(), "myregion");
  EXPECT_EQ(p.first->zone(), "myzone");
  EXPECT_EQ(p.first->sub_zone(), "mysubzone");
  EXPECT_EQ(p.second.lb_weight, 1);
  EXPECT_EQ(p.second.endpoints.size(), 0);
  ASSERT_NE(resource.drop_config, nullptr);
  EXPECT_TRUE(resource.drop_config->drop_category_list().empty());
}

TEST_F(XdsEndpointTest, NoLocality) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[0].locality error:field not present]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, InvalidPort) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(65537);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[0].lb_endpoints[0].endpoint.address"
            ".socket_address.port_value error:invalid port]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, InvalidAddress) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("not_an_ip_address");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[0].lb_endpoints[0].endpoint.address"
            ".socket_address error:"
            "Failed to parse address:not_an_ip_address:443]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, MissingSocketAddress) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  locality->add_lb_endpoints()->mutable_endpoint()->mutable_address();
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[0].lb_endpoints[0].endpoint.address"
            ".socket_address error:field not present]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, MissingAddress) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  locality->add_lb_endpoints()->mutable_endpoint();
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[0].lb_endpoints[0].endpoint.address "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, MissingEndpoint) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  locality->add_lb_endpoints();
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[0].lb_endpoints[0].endpoint "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, EndpointWeightZero) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* endpoint = locality->add_lb_endpoints();
  endpoint->mutable_load_balancing_weight()->set_value(0);
  auto* socket_address =
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[0].lb_endpoints[0].load_balancing_weight "
            "error:must be greater than 0]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, DuplicateLocalityName) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  socket_address = locality->add_lb_endpoints()
                       ->mutable_endpoint()
                       ->mutable_address()
                       ->mutable_socket_address();
  socket_address->set_address("127.0.0.2");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[1] error:duplicate locality {region=\"myregion\", "
            "zone=\"myzone\", sub_zone=\"mysubzone\"} found in priority 0]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, SparsePriorityList) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  locality->set_priority(1);
  locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  locality_name = locality->mutable_locality();
  locality_name->set_region("myregion2");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  socket_address = locality->add_lb_endpoints()
                       ->mutable_endpoint()
                       ->mutable_address()
                       ->mutable_socket_address();
  socket_address->set_address("127.0.0.2");
  socket_address->set_port_value(443);
  locality->set_priority(3);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints errors:[priority 0 empty; priority 2 empty]]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, LocalityWeightsWithinPriorityExceedUint32Max) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  // First locality has weight of 1.
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  locality->set_priority(0);
  // Second locality has weight of uint32 max.
  locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(
      std::numeric_limits<uint32_t>::max());
  locality_name = locality->mutable_locality();
  locality_name->set_region("myregion2");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  socket_address = locality->add_lb_endpoints()
                       ->mutable_endpoint()
                       ->mutable_address()
                       ->mutable_socket_address();
  socket_address->set_address("127.0.0.2");
  socket_address->set_port_value(443);
  locality->set_priority(0);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints error:sum of locality weights for priority 0 "
            "exceeds uint32 max]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, DuplicateAddresses) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  locality->set_priority(0);
  locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  locality_name = locality->mutable_locality();
  locality_name->set_region("myregion2");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  socket_address = locality->add_lb_endpoints()
                       ->mutable_endpoint()
                       ->mutable_address()
                       ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  locality->set_priority(0);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:endpoints[1].lb_endpoints[0] "
            "error:duplicate endpoint address \"ipv4:127.0.0.1:443\"]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, DropConfig) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  auto* drop_overload = cla.mutable_policy()->add_drop_overloads();
  drop_overload->set_category("lb_drop");
  drop_overload->mutable_drop_percentage()->set_numerator(50);
  drop_overload = cla.mutable_policy()->add_drop_overloads();
  drop_overload->set_category("lb_overload");
  drop_overload->mutable_drop_percentage()->set_numerator(2500);
  drop_overload->mutable_drop_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  drop_overload = cla.mutable_policy()->add_drop_overloads();
  drop_overload->set_category("why_not");
  drop_overload->mutable_drop_percentage()->set_numerator(750000);
  drop_overload->mutable_drop_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::MILLION);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_NE(resource.drop_config, nullptr);
  const auto& drop_list = resource.drop_config->drop_category_list();
  ASSERT_EQ(drop_list.size(), 3);
  EXPECT_EQ(drop_list[0].name, "lb_drop");
  EXPECT_EQ(drop_list[0].parts_per_million, 500000);
  EXPECT_EQ(drop_list[1].name, "lb_overload");
  EXPECT_EQ(drop_list[1].parts_per_million, 250000);
  EXPECT_EQ(drop_list[2].name, "why_not");
  EXPECT_EQ(drop_list[2].parts_per_million, 750000);
}

TEST_F(XdsEndpointTest, CapsDropPercentageAt100) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  auto* drop_overload = cla.mutable_policy()->add_drop_overloads();
  drop_overload->set_category("lb_drop");
  drop_overload->mutable_drop_percentage()->set_numerator(10000001);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_NE(resource.drop_config, nullptr);
  const auto& drop_list = resource.drop_config->drop_category_list();
  ASSERT_EQ(drop_list.size(), 1);
  EXPECT_EQ(drop_list[0].name, "lb_drop");
  EXPECT_EQ(drop_list[0].parts_per_million, 1000000);
  EXPECT_TRUE(resource.drop_config->drop_all());
}

TEST_F(XdsEndpointTest, MissingDropCategoryName) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  auto* drop_overload = cla.mutable_policy()->add_drop_overloads();
  drop_overload->mutable_drop_percentage()->set_numerator(50);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:policy.drop_overloads[0].category "
            "error:empty drop category name]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, MissingDropPercentage) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  auto* drop_overload = cla.mutable_policy()->add_drop_overloads();
  drop_overload->set_category("lb_drop");
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:policy.drop_overloads[0].drop_percentage "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, DropPercentageInvalidDenominator) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* socket_address = locality->add_lb_endpoints()
                             ->mutable_endpoint()
                             ->mutable_address()
                             ->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  auto* drop_overload = cla.mutable_policy()->add_drop_overloads();
  drop_overload->set_category("lb_drop");
  drop_overload->mutable_drop_percentage()->set_numerator(750000);
  drop_overload->mutable_drop_percentage()->set_denominator(
      static_cast<envoy::type::v3::FractionalPercent_DenominatorType>(100));
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors parsing EDS resource: ["
            "field:policy.drop_overloads[0].drop_percentage.denominator "
            "error:unknown denominator type]")
      << decode_result.resource.status();
}

TEST_F(XdsEndpointTest, IgnoresEndpointsInUnsupportedStates) {
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* endpoint = locality->add_lb_endpoints();
  auto* socket_address =
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  endpoint = locality->add_lb_endpoints();
  endpoint->set_health_status(envoy::config::core::v3::HealthStatus::DRAINING);
  socket_address =
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(444);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_EQ(resource.priorities.size(), 1);
  const auto& priority = resource.priorities[0];
  ASSERT_EQ(priority.localities.size(), 1);
  const auto& p = *priority.localities.begin();
  ASSERT_EQ(p.first, p.second.name.get());
  EXPECT_EQ(p.first->region(), "myregion");
  EXPECT_EQ(p.first->zone(), "myzone");
  EXPECT_EQ(p.first->sub_zone(), "mysubzone");
  EXPECT_EQ(p.second.lb_weight, 1);
  ASSERT_EQ(p.second.endpoints.size(), 1);
  const auto& address = p.second.endpoints.front();
  auto addr = grpc_sockaddr_to_string(&address.address(), /*normalize=*/false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "127.0.0.1:443");
}

TEST_F(XdsEndpointTest, EndpointHealthStatus) {
  ScopedExperimentalEnvVar env_var(
      "GRPC_EXPERIMENTAL_XDS_ENABLE_OVERRIDE_HOST");
  ClusterLoadAssignment cla;
  cla.set_cluster_name("foo");
  auto* locality = cla.add_endpoints();
  locality->mutable_load_balancing_weight()->set_value(1);
  auto* locality_name = locality->mutable_locality();
  locality_name->set_region("myregion");
  locality_name->set_zone("myzone");
  locality_name->set_sub_zone("mysubzone");
  auto* endpoint = locality->add_lb_endpoints();
  auto* socket_address =
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.1");
  socket_address->set_port_value(443);
  endpoint = locality->add_lb_endpoints();
  endpoint->set_health_status(envoy::config::core::v3::HealthStatus::DRAINING);
  socket_address =
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.2");
  socket_address->set_port_value(443);
  endpoint = locality->add_lb_endpoints();
  endpoint->set_health_status(envoy::config::core::v3::HealthStatus::UNHEALTHY);
  socket_address =
      endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  socket_address->set_address("127.0.0.3");
  socket_address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(cla.SerializeToString(&serialized_resource));
  auto* resource_type = XdsEndpointResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsEndpointResource&>(**decode_result.resource);
  ASSERT_EQ(resource.priorities.size(), 1);
  const auto& priority = resource.priorities[0];
  ASSERT_EQ(priority.localities.size(), 1);
  const auto& p = *priority.localities.begin();
  ASSERT_EQ(p.first, p.second.name.get());
  EXPECT_EQ(p.first->region(), "myregion");
  EXPECT_EQ(p.first->zone(), "myzone");
  EXPECT_EQ(p.first->sub_zone(), "mysubzone");
  EXPECT_EQ(p.second.lb_weight, 1);
  ASSERT_EQ(p.second.endpoints.size(), 2);
  const auto* address = &p.second.endpoints[0];
  auto addr = grpc_sockaddr_to_string(&address->address(), /*normalize=*/false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "127.0.0.1:443");
  const auto* health_attribute =
      static_cast<const XdsEndpointHealthStatusAttribute*>(
          address->GetAttribute(XdsEndpointHealthStatusAttribute::kKey));
  ASSERT_NE(health_attribute, nullptr);
  EXPECT_EQ(health_attribute->status().status(), XdsHealthStatus::kUnknown);
  address = &p.second.endpoints[1];
  addr = grpc_sockaddr_to_string(&address->address(), /*normalize=*/false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "127.0.0.2:443");
  health_attribute = static_cast<const XdsEndpointHealthStatusAttribute*>(
      address->GetAttribute(XdsEndpointHealthStatusAttribute::kKey));
  ASSERT_NE(health_attribute, nullptr);
  EXPECT_EQ(health_attribute->status().status(), XdsHealthStatus::kDraining);
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
