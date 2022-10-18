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
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/proto/grpc/testing/xds/v3/fault.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.pb.h"
#include "src/proto/grpc/testing/xds/v3/tls.pb.h"
#include "test/core/util/test_config.h"

using envoy::config::listener::v3::Listener;
using envoy::extensions::filters::http::fault::v3::HTTPFault;
using envoy::extensions::filters::http::router::v3::Router;
using envoy::extensions::filters::network::http_connection_manager::v3::
    HttpConnectionManager;
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
  EXPECT_TRUE(resource_type->AllResourcesRequiredInSotW());
}

TEST_F(XdsListenerTest, UnparseableProto) {
  std::string serialized_resource("\0", 1);
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
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
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
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
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
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

TEST_F(ApiListenerTest, RdsConfigSourceUsesAds) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_ads();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
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

TEST_F(ApiListenerTest, SetsMaxStreamDuration) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* duration =
      hcm.mutable_common_http_protocol_options()->mutable_max_stream_duration();
  duration->set_seconds(5);
  duration->set_nanos(5000000);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
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
  EXPECT_EQ(api_listener->http_max_stream_duration,
            Duration::Milliseconds(5005));
}

TEST_F(ApiListenerTest, InnerApiListenerNotSet) {
  Listener listener;
  listener.set_name("foo");
  listener.mutable_api_listener();
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener error:field not present]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, DoesNotContainHttpConnectionManager) {
  Listener listener;
  listener.set_name("foo");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(Listener());
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.config.listener.v3.Listener] "
            "error:unsupported filter type]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, UnparseableHttpConnectionManagerConfig) {
  Listener listener;
  listener.set_name("foo");
  auto* any = listener.mutable_api_listener()->mutable_api_listener();
  any->PackFrom(HttpConnectionManager());
  any->set_value(std::string("\0", 1));
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager] "
            "error:could not parse HttpConnectionManager config]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, UnsupportedFieldsSet) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  hcm.set_xff_num_trusted_hops(1);
  hcm.add_original_ip_detection_extensions();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].original_ip_detection_extensions "
            "error:must be empty; "
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].xff_num_trusted_hops "
            "error:must be zero]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, InvalidMaxStreamDuration) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  hcm.mutable_common_http_protocol_options()
      ->mutable_max_stream_duration()
      ->set_seconds(-1);
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].common_http_protocol_options"
            ".max_stream_duration.seconds "
            "error:value must be in the range [0, 315576000000]]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, EmptyHttpFilterName) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters[0].name "
            "error:empty filter name]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, DuplicateHttpFilterName) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  *hcm.add_http_filters() = hcm.http_filters(0);  // Copy filter.
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters[1].name "
            "error:duplicate HTTP filter name: router]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, HttpFilterMissingConfig) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters[0].typed_config "
            "error:field not present]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, HttpFilterTypeNotSupported) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Listener());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters[0].typed_config.value["
            "envoy.config.listener.v3.Listener] "
            "error:unsupported filter type]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, HttpFilterNotSupportedOnClient) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("rbac");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::rbac::v3::RBAC());
  filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters[0].typed_config.value["
            "envoy.extensions.filters.http.rbac.v3.RBAC] "
            "error:filter is not supported on clients]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, NoHttpFilters) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters "
            "error:expected at least one HTTP filter]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, TerminalFilterNotLast) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  filter = hcm.add_http_filters();
  filter->set_name("fault");
  filter->mutable_typed_config()->PackFrom(HTTPFault());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters errors:["
            "terminal filter for config type "
            "envoy.extensions.filters.http.router.v3.Router must be the "
            "last filter in the chain; "
            "non-terminal filter for config type "
            "envoy.extensions.filters.http.fault.v3.HTTPFault is the "
            "last filter in the chain]]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, NeitherRouteConfigNorRdsName) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager] "
            "error:neither route_config nor rds fields are present]")
      << decode_result.resource.status();
}

TEST_F(ApiListenerTest, RdsConfigSourceNotAdsOrSelf) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->set_path("/foo/bar");
  listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating ApiListener: ["
            "field:api_listener.api_listener.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].rds.config_source "
            "error:ConfigSource does not specify ADS or SELF]")
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
