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

#include <memory>
#include <string>
#include <utility>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "upb/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/ext/xds/xds_route_config.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"
#include "src/proto/grpc/lookup/v1/rls_config.pb.h"
#include "src/proto/grpc/testing/xds/v3/fault.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/util/scoped_env_var.h"

using envoy::config::route::v3::RouteConfiguration;
using grpc::lookup::v1::RouteLookupClusterSpecifier;

namespace grpc_core {
namespace testing {
namespace {

TraceFlag xds_route_config_resource_type_test_trace(
    true, "xds_route_config_resource_type_test");

class XdsRouteConfigTest : public ::testing::Test {
 protected:
  XdsRouteConfigTest()
      : xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(), xds_client_->bootstrap().server(),
                        &xds_route_config_resource_type_test_trace,
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
      gpr_log(GPR_ERROR, "Error parsing bootstrap: %s",
              bootstrap.status().ToString().c_str());
      GPR_ASSERT(false);
    }
    return MakeRefCounted<XdsClient>(std::move(*bootstrap),
                                     /*transport_factory=*/nullptr);
  }

  RefCountedPtr<XdsClient> xds_client_;
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
  XdsResourceType::DecodeContext decode_context_;
};

TEST_F(XdsRouteConfigTest, Definition) {
  auto* resource_type = XdsRouteConfigResourceType::Get();
  ASSERT_NE(resource_type, nullptr);
  EXPECT_EQ(resource_type->type_url(),
            "envoy.config.route.v3.RouteConfiguration");
  EXPECT_FALSE(resource_type->AllResourcesRequiredInSotW());
}

TEST_F(XdsRouteConfigTest, UnparseableProto) {
  std::string serialized_resource("\0", 1);
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "Can't parse RouteConfiguration resource.")
      << decode_result.resource.status();
}

TEST_F(XdsRouteConfigTest, MinimumValidConfig) {
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster("cluster1");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  EXPECT_THAT(resource.cluster_specifier_plugin_map, ::testing::ElementsAre());
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  EXPECT_THAT(resource.virtual_hosts[0].domains, ::testing::ElementsAre("*"));
  EXPECT_THAT(resource.virtual_hosts[0].typed_per_filter_config,
              ::testing::ElementsAre());
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto& matchers = route.matchers;
  EXPECT_EQ(matchers.path_matcher.ToString(), "StringMatcher{prefix=}");
  EXPECT_THAT(matchers.header_matchers, ::testing::ElementsAre());
  EXPECT_FALSE(matchers.fraction_per_million.has_value());
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  auto* cluster =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction::ClusterName>(
          &action->action);
  ASSERT_NE(cluster, nullptr);
  EXPECT_EQ(cluster->cluster_name, "cluster1");
  EXPECT_THAT(action->hash_policies, ::testing::ElementsAre());
  EXPECT_FALSE(action->retry_policy.has_value());
  EXPECT_FALSE(action->max_stream_duration.has_value());
  EXPECT_THAT(route.typed_per_filter_config, ::testing::ElementsAre());
}

//
// virtual host tests
//

using VirtualHostTest = XdsRouteConfigTest;

TEST_F(VirtualHostTest, BadDomainPattern) {
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("foo*bar");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster("cluster1");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:virtual_hosts[0].domains[0] "
            "error:invalid domain pattern \"foo*bar\"]")
      << decode_result.resource.status();
}

TEST_F(VirtualHostTest, NoDomainsSpecified) {
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* vhost = route_config.add_virtual_hosts();
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster("cluster1");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:virtual_hosts[0].domains error:must be non-empty]")
      << decode_result.resource.status();
}

//
// typed_per_filter_config tests
//

// These tests cover common handling of typed_per_filter_config at all
// three layers (virtual host, route, and weighted cluster), so we run
// them in all three contexts.
class TypedPerFilterConfigScope {
 public:
  enum Scope { kVirtualHost, kRoute, kWeightedCluster };

  explicit TypedPerFilterConfigScope(Scope scope) : scope_(scope) {}

  Scope scope() const { return scope_; }

  // For use as the final parameter in INSTANTIATE_TEST_SUITE_P().
  static std::string Name(
      const ::testing::TestParamInfo<TypedPerFilterConfigScope>& info) {
    switch (info.param.scope_) {
      case kVirtualHost: return "VirtualHost";
      case kRoute: return "Route";
      case kWeightedCluster: return "WeightedCluster";
      default: break;
    }
    GPR_UNREACHABLE_CODE(return "UNKNOWN");
  }

 private:
  Scope scope_;
};

class TypedPerFilterConfigTest
    : public XdsRouteConfigTest,
      public ::testing::WithParamInterface<TypedPerFilterConfigScope> {
 protected:
  TypedPerFilterConfigTest() {
    route_config_.set_name("foo");
    auto* vhost = route_config_.add_virtual_hosts();
    vhost->add_domains("*");
    auto* route_proto = vhost->add_routes();
    route_proto->mutable_match()->set_prefix("");
    route_proto->mutable_route()->set_cluster("cluster1");
  }

  static google::protobuf::Map<std::string, google::protobuf::Any>*
  GetTypedPerFilterConfigProto(RouteConfiguration* route_config) {
    switch (GetParam().scope()) {
      case TypedPerFilterConfigScope::kVirtualHost:
        return route_config->mutable_virtual_hosts(0)
                   ->mutable_typed_per_filter_config();
      case TypedPerFilterConfigScope::kRoute:
        return route_config->mutable_virtual_hosts(0)->mutable_routes(0)
                   ->mutable_typed_per_filter_config();
      case TypedPerFilterConfigScope::kWeightedCluster: {
        auto* cluster = route_config->mutable_virtual_hosts(0)
                   ->mutable_routes(0)
                   ->mutable_route()
                   ->mutable_weighted_clusters()
                   ->add_clusters();
        cluster->set_name("cluster1");
        cluster->mutable_weight()->set_value(1);
        return cluster->mutable_typed_per_filter_config();
      }
      default:
        break;
    }
    GPR_UNREACHABLE_CODE(return nullptr);
  }

  static const XdsRouteConfigResource::TypedPerFilterConfig&
  GetTypedPerFilterConfig(const XdsRouteConfigResource& resource) {
    switch (GetParam().scope()) {
      case TypedPerFilterConfigScope::kVirtualHost:
        return resource.virtual_hosts[0].typed_per_filter_config;
      case TypedPerFilterConfigScope::kRoute:
        return resource.virtual_hosts[0].routes[0].typed_per_filter_config;
      case TypedPerFilterConfigScope::kWeightedCluster: {
        auto& action = absl::get<XdsRouteConfigResource::Route::RouteAction>(
            resource.virtual_hosts[0].routes[0].action);
        auto& weighted_clusters =
            absl::get<std::vector<
                XdsRouteConfigResource::Route::RouteAction::ClusterWeight>>(
                    action.action);
        return weighted_clusters[0].typed_per_filter_config;
      }
      default:
        break;
    }
    GPR_ASSERT(false);
  }

  static absl::string_view FieldName() {
    switch (GetParam().scope()) {
      case TypedPerFilterConfigScope::kVirtualHost:
        return "virtual_hosts[0].typed_per_filter_config";
      case TypedPerFilterConfigScope::kRoute:
        return "virtual_hosts[0].routes[0].typed_per_filter_config";
      case TypedPerFilterConfigScope::kWeightedCluster:
        return "virtual_hosts[0].routes[0].route.weighted_clusters"
               ".clusters[0].typed_per_filter_config";
      default:
        break;
    }
    GPR_ASSERT(false);
  }

  RouteConfiguration route_config_;
};

INSTANTIATE_TEST_SUITE_P(
    XdsRouteConfig, TypedPerFilterConfigTest,
    ::testing::Values(
        TypedPerFilterConfigScope(TypedPerFilterConfigScope::kVirtualHost),
        TypedPerFilterConfigScope(TypedPerFilterConfigScope::kRoute),
        TypedPerFilterConfigScope(TypedPerFilterConfigScope::kWeightedCluster)),
    &TypedPerFilterConfigScope::Name);

TEST_P(TypedPerFilterConfigTest, Basic) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault_config;
  fault_config.mutable_abort()->set_grpc_status(GRPC_STATUS_PERMISSION_DENIED);
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(fault_config);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  auto& typed_per_filter_config = GetTypedPerFilterConfig(resource);
  ASSERT_EQ(typed_per_filter_config.size(), 1UL);
  auto it = typed_per_filter_config.begin();
  ASSERT_NE(it, typed_per_filter_config.end());
  EXPECT_EQ("fault", it->first);
  const auto& filter_config = it->second;
  EXPECT_EQ(filter_config.config_proto_type_name,
            "envoy.extensions.filters.http.fault.v3.HTTPFault");
  EXPECT_EQ(filter_config.config.Dump(),
            "{\"abortCode\":\"PERMISSION_DENIED\"}");
}

TEST_P(TypedPerFilterConfigTest, EmptyName) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault_config;
  fault_config.mutable_abort()->set_grpc_status(GRPC_STATUS_PERMISSION_DENIED);
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)[""].PackFrom(fault_config);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[] error:filter name must be non-empty]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest, EmptyConfig) {
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"];
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[fault].type_url error:field not present]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest, UnsupportedFilterType) {
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(RouteConfiguration());
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[fault].value[envoy.config.route.v3.RouteConfiguration] "
          "error:unsupported filter type]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest, FilterConfigInvalid) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault_config;
  fault_config.mutable_abort()->set_grpc_status(123);
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(fault_config);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[fault].value[envoy.extensions.filters.http.fault.v3.HTTPFault]"
          ".abort.grpc_status "
          "error:invalid gRPC status code: 123]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest, FilterConfigWrapper) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault_config;
  fault_config.mutable_abort()->set_grpc_status(GRPC_STATUS_PERMISSION_DENIED);
  envoy::config::route::v3::FilterConfig filter_config_wrapper;
  filter_config_wrapper.mutable_config()->PackFrom(fault_config);
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(filter_config_wrapper);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  auto& typed_per_filter_config = GetTypedPerFilterConfig(resource);
  ASSERT_EQ(typed_per_filter_config.size(), 1UL);
  auto it = typed_per_filter_config.begin();
  ASSERT_NE(it, typed_per_filter_config.end());
  EXPECT_EQ("fault", it->first);
  const auto& filter_config = it->second;
  EXPECT_EQ(filter_config.config_proto_type_name,
            "envoy.extensions.filters.http.fault.v3.HTTPFault");
  EXPECT_EQ(filter_config.config.Dump(),
            "{\"abortCode\":\"PERMISSION_DENIED\"}");
}

TEST_P(TypedPerFilterConfigTest, FilterConfigWrapperInTypedStruct) {
  xds::type::v3::TypedStruct typed_struct;
  typed_struct.set_type_url(
      "types.googleapis.com/envoy.config.route.v3.FilterConfig");
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(typed_struct);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[fault].value[xds.type.v3.TypedStruct].value["
          "envoy.config.route.v3.FilterConfig] "
          "error:could not parse FilterConfig]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest, FilterConfigWrapperUnparseable) {
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  auto& any = (*typed_per_filter_config_proto)["fault"];
  any.set_type_url("types.googleapis.com/envoy.config.route.v3.FilterConfig");
  any.set_value(std::string("\0", 1));
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[fault].value[envoy.config.route.v3.FilterConfig] "
          "error:could not parse FilterConfig]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest, FilterConfigWrapperEmptyConfig) {
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(
      envoy::config::route::v3::FilterConfig());
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[fault].value[envoy.config.route.v3.FilterConfig].config "
          "error:field not present]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest, FilterConfigWrapperUnsupportedFilterType) {
  envoy::config::route::v3::FilterConfig filter_config_wrapper;
  filter_config_wrapper.mutable_config()->PackFrom(RouteConfiguration());
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(filter_config_wrapper);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          "[fault].value[envoy.config.route.v3.FilterConfig].config.value["
          "envoy.config.route.v3.RouteConfiguration] "
          "error:unsupported filter type]"))
      << decode_result.resource.status();
}

TEST_P(TypedPerFilterConfigTest,
       FilterConfigWrapperUnsupportedOptionalFilterType) {
  envoy::config::route::v3::FilterConfig filter_config_wrapper;
  filter_config_wrapper.mutable_config()->PackFrom(RouteConfiguration());
  filter_config_wrapper.set_is_optional(true);
  auto* typed_per_filter_config_proto =
      GetTypedPerFilterConfigProto(&route_config_);
  (*typed_per_filter_config_proto)["fault"].PackFrom(filter_config_wrapper);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  auto& typed_per_filter_config = GetTypedPerFilterConfig(resource);
  EXPECT_THAT(typed_per_filter_config, ::testing::ElementsAre());
}

//
// retry policy tests
//

// These tests cover common handling of retry policy at both the virtual
// host and route layer, so we run them in both contexts.
class RetryPolicyScope {
 public:
  enum Scope { kVirtualHost, kRoute };

  explicit RetryPolicyScope(Scope scope) : scope_(scope) {}

  Scope scope() const { return scope_; }

  // For use as the final parameter in INSTANTIATE_TEST_SUITE_P().
  static std::string Name(
      const ::testing::TestParamInfo<RetryPolicyScope>& info) {
    switch (info.param.scope_) {
      case kVirtualHost: return "VirtualHost";
      case kRoute: return "Route";
      default: break;
    }
    GPR_UNREACHABLE_CODE(return "UNKNOWN");
  }

 private:
  Scope scope_;
};

class RetryPolicyTest
    : public XdsRouteConfigTest,
      public ::testing::WithParamInterface<RetryPolicyScope> {
 protected:
  RetryPolicyTest() {
    route_config_.set_name("foo");
    auto* vhost = route_config_.add_virtual_hosts();
    vhost->add_domains("*");
    auto* route_proto = vhost->add_routes();
    route_proto->mutable_match()->set_prefix("");
    route_proto->mutable_route()->set_cluster("cluster1");
  }

  static envoy::config::route::v3::RetryPolicy* GetRetryPolicyProto(
      RouteConfiguration* route_config) {
    switch (GetParam().scope()) {
      case RetryPolicyScope::kVirtualHost:
        return route_config->mutable_virtual_hosts(0)->mutable_retry_policy();
      case RetryPolicyScope::kRoute:
        return route_config->mutable_virtual_hosts(0)->mutable_routes(0)
                   ->mutable_route()
                   ->mutable_retry_policy();
      default:
        break;
    }
    GPR_UNREACHABLE_CODE(return nullptr);
  }

  static absl::string_view FieldName() {
    switch (GetParam().scope()) {
      case RetryPolicyScope::kVirtualHost:
        return "virtual_hosts[0].retry_policy";
      case RetryPolicyScope::kRoute:
        return "virtual_hosts[0].routes[0].route.retry_policy";
      default:
        break;
    }
    GPR_ASSERT(false);
  }

  RouteConfiguration route_config_;
};

INSTANTIATE_TEST_SUITE_P(
    XdsRouteConfig, RetryPolicyTest,
    ::testing::Values(
        RetryPolicyScope(RetryPolicyScope::kVirtualHost),
        RetryPolicyScope(RetryPolicyScope::kRoute)),
    &RetryPolicyScope::Name);

TEST_P(RetryPolicyTest, Empty) {
  GetRetryPolicyProto(&route_config_);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  ASSERT_TRUE(action->retry_policy.has_value());
  const auto& retry_policy = *action->retry_policy;
  // Defaults.
  auto expected_codes = internal::StatusCodeSet();
  EXPECT_EQ(retry_policy.retry_on, expected_codes)
      << "Actual: " << retry_policy.retry_on.ToString()
      << "\nExpected: " << expected_codes.ToString();
  EXPECT_EQ(retry_policy.num_retries, 1);
  EXPECT_EQ(retry_policy.retry_back_off.base_interval,
            Duration::Milliseconds(25));
  EXPECT_EQ(retry_policy.retry_back_off.max_interval,
            Duration::Milliseconds(250));
}

TEST_P(RetryPolicyTest, AllFields) {
  auto* retry_policy_proto = GetRetryPolicyProto(&route_config_);
  retry_policy_proto->set_retry_on(
      "cancelled,deadline-exceeded,internal,some-unsupported-policy,"
      "resource-exhausted,unavailable");
  retry_policy_proto->mutable_num_retries()->set_value(3);
  auto* backoff = retry_policy_proto->mutable_retry_back_off();
  backoff->mutable_base_interval()->set_seconds(1);
  backoff->mutable_max_interval()->set_seconds(3);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  ASSERT_TRUE(action->retry_policy.has_value());
  const auto& retry_policy = *action->retry_policy;
  auto expected_codes = internal::StatusCodeSet()
      .Add(GRPC_STATUS_CANCELLED)
      .Add(GRPC_STATUS_DEADLINE_EXCEEDED)
      .Add(GRPC_STATUS_INTERNAL)
      .Add(GRPC_STATUS_RESOURCE_EXHAUSTED)
      .Add(GRPC_STATUS_UNAVAILABLE);
  EXPECT_EQ(retry_policy.retry_on, expected_codes)
      << "Actual: " << retry_policy.retry_on.ToString()
      << "\nExpected: " << expected_codes.ToString();
  EXPECT_EQ(retry_policy.num_retries, 3);
  EXPECT_EQ(retry_policy.retry_back_off.base_interval, Duration::Seconds(1));
  EXPECT_EQ(retry_policy.retry_back_off.max_interval, Duration::Seconds(3));
}

TEST_P(RetryPolicyTest, MaxIntervalDefaultsTo10xBaseInterval) {
  auto* retry_policy_proto = GetRetryPolicyProto(&route_config_);
  retry_policy_proto->mutable_retry_back_off()->mutable_base_interval()
      ->set_seconds(3);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  ASSERT_TRUE(action->retry_policy.has_value());
  const auto& retry_policy = *action->retry_policy;
  EXPECT_EQ(retry_policy.retry_back_off.base_interval, Duration::Seconds(3));
  EXPECT_EQ(retry_policy.retry_back_off.max_interval, Duration::Seconds(30));
}

TEST_P(RetryPolicyTest, InvalidValues) {
  auto* retry_policy_proto = GetRetryPolicyProto(&route_config_);
  retry_policy_proto->set_retry_on("unavailable");
  retry_policy_proto->mutable_num_retries()->set_value(0);
  auto* backoff = retry_policy_proto->mutable_retry_back_off();
  backoff->mutable_base_interval()->set_seconds(315576000001);
  backoff->mutable_max_interval()->set_seconds(315576000001);
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          ".num_retries error:must be greater than 0; field:",
          FieldName(),
          ".retry_back_off.base_interval.seconds "
          "error:value must be in the range [0, 315576000000]; field:",
          FieldName(),
          ".retry_back_off.max_interval.seconds "
          "error:value must be in the range [0, 315576000000]]"))
      << decode_result.resource.status();
}

TEST_P(RetryPolicyTest, MissingBaseInterval) {
  auto* retry_policy_proto = GetRetryPolicyProto(&route_config_);
  retry_policy_proto->mutable_retry_back_off();
  std::string serialized_resource;
  ASSERT_TRUE(route_config_.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(
          "errors validating RouteConfiguration resource: [field:",
          FieldName(),
          ".retry_back_off.base_interval error:field not present]"))
      << decode_result.resource.status();
}

using RetryPolicyOverrideTest = XdsRouteConfigTest;

TEST_F(RetryPolicyOverrideTest, RoutePolicyOverridesVhostPolicy) {
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  vhost->mutable_retry_policy()->set_retry_on("unavailable");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster("cluster1");
  route_proto->mutable_route()->mutable_retry_policy()
      ->set_retry_on("cancelled");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  ASSERT_TRUE(action->retry_policy.has_value());
  auto expected_codes = internal::StatusCodeSet().Add(GRPC_STATUS_CANCELLED);
  EXPECT_EQ(action->retry_policy->retry_on, expected_codes)
      << "Actual: " << action->retry_policy->retry_on.ToString()
      << "\nExpected: " << expected_codes.ToString();
}

//
// RLS tests
//

using RlsTest = XdsRouteConfigTest;

TEST_F(RlsTest, Basic) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  RouteLookupClusterSpecifier rls_cluster_specifier;
  auto* rls_config = rls_cluster_specifier.mutable_route_lookup_config();
  rls_config->set_cache_size_bytes(1024);
  rls_config->set_lookup_service("rls.example.com");
  auto* grpc_keybuilder = rls_config->add_grpc_keybuilders();
  grpc_keybuilder->add_names()->set_service("service");
  typed_extension_config->mutable_typed_config()->PackFrom(
      rls_cluster_specifier);
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  EXPECT_THAT(
      resource.cluster_specifier_plugin_map,
      ::testing::ElementsAre(
          ::testing::Pair(
              "rls",
              "[{\"rls_experimental\":{"
              "\"childPolicy\":[{\"cds_experimental\":{}}],"
              "\"childPolicyConfigTargetFieldName\":\"cluster\","
              "\"routeLookupConfig\":{"
              "\"cacheSizeBytes\":\"1024\","
              "\"grpcKeybuilders\":[{\"names\":[{\"service\":\"service\"}]}],"
              "\"lookupService\":\"rls.example.com\"}}}]")));
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  EXPECT_THAT(resource.virtual_hosts[0].domains, ::testing::ElementsAre("*"));
  EXPECT_THAT(resource.virtual_hosts[0].typed_per_filter_config,
              ::testing::ElementsAre());
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto& matchers = route.matchers;
  EXPECT_EQ(matchers.path_matcher.ToString(), "StringMatcher{prefix=}");
  EXPECT_THAT(matchers.header_matchers, ::testing::ElementsAre());
  EXPECT_FALSE(matchers.fraction_per_million.has_value());
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  auto* plugin_name = absl::get_if<
      XdsRouteConfigResource::Route::RouteAction::ClusterSpecifierPluginName>(
          &action->action);
  ASSERT_NE(plugin_name, nullptr);
  EXPECT_EQ(plugin_name->cluster_specifier_plugin_name, "rls");
}

TEST_F(RlsTest, ClusterSpecifierPluginsIgnoredWhenNotEnabled) {
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  RouteLookupClusterSpecifier rls_cluster_specifier;
  auto* rls_config = rls_cluster_specifier.mutable_route_lookup_config();
  rls_config->set_cache_size_bytes(1024);
  rls_config->set_lookup_service("rls.example.com");
  auto* grpc_keybuilder = rls_config->add_grpc_keybuilders();
  grpc_keybuilder->add_names()->set_service("service");
  typed_extension_config->mutable_typed_config()->PackFrom(
      rls_cluster_specifier);
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster("cluster1");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  EXPECT_THAT(resource.cluster_specifier_plugin_map, ::testing::ElementsAre());
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto& matchers = route.matchers;
  EXPECT_EQ(matchers.path_matcher.ToString(), "StringMatcher{prefix=}");
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  auto* cluster_name = absl::get_if<
      XdsRouteConfigResource::Route::RouteAction::ClusterName>(&action->action);
  ASSERT_NE(cluster_name, nullptr);
  EXPECT_EQ(cluster_name->cluster_name, "cluster1");
}

TEST_F(RlsTest, DuplicateClusterSpecifierPluginNames) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  RouteLookupClusterSpecifier rls_cluster_specifier;
  auto* rls_config = rls_cluster_specifier.mutable_route_lookup_config();
  rls_config->set_cache_size_bytes(1024);
  rls_config->set_lookup_service("rls.example.com");
  auto* grpc_keybuilder = rls_config->add_grpc_keybuilders();
  grpc_keybuilder->add_names()->set_service("service");
  typed_extension_config->mutable_typed_config()->PackFrom(
      rls_cluster_specifier);
  *route_config.add_cluster_specifier_plugins() = *cluster_specifier_plugin;
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:cluster_specifier_plugins[1].extension.name "
            "error:duplicate name \"rls\"]")
      << decode_result.resource.status();
}

TEST_F(RlsTest, ClusterSpecifierPluginTypedConfigNotPresent) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:cluster_specifier_plugins[0].extension.typed_config "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(RlsTest, UnsupportedClusterSpecifierPlugin) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  typed_extension_config->mutable_typed_config()->PackFrom(
      RouteConfiguration());
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:cluster_specifier_plugins[0].extension.typed_config.value["
            "envoy.config.route.v3.RouteConfiguration] "
            "error:unsupported ClusterSpecifierPlugin type]")
      << decode_result.resource.status();
}

TEST_F(RlsTest, UnsupportedButOptionalClusterSpecifierPlugin) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  cluster_specifier_plugin->set_is_optional(true);
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  typed_extension_config->mutable_typed_config()->PackFrom(
      RouteConfiguration());
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster("cluster1");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource =
      static_cast<XdsRouteConfigResource&>(**decode_result.resource);
  EXPECT_THAT(resource.cluster_specifier_plugin_map, ::testing::ElementsAre());
  ASSERT_EQ(resource.virtual_hosts.size(), 1UL);
  ASSERT_EQ(resource.virtual_hosts[0].routes.size(), 1UL);
  auto& route = resource.virtual_hosts[0].routes[0];
  auto& matchers = route.matchers;
  EXPECT_EQ(matchers.path_matcher.ToString(), "StringMatcher{prefix=}");
  auto* action =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction>(&route.action);
  ASSERT_NE(action, nullptr);
  auto* cluster =
      absl::get_if<XdsRouteConfigResource::Route::RouteAction::ClusterName>(
          &action->action);
  ASSERT_NE(cluster, nullptr);
  EXPECT_EQ(cluster->cluster_name, "cluster1");
}

TEST_F(RlsTest, InvalidGrpcLbPolicyConfig) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  RouteLookupClusterSpecifier rls_cluster_specifier;
  auto* rls_config = rls_cluster_specifier.mutable_route_lookup_config();
  rls_config->set_cache_size_bytes(1024);
  auto* grpc_keybuilder = rls_config->add_grpc_keybuilders();
  grpc_keybuilder->add_names()->set_service("service");
  typed_extension_config->mutable_typed_config()->PackFrom(
      rls_cluster_specifier);
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:cluster_specifier_plugins[0].extension.typed_config "
            "error:ClusterSpecifierPlugin returned invalid LB policy config: "
            "errors validing RLS LB policy config: ["
            "field:routeLookupConfig.lookupService error:field not present]]")
      << decode_result.resource.status();
}

TEST_F(RlsTest, RlsInTypedStruct) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  xds::type::v3::TypedStruct typed_struct;
  typed_struct.set_type_url(
      "types.googleapis.com/grpc.lookup.v1.RouteLookupClusterSpecifier");
  typed_extension_config->mutable_typed_config()->PackFrom(typed_struct);
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:cluster_specifier_plugins[0].extension.typed_config.value["
            "xds.type.v3.TypedStruct].value["
            "grpc.lookup.v1.RouteLookupClusterSpecifier] "
            "error:could not parse plugin config]")
      << decode_result.resource.status();
}

TEST_F(RlsTest, RlsConfigUnparseable) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  auto* typed_config = typed_extension_config->mutable_typed_config();
  typed_config->PackFrom(RouteLookupClusterSpecifier());
  typed_config->set_value(std::string("\0", 1));
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:cluster_specifier_plugins[0].extension.typed_config.value["
            "grpc.lookup.v1.RouteLookupClusterSpecifier] "
            "error:could not parse plugin config]")
      << decode_result.resource.status();
}

TEST_F(RlsTest, RlsMissingRouteLookupConfig) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* cluster_specifier_plugin = route_config.add_cluster_specifier_plugins();
  auto* typed_extension_config = cluster_specifier_plugin->mutable_extension();
  typed_extension_config->set_name("rls");
  typed_extension_config->mutable_typed_config()->PackFrom(
      RouteLookupClusterSpecifier());
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:cluster_specifier_plugins[0].extension.typed_config.value["
            "grpc.lookup.v1.RouteLookupClusterSpecifier].route_lookup_config "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(RlsTest, RouteUsesUnconfiguredClusterSpecifierPlugin) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RLS_LB");
  RouteConfiguration route_config;
  route_config.set_name("foo");
  auto* vhost = route_config.add_virtual_hosts();
  vhost->add_domains("*");
  auto* route_proto = vhost->add_routes();
  route_proto->mutable_match()->set_prefix("");
  route_proto->mutable_route()->set_cluster_specifier_plugin("rls");
  std::string serialized_resource;
  ASSERT_TRUE(route_config.SerializeToString(&serialized_resource));
  auto* resource_type = XdsRouteConfigResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating RouteConfiguration resource: ["
            "field:virtual_hosts[0].routes[0].route.cluster_specifier_plugin "
            "error:unknown cluster specifier plugin name \"rls\"]")
      << decode_result.resource.status();
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
