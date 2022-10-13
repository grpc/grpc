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
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.pb.h"
#include "src/proto/grpc/testing/xds/v3/tls.pb.h"
#include "test/core/util/test_config.h"

using envoy::config::listener::v3::Listener;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext;

namespace grpc_core {
namespace testing {
namespace {

TraceFlag xds_listener_resource_type_test_trace(
    true, "xds_listener_resource_type_test");

class XdsListenerTest : public ::testing::Test {
 protected:
  XdsListenerTest()
      : xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(), xds_client_->bootstrap().server(),
                        &xds_listener_resource_type_test_trace,
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

TEST_F(XdsListenerTest, Definition) {
  auto* resource_type = XdsListenerResourceType::Get();
  ASSERT_NE(resource_type, nullptr);
  EXPECT_EQ(resource_type->type_url(), "envoy.config.listener.v3.Listener");
  EXPECT_EQ(resource_type->v2_type_url(), "envoy.api.v2.Listener");
  EXPECT_TRUE(resource_type->AllResourcesRequiredInSotW());
}

TEST_F(XdsListenerTest, UnparseableProto) {
  std::string serialized_resource("\0", 1);
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "Can't parse Listener resource.")
      << decode_result.resource.status();
}

TEST_F(XdsListenerTest, NeitherAddressNotApiListener) {
  Listener listener;
  listener.set_name("foo");
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "Listener has neither address nor ApiListener")
      << decode_result.resource.status();
}

//
// API listener tests
//

using ApiListenerTest = XdsListenerTest;

TEST_F(ApiListenerTest, MinimumValidConfig) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::router::v3::Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result = resource_type->Decode(
      decode_context_, serialized_resource, /*is_v2=*/false);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* api_listener = absl::get_if<XdsListenerResource::HttpConnectionManager>(
      &resource.listener);
  ASSERT_NE(api_listener, nullptr);
  auto* rds_name = absl::get_if<std::string>(&api_listener->route_config);
  ASSERT_NE(rds_name, nullptr);
  EXPECT_EQ(*rds_name, "rds_name");
  ASSERT_EQ(api_listener->http_filters.size(), 1UL);
  auto& router = api_listener->http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << router.config.config.Dump();
  EXPECT_EQ(api_listener->http_max_stream_duration, Duration::Zero());
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
