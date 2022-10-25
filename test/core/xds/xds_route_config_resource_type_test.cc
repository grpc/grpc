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
#include "src/proto/grpc/testing/xds/v3/fault.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "test/core/util/test_config.h"

using envoy::config::route::v3::RouteConfiguration;

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
// cluster type tests
//

// using ClusterTypeTest = XdsRouteConfigTest;

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
