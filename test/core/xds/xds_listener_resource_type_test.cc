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
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "upb/reflection/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_listener.h"
#include "src/core/ext/xds/xds_resource_type.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"
#include "src/proto/grpc/testing/xds/v3/address.pb.h"
#include "src/proto/grpc/testing/xds/v3/base.pb.h"
#include "src/proto/grpc/testing/xds/v3/config_source.pb.h"
#include "src/proto/grpc/testing/xds/v3/fault.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/listener.pb.h"
#include "src/proto/grpc/testing/xds/v3/protocol.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.pb.h"
#include "src/proto/grpc/testing/xds/v3/string.pb.h"
#include "src/proto/grpc/testing/xds/v3/tls.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "test/core/util/test_config.h"

using envoy::config::listener::v3::Listener;
using envoy::extensions::filters::http::fault::v3::HTTPFault;
using envoy::extensions::filters::http::rbac::v3::RBAC;
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

// TODO(roth): Re-enable the following test once
// github.com/istio/istio/issues/38914 is resolved.
TEST_F(XdsListenerTest, DISABLED_BothAddressAndApiListener) {
  Listener listener;
  listener.set_name("foo");
  listener.mutable_api_listener();
  listener.mutable_address();
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "Listener has both address and ApiListener")
      << decode_result.resource.status();
}

//
// HttpConnectionManager tests
//

struct HttpConnectionManagerLocation {
  bool in_api_listener = false;

  explicit HttpConnectionManagerLocation(bool in_api_listener)
      : in_api_listener(in_api_listener) {}

  // For use as the final parameter in INSTANTIATE_TEST_SUITE_P().
  static std::string Name(
      const ::testing::TestParamInfo<HttpConnectionManagerLocation>& info) {
    return info.param.in_api_listener ? "ApiListener" : "TcpListener";
  }
};

// These tests cover common behavior for both API listeners and TCP
// listeners, so we run them in both contexts.
class HttpConnectionManagerTest
    : public XdsListenerTest,
      public ::testing::WithParamInterface<HttpConnectionManagerLocation> {
 protected:
  static Listener MakeListener(HttpConnectionManager hcm) {
    Listener listener;
    listener.set_name("foo");
    if (GetParam().in_api_listener) {
      // Client.
      listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
    } else {
      // Server.
      listener.mutable_default_filter_chain()
          ->add_filters()
          ->mutable_typed_config()
          ->PackFrom(hcm);
      auto* address = listener.mutable_address()->mutable_socket_address();
      address->set_address("127.0.0.1");
      address->set_port_value(443);
    }
    return listener;
  }

  static absl::optional<XdsListenerResource::HttpConnectionManager>
  GetHCMConfig(const XdsListenerResource& resource) {
    if (GetParam().in_api_listener) {
      // Client.
      auto* hcm = absl::get_if<XdsListenerResource::HttpConnectionManager>(
          &resource.listener);
      if (hcm == nullptr) return absl::nullopt;
      return *hcm;
    }
    // Server.
    auto* tcp_listener =
        absl::get_if<XdsListenerResource::TcpListener>(&resource.listener);
    if (tcp_listener == nullptr) return absl::nullopt;
    if (!tcp_listener->default_filter_chain.has_value()) return absl::nullopt;
    return tcp_listener->default_filter_chain->http_connection_manager;
  }

  static absl::string_view ErrorPrefix() {
    // Client.
    if (GetParam().in_api_listener) return "errors validating ApiListener: ";
    // Server.
    return "errors validating server Listener: ";
  }

  static absl::string_view FieldPrefix() {
    // Client.
    if (GetParam().in_api_listener) return "api_listener.api_listener";
    // Server.
    return "default_filter_chain.filters[0].typed_config";
  }
};

INSTANTIATE_TEST_SUITE_P(
    XdsHcm, HttpConnectionManagerTest,
    ::testing::Values(HttpConnectionManagerLocation(true),
                      HttpConnectionManagerLocation(false)),
    &HttpConnectionManagerLocation::Name);

TEST_P(HttpConnectionManagerTest, MinimumValidConfig) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto http_connection_manager = GetHCMConfig(resource);
  ASSERT_TRUE(http_connection_manager.has_value());
  auto* rds_name =
      absl::get_if<std::string>(&http_connection_manager->route_config);
  ASSERT_NE(rds_name, nullptr);
  EXPECT_EQ(*rds_name, "rds_name");
  ASSERT_EQ(http_connection_manager->http_filters.size(), 1UL);
  auto& router = http_connection_manager->http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
  EXPECT_EQ(http_connection_manager->http_max_stream_duration,
            Duration::Zero());
}

TEST_P(HttpConnectionManagerTest, RdsConfigSourceUsesAds) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_ads();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto http_connection_manager = GetHCMConfig(resource);
  ASSERT_TRUE(http_connection_manager.has_value());
  auto* rds_name =
      absl::get_if<std::string>(&http_connection_manager->route_config);
  ASSERT_NE(rds_name, nullptr);
  EXPECT_EQ(*rds_name, "rds_name");
  ASSERT_EQ(http_connection_manager->http_filters.size(), 1UL);
  auto& router = http_connection_manager->http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
  EXPECT_EQ(http_connection_manager->http_max_stream_duration,
            Duration::Zero());
}

TEST_P(HttpConnectionManagerTest, NeitherRouteConfigNorRdsName) {
  HttpConnectionManager hcm;
  hcm.mutable_scoped_routes();
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager] "
                   "error:neither route_config nor rds fields are present]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, RdsConfigSourceNotAdsOrSelf) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->set_path("/foo/bar");
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].rds.config_source "
                   "error:ConfigSource does not specify ADS or SELF]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, RdsConfigSourceNotSet) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].rds.config_source "
                   "error:field not present]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, SetsMaxStreamDuration) {
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
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto http_connection_manager = GetHCMConfig(resource);
  ASSERT_TRUE(http_connection_manager.has_value());
  auto* rds_name =
      absl::get_if<std::string>(&http_connection_manager->route_config);
  ASSERT_NE(rds_name, nullptr);
  EXPECT_EQ(*rds_name, "rds_name");
  ASSERT_EQ(http_connection_manager->http_filters.size(), 1UL);
  auto& router = http_connection_manager->http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
  EXPECT_EQ(http_connection_manager->http_max_stream_duration,
            Duration::Milliseconds(5005));
}

TEST_P(HttpConnectionManagerTest, InvalidMaxStreamDuration) {
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
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].common_http_protocol_options"
                   ".max_stream_duration.seconds "
                   "error:value must be in the range [0, 315576000000]]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, UnsupportedFieldsSet) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  hcm.set_xff_num_trusted_hops(1);
  hcm.add_original_ip_detection_extensions();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].original_ip_detection_extensions "
                   "error:must be empty; field:",
                   FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].xff_num_trusted_hops "
                   "error:must be zero]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, EmptyHttpFilterName) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].http_filters[0].name "
                   "error:empty filter name]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, DuplicateHttpFilterName) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  *hcm.add_http_filters() = hcm.http_filters(0);  // Copy filter.
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].http_filters[1].name "
                   "error:duplicate HTTP filter name: router]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, HttpFilterMissingConfig) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].http_filters[0].typed_config "
                   "error:field not present]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, HttpFilterTypeNotSupported) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Listener());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].http_filters[0].typed_config.value["
                   "envoy.config.listener.v3.Listener] "
                   "error:unsupported filter type]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, HttpFilterTypeNotSupportedButOptional) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("unsupported");
  filter->mutable_typed_config()->PackFrom(Listener());
  filter->set_is_optional(true);
  filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto http_connection_manager = GetHCMConfig(resource);
  ASSERT_TRUE(http_connection_manager.has_value());
  ASSERT_EQ(http_connection_manager->http_filters.size(), 1UL);
  auto& router = http_connection_manager->http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
}

TEST_P(HttpConnectionManagerTest, NoHttpFilters) {
  HttpConnectionManager hcm;
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].http_filters "
                   "error:expected at least one HTTP filter]"))
      << decode_result.resource.status();
}

TEST_P(HttpConnectionManagerTest, TerminalFilterNotLast) {
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  filter = hcm.add_http_filters();
  if (GetParam().in_api_listener) {
    // Client.
    filter->set_name("fault");
    filter->mutable_typed_config()->PackFrom(HTTPFault());
  } else {
    // Server.
    filter->set_name("rbac");
    filter->mutable_typed_config()->PackFrom(RBAC());
  }
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  Listener listener = MakeListener(hcm);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      decode_result.resource.status().message(),
      absl::StrCat(ErrorPrefix(), "[field:", FieldPrefix(),
                   ".value["
                   "envoy.extensions.filters.network.http_connection_manager.v3"
                   ".HttpConnectionManager].http_filters errors:["
                   "terminal filter for config type "
                   "envoy.extensions.filters.http.router.v3.Router must be the "
                   "last filter in the chain; "
                   "non-terminal filter for config type ",
                   (GetParam().in_api_listener
                        ? "envoy.extensions.filters.http.fault.v3.HTTPFault"
                        : "envoy.extensions.filters.http.rbac.v3.RBAC"),
                   " is the last filter in the chain]]"))
      << decode_result.resource.status();
}

using HttpConnectionManagerClientOrServerOnlyTest = XdsListenerTest;

TEST_F(HttpConnectionManagerClientOrServerOnlyTest,
       HttpFilterNotSupportedOnClient) {
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

TEST_F(HttpConnectionManagerClientOrServerOnlyTest,
       HttpFilterNotSupportedOnClientButOptional) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("rbac");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::rbac::v3::RBAC());
  filter->set_is_optional(true);
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
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* api_listener = absl::get_if<XdsListenerResource::HttpConnectionManager>(
      &resource.listener);
  ASSERT_NE(api_listener, nullptr);
  ASSERT_EQ(api_listener->http_filters.size(), 1UL);
  auto& router = api_listener->http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
}

TEST_F(HttpConnectionManagerClientOrServerOnlyTest,
       HttpFilterNotSupportedOnServer) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("fault");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::fault::v3::HTTPFault());
  filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(hcm);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.filters[0].typed_config.value["
            "envoy.extensions.filters.network.http_connection_manager.v3"
            ".HttpConnectionManager].http_filters[0].typed_config.value["
            "envoy.extensions.filters.http.fault.v3.HTTPFault] "
            "error:filter is not supported on servers]")
      << decode_result.resource.status();
}

TEST_F(HttpConnectionManagerClientOrServerOnlyTest,
       HttpFilterNotSupportedOnServerButOptional) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("fault");
  filter->mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::fault::v3::HTTPFault());
  filter->set_is_optional(true);
  filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(hcm);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* tcp_listener =
      absl::get_if<XdsListenerResource::TcpListener>(&resource.listener);
  ASSERT_NE(tcp_listener, nullptr);
  ASSERT_TRUE(tcp_listener->default_filter_chain.has_value());
  const auto& http_connection_manager =
      tcp_listener->default_filter_chain->http_connection_manager;
  ASSERT_EQ(http_connection_manager.http_filters.size(), 1UL);
  auto& router = http_connection_manager.http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
}

//
// API listener tests
//

using ApiListenerTest = XdsListenerTest;

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

//
// TCP listener tests
//

using TcpListenerTest = XdsListenerTest;

TEST_F(TcpListenerTest, MinimumValidConfig) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(hcm);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* tcp_listener =
      absl::get_if<XdsListenerResource::TcpListener>(&resource.listener);
  ASSERT_NE(tcp_listener, nullptr);
  EXPECT_EQ(tcp_listener->address, "127.0.0.1:443");
  EXPECT_THAT(tcp_listener->filter_chain_map.destination_ip_vector,
              ::testing::ElementsAre());
  ASSERT_TRUE(tcp_listener->default_filter_chain.has_value());
  EXPECT_TRUE(
      tcp_listener->default_filter_chain->downstream_tls_context.Empty());
  const auto& http_connection_manager =
      tcp_listener->default_filter_chain->http_connection_manager;
  auto* rds_name =
      absl::get_if<std::string>(&http_connection_manager.route_config);
  ASSERT_NE(rds_name, nullptr);
  EXPECT_EQ(*rds_name, "rds_name");
  ASSERT_EQ(http_connection_manager.http_filters.size(), 1UL);
  auto& router = http_connection_manager.http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
  EXPECT_EQ(http_connection_manager.http_max_stream_duration, Duration::Zero());
}

// TODO(yashkt): Add tests for all interesting combinations of filter
// chain match criteria.
TEST_F(TcpListenerTest, FilterChainMatchCriteria) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* match = filter_chain->mutable_filter_chain_match();
  auto* cidr_range = match->add_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(24);
  cidr_range = match->add_source_prefix_ranges();
  cidr_range->set_address_prefix("5.6.7.8");
  cidr_range->mutable_prefix_len()->set_value(16);
  match->add_source_ports(1025);
  match->set_transport_protocol("raw_buffer");
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* tcp_listener =
      absl::get_if<XdsListenerResource::TcpListener>(&resource.listener);
  ASSERT_NE(tcp_listener, nullptr);
  EXPECT_EQ(tcp_listener->address, "127.0.0.1:443");
  EXPECT_FALSE(tcp_listener->default_filter_chain.has_value());
  ASSERT_EQ(tcp_listener->filter_chain_map.destination_ip_vector.size(), 1UL);
  auto& dest_ip = tcp_listener->filter_chain_map.destination_ip_vector.front();
  ASSERT_TRUE(dest_ip.prefix_range.has_value());
  auto addr = grpc_sockaddr_to_string(&dest_ip.prefix_range->address, false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "1.2.3.0:0");
  EXPECT_EQ(dest_ip.prefix_range->prefix_len, 24);
  ASSERT_EQ(dest_ip.source_types_array.size(), 3UL);
  EXPECT_THAT(dest_ip.source_types_array[1], ::testing::ElementsAre());
  EXPECT_THAT(dest_ip.source_types_array[2], ::testing::ElementsAre());
  ASSERT_EQ(dest_ip.source_types_array[0].size(), 1UL);
  auto& source_ip = dest_ip.source_types_array[0].front();
  ASSERT_TRUE(source_ip.prefix_range.has_value());
  addr = grpc_sockaddr_to_string(&source_ip.prefix_range->address, false);
  ASSERT_TRUE(addr.ok()) << addr.status();
  EXPECT_EQ(*addr, "5.6.0.0:0");
  EXPECT_EQ(source_ip.prefix_range->prefix_len, 16);
  ASSERT_EQ(source_ip.ports_map.size(), 1UL);
  auto it = source_ip.ports_map.begin();
  EXPECT_EQ(it->first, 1025);
  ASSERT_NE(it->second.data, nullptr);
  auto& filter_data = *it->second.data;
  EXPECT_TRUE(filter_data.downstream_tls_context.Empty());
  const auto& http_connection_manager = filter_data.http_connection_manager;
  auto* rds_name =
      absl::get_if<std::string>(&http_connection_manager.route_config);
  ASSERT_NE(rds_name, nullptr);
  EXPECT_EQ(*rds_name, "rds_name");
  ASSERT_EQ(http_connection_manager.http_filters.size(), 1UL);
  auto& router = http_connection_manager.http_filters[0];
  EXPECT_EQ(router.name, "router");
  EXPECT_EQ(router.config.config_proto_type_name,
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(router.config.config, Json()) << JsonDump(router.config.config);
  EXPECT_EQ(http_connection_manager.http_max_stream_duration, Duration::Zero());
}

TEST_F(TcpListenerTest, SocketAddressNotPresent) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(hcm);
  listener.mutable_address();
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:address.socket_address error:field not present]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, SocketAddressBadValues) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(hcm);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(65536);
  address->set_protocol(address->UDP);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:address.socket_address.port_value error:invalid port; "
            "field:address.socket_address.protocol error:value must be TCP]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, UseOriginalDstNotSupported) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(hcm);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  listener.mutable_use_original_dst()->set_value(true);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:use_original_dst error:field not supported]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, NoFilterChains) {
  Listener listener;
  listener.set_name("foo");
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain "
            "error:must be set if filter_chains is unset]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, UnsupportedFilter) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(Listener());
  listener.mutable_default_filter_chain()
      ->add_filters()
      ->mutable_typed_config()
      ->PackFrom(hcm);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.filters "
            "error:must have exactly one filter (HttpConnectionManager -- "
            "no other filter is supported at the moment); "
            "field:default_filter_chain.filters[0].typed_config.value["
            "envoy.config.listener.v3.Listener] "
            "error:unsupported filter type]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, BadCidrRanges) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* match = filter_chain->mutable_filter_chain_match();
  auto* cidr_range = match->add_prefix_ranges();
  cidr_range->set_address_prefix("foobar");
  cidr_range->mutable_prefix_len()->set_value(24);
  cidr_range = match->add_source_prefix_ranges();
  cidr_range->set_address_prefix("invalid");
  cidr_range->mutable_prefix_len()->set_value(16);
  match->add_source_ports(1025);
  match->set_transport_protocol("raw_buffer");
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:filter_chains[0].filter_chain_match.prefix_ranges[0]"
            ".address_prefix error:Failed to parse address:foobar:0; "
            "field:filter_chains[0].filter_chain_match.source_prefix_ranges[0]"
            ".address_prefix error:Failed to parse address:invalid:0]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, DuplicateMatchOnDestinationPrefixRanges) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* match = filter_chain->mutable_filter_chain_match();
  auto* cidr_range = match->add_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(24);
  cidr_range = match->add_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(16);
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  match = filter_chain->mutable_filter_chain_match();
  cidr_range = match->add_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(24);
  cidr_range = match->add_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(32);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: [field:filter_chains "
            "error:duplicate matching rules detected when adding filter chain: "
            "{prefix_ranges={{address_prefix=1.2.3.0:0, prefix_len=24}, "
            "{address_prefix=1.2.3.4:0, prefix_len=32}}}]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, DuplicateMatchOnTransportProtocol) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  filter_chain->mutable_filter_chain_match()->set_transport_protocol(
      "raw_buffer");
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  filter_chain->mutable_filter_chain_match()->set_transport_protocol(
      "raw_buffer");
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: [field:filter_chains "
            "error:duplicate matching rules detected when adding filter chain: "
            "{transport_protocol=raw_buffer}]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, DuplicateMatchOnSourceType) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* match = filter_chain->mutable_filter_chain_match();
  match->set_source_type(match->SAME_IP_OR_LOOPBACK);
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  match = filter_chain->mutable_filter_chain_match();
  match->set_source_type(match->SAME_IP_OR_LOOPBACK);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: [field:filter_chains "
            "error:duplicate matching rules detected when adding filter chain: "
            "{source_type=SAME_IP_OR_LOOPBACK}]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, DuplicateMatchOnSourcePrefixRanges) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* match = filter_chain->mutable_filter_chain_match();
  auto* cidr_range = match->add_source_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(24);
  cidr_range = match->add_source_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(16);
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  match = filter_chain->mutable_filter_chain_match();
  cidr_range = match->add_source_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(24);
  cidr_range = match->add_source_prefix_ranges();
  cidr_range->set_address_prefix("1.2.3.4");
  cidr_range->mutable_prefix_len()->set_value(32);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: [field:filter_chains "
            "error:duplicate matching rules detected when adding filter chain: "
            "{source_prefix_ranges={{address_prefix=1.2.3.0:0, prefix_len=24}, "
            "{address_prefix=1.2.3.4:0, prefix_len=32}}}]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, DuplicateMatchOnSourcePort) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  filter_chain->mutable_filter_chain_match()->add_source_ports(8080);
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  filter_chain->mutable_filter_chain_match()->add_source_ports(8080);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: [field:filter_chains "
            "error:duplicate matching rules detected when adding filter chain: "
            "{source_ports={8080}}]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, DownstreamTlsContext) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  auto* common_tls_context =
      downstream_tls_context.mutable_common_tls_context();
  auto* cert_provider =
      common_tls_context->mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* tcp_listener =
      absl::get_if<XdsListenerResource::TcpListener>(&resource.listener);
  ASSERT_NE(tcp_listener, nullptr);
  EXPECT_EQ(tcp_listener->address, "127.0.0.1:443");
  EXPECT_THAT(tcp_listener->filter_chain_map.destination_ip_vector,
              ::testing::ElementsAre());
  ASSERT_TRUE(tcp_listener->default_filter_chain.has_value());
  auto& tls_context =
      tcp_listener->default_filter_chain->downstream_tls_context;
  EXPECT_FALSE(tls_context.require_client_certificate);
  auto& cert_provider_instance =
      tls_context.common_tls_context.tls_certificate_provider_instance;
  EXPECT_EQ(cert_provider_instance.instance_name, "provider1");
  EXPECT_EQ(cert_provider_instance.certificate_name, "cert_name");
  EXPECT_TRUE(
      tls_context.common_tls_context.certificate_validation_context.Empty());
}

TEST_F(TcpListenerTest, DownstreamTlsContextWithCaCertProviderInstance) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  auto* common_tls_context =
      downstream_tls_context.mutable_common_tls_context();
  auto* cert_provider =
      common_tls_context->mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  cert_provider = common_tls_context->mutable_validation_context()
                      ->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("ca_cert_name");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* tcp_listener =
      absl::get_if<XdsListenerResource::TcpListener>(&resource.listener);
  ASSERT_NE(tcp_listener, nullptr);
  EXPECT_EQ(tcp_listener->address, "127.0.0.1:443");
  EXPECT_THAT(tcp_listener->filter_chain_map.destination_ip_vector,
              ::testing::ElementsAre());
  ASSERT_TRUE(tcp_listener->default_filter_chain.has_value());
  auto& tls_context =
      tcp_listener->default_filter_chain->downstream_tls_context;
  EXPECT_FALSE(tls_context.require_client_certificate);
  auto& cert_provider_instance =
      tls_context.common_tls_context.tls_certificate_provider_instance;
  EXPECT_EQ(cert_provider_instance.instance_name, "provider1");
  EXPECT_EQ(cert_provider_instance.certificate_name, "cert_name");
  auto& ca_cert_provider_instance =
      tls_context.common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance;
  EXPECT_EQ(ca_cert_provider_instance.instance_name, "provider1");
  EXPECT_EQ(ca_cert_provider_instance.certificate_name, "ca_cert_name");
}

TEST_F(TcpListenerTest, ClientCertificateRequired) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_require_client_certificate()->set_value(true);
  auto* common_tls_context =
      downstream_tls_context.mutable_common_tls_context();
  auto* cert_provider =
      common_tls_context->mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  cert_provider = common_tls_context->mutable_validation_context()
                      ->mutable_ca_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("ca_cert_name");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  ASSERT_TRUE(decode_result.resource.ok()) << decode_result.resource.status();
  ASSERT_TRUE(decode_result.name.has_value());
  EXPECT_EQ(*decode_result.name, "foo");
  auto& resource = static_cast<XdsListenerResource&>(**decode_result.resource);
  auto* tcp_listener =
      absl::get_if<XdsListenerResource::TcpListener>(&resource.listener);
  ASSERT_NE(tcp_listener, nullptr);
  EXPECT_EQ(tcp_listener->address, "127.0.0.1:443");
  EXPECT_THAT(tcp_listener->filter_chain_map.destination_ip_vector,
              ::testing::ElementsAre());
  ASSERT_TRUE(tcp_listener->default_filter_chain.has_value());
  auto& tls_context =
      tcp_listener->default_filter_chain->downstream_tls_context;
  EXPECT_TRUE(tls_context.require_client_certificate);
  auto& cert_provider_instance =
      tls_context.common_tls_context.tls_certificate_provider_instance;
  EXPECT_EQ(cert_provider_instance.instance_name, "provider1");
  EXPECT_EQ(cert_provider_instance.certificate_name, "cert_name");
  auto& ca_cert_provider_instance =
      tls_context.common_tls_context.certificate_validation_context
          .ca_certificate_provider_instance;
  EXPECT_EQ(ca_cert_provider_instance.instance_name, "provider1");
  EXPECT_EQ(ca_cert_provider_instance.certificate_name, "ca_cert_name");
}

// This is just one example of where CommonTlsContext::Parse() will
// generate an error, to show that we're propagating any such errors
// correctly.  An exhaustive set of tests for CommonTlsContext::Parse()
// is in xds_common_types_test.cc.
TEST_F(TcpListenerTest, UnknownCertificateProviderInstance) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  auto* common_tls_context =
      downstream_tls_context.mutable_common_tls_context();
  auto* cert_provider =
      common_tls_context->mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("fake");
  cert_provider->set_certificate_name("cert_name");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext]"
            ".common_tls_context.tls_certificate_provider_instance"
            ".instance_name "
            "error:unrecognized certificate provider instance name: fake]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, UnknownTransportSocketType) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  transport_socket->mutable_typed_config()->PackFrom(Listener());
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.config.listener.v3.Listener].type_url "
            "error:unsupported transport socket type]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, UnparseableDownstreamTlsContext) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  auto* typed_config = transport_socket->mutable_typed_config();
  typed_config->PackFrom(DownstreamTlsContext());
  typed_config->set_value(std::string("\0", 1));
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext] "
            "error:can't decode DownstreamTlsContext]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, DownstreamTlsContextInTypedStruct) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  xds::type::v3::TypedStruct typed_struct;
  typed_struct.set_type_url(
      "types.googleapis.com/"
      "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext");
  transport_socket->mutable_typed_config()->PackFrom(typed_struct);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "xds.type.v3.TypedStruct].value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext] "
            "error:can't decode DownstreamTlsContext]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, MatchSubjectAltNames) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  auto* common_tls_context =
      downstream_tls_context.mutable_common_tls_context();
  auto* cert_provider =
      common_tls_context->mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  common_tls_context->mutable_validation_context()
      ->add_match_subject_alt_names()
      ->set_exact("exact");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext]"
            ".common_tls_context "
            "error:match_subject_alt_names not supported on servers]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, NoTlsCertificateProvider) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  transport_socket->mutable_typed_config()->PackFrom(DownstreamTlsContext());
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext] "
            "error:TLS configuration provided but no "
            "tls_certificate_provider_instance found]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, RequireClientCertWithoutCaCertProvider) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_require_client_certificate()->set_value(true);
  auto* common_tls_context =
      downstream_tls_context.mutable_common_tls_context();
  auto* cert_provider =
      common_tls_context->mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext]"
            ".require_client_certificate "
            "error:client certificate required but no certificate "
            "provider instance specified for validation]")
      << decode_result.resource.status();
}

TEST_F(TcpListenerTest, UnsupportedFields) {
  Listener listener;
  listener.set_name("foo");
  HttpConnectionManager hcm;
  auto* filter = hcm.add_http_filters();
  filter->set_name("router");
  filter->mutable_typed_config()->PackFrom(Router());
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name("rds_name");
  rds->mutable_config_source()->mutable_self();
  auto* filter_chain = listener.mutable_default_filter_chain();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  auto* transport_socket = filter_chain->mutable_transport_socket();
  transport_socket->set_name("envoy.transport_sockets.tls");
  DownstreamTlsContext downstream_tls_context;
  downstream_tls_context.mutable_require_sni()->set_value(true);
  downstream_tls_context.set_ocsp_staple_policy(
      downstream_tls_context.STRICT_STAPLING);
  auto* common_tls_context =
      downstream_tls_context.mutable_common_tls_context();
  auto* cert_provider =
      common_tls_context->mutable_tls_certificate_provider_instance();
  cert_provider->set_instance_name("provider1");
  cert_provider->set_certificate_name("cert_name");
  transport_socket->mutable_typed_config()->PackFrom(downstream_tls_context);
  auto* address = listener.mutable_address()->mutable_socket_address();
  address->set_address("127.0.0.1");
  address->set_port_value(443);
  std::string serialized_resource;
  ASSERT_TRUE(listener.SerializeToString(&serialized_resource));
  auto* resource_type = XdsListenerResourceType::Get();
  auto decode_result =
      resource_type->Decode(decode_context_, serialized_resource);
  EXPECT_EQ(decode_result.resource.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decode_result.resource.status().message(),
            "errors validating server Listener: ["
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext]"
            ".ocsp_staple_policy "
            "error:value must be LENIENT_STAPLING; "
            "field:default_filter_chain.transport_socket.typed_config.value["
            "envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext]"
            ".require_sni "
            "error:field unsupported]")
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
