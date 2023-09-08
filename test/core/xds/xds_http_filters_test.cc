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

#include "src/core/ext/xds/xds_http_filters.h"

#include <stdint.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"
#include "upb/reflection/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config_protobuf.h>

#include "src/core/ext/filters/fault_injection/fault_injection_filter.h"
#include "src/core/ext/filters/fault_injection/fault_injection_service_config_parser.h"
#include "src/core/ext/filters/rbac/rbac_filter.h"
#include "src/core/ext/filters/rbac/rbac_service_config_parser.h"
#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/ext/filters/stateful_session/stateful_session_service_config_parser.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json_writer.h"
#include "src/proto/grpc/testing/xds/v3/address.pb.h"
#include "src/proto/grpc/testing/xds/v3/cookie.pb.h"
#include "src/proto/grpc/testing/xds/v3/extension.pb.h"
#include "src/proto/grpc/testing/xds/v3/fault.pb.h"
#include "src/proto/grpc/testing/xds/v3/fault_common.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/metadata.pb.h"
#include "src/proto/grpc/testing/xds/v3/path.pb.h"
#include "src/proto/grpc/testing/xds/v3/percent.pb.h"
#include "src/proto/grpc/testing/xds/v3/range.pb.h"
#include "src/proto/grpc/testing/xds/v3/rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/regex.pb.h"
#include "src/proto/grpc/testing/xds/v3/route.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.pb.h"
#include "src/proto/grpc/testing/xds/v3/stateful_session.pb.h"
#include "src/proto/grpc/testing/xds/v3/stateful_session_cookie.pb.h"
#include "src/proto/grpc/testing/xds/v3/string.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "test/core/util/scoped_env_var.h"
#include "test/core/util/test_config.h"

// IWYU pragma: no_include <google/protobuf/message.h>

namespace grpc_core {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::fault::v3::HTTPFault;
using ::envoy::extensions::filters::http::rbac::v3::RBAC;
using ::envoy::extensions::filters::http::rbac::v3::RBACPerRoute;
using ::envoy::extensions::filters::http::router::v3::Router;
using ::envoy::extensions::filters::http::stateful_session::v3::StatefulSession;
using ::envoy::extensions::filters::http::stateful_session::v3 ::
    StatefulSessionPerRoute;
using ::envoy::extensions::http::stateful_session::cookie::v3 ::
    CookieBasedSessionState;

//
// base class for filter tests
//

class XdsHttpFilterTest : public ::testing::Test {
 protected:
  XdsHttpFilterTest()
      : xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(), xds_server_, nullptr,
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

  XdsExtension MakeXdsExtension(const grpc::protobuf::Message& message) {
    google::protobuf::Any any;
    any.PackFrom(message);
    type_url_storage_ =
        std::string(absl::StripPrefix(any.type_url(), "type.googleapis.com/"));
    serialized_storage_ = std::string(any.value());
    ValidationErrors::ScopedField field(
        &errors_, absl::StrCat("http_filter.value[", type_url_storage_, "]"));
    XdsExtension extension;
    extension.type = absl::string_view(type_url_storage_);
    extension.value = absl::string_view(serialized_storage_);
    extension.validation_fields.push_back(std::move(field));
    return extension;
  }

  const XdsHttpFilterImpl* GetFilter(absl::string_view type) {
    return registry_.GetFilterForType(
        absl::StripPrefix(type, "type.googleapis.com/"));
  }

  GrpcXdsBootstrap::GrpcXdsServer xds_server_;
  RefCountedPtr<XdsClient> xds_client_;
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
  XdsResourceType::DecodeContext decode_context_;
  XdsHttpFilterRegistry registry_;
  ValidationErrors errors_;
  std::string type_url_storage_;
  std::string serialized_storage_;
};

//
// XdsHttpFilterRegistry tests
//

using XdsHttpFilterRegistryTest = XdsHttpFilterTest;

TEST_F(XdsHttpFilterRegistryTest, Basic) {
  // Start with an empty registry.
  registry_ = XdsHttpFilterRegistry(/*register_builtins=*/false);
  // Returns null when a filter has not yet been registered.
  XdsExtension extension = MakeXdsExtension(Router());
  EXPECT_EQ(GetFilter(extension.type), nullptr);
  // Now register the filter.
  auto filter = std::make_unique<XdsHttpRouterFilter>();
  auto* filter_ptr = filter.get();
  registry_.RegisterFilter(std::move(filter));
  // And check that it is now present.
  EXPECT_EQ(GetFilter(extension.type), filter_ptr);
}

using XdsHttpFilterRegistryDeathTest = XdsHttpFilterTest;

TEST_F(XdsHttpFilterRegistryDeathTest, DuplicateRegistryFails) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  ASSERT_DEATH(
      // The router filter is already in the registry.
      registry_.RegisterFilter(std::make_unique<XdsHttpRouterFilter>()), "");
}

//
// Router filter tests
//

class XdsRouterFilterTest : public XdsHttpFilterTest {
 protected:
  XdsRouterFilterTest() {
    XdsExtension extension = MakeXdsExtension(Router());
    filter_ = GetFilter(extension.type);
    GPR_ASSERT(filter_ != nullptr);
  }

  const XdsHttpFilterImpl* filter_;
};

TEST_F(XdsRouterFilterTest, Accessors) {
  EXPECT_EQ(filter_->ConfigProtoName(),
            "envoy.extensions.filters.http.router.v3.Router");
  EXPECT_EQ(filter_->OverrideConfigProtoName(), "");
  EXPECT_EQ(filter_->channel_filter(), nullptr);
  EXPECT_TRUE(filter_->IsSupportedOnClients());
  EXPECT_TRUE(filter_->IsSupportedOnServers());
  EXPECT_TRUE(filter_->IsTerminalFilter());
}

TEST_F(XdsRouterFilterTest, GenerateFilterConfig) {
  XdsExtension extension = MakeXdsExtension(Router());
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json()) << JsonDump(config->config);
}

TEST_F(XdsRouterFilterTest, GenerateFilterConfigTypedStruct) {
  XdsExtension extension = MakeXdsExtension(Router());
  extension.value = Json();
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value[envoy.extensions.filters.http.router.v3.Router] "
      "error:could not parse router filter config]")
      << status;
}

TEST_F(XdsRouterFilterTest, GenerateFilterConfigUnparseable) {
  XdsExtension extension = MakeXdsExtension(Router());
  std::string serialized_resource("\0", 1);
  extension.value = absl::string_view(serialized_resource);
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value[envoy.extensions.filters.http.router.v3.Router] "
      "error:could not parse router filter config]")
      << status;
}

TEST_F(XdsRouterFilterTest, GenerateFilterConfigOverride) {
  XdsExtension extension = MakeXdsExtension(Router());
  auto config = filter_->GenerateFilterConfigOverride(
      decode_context_, std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value[envoy.extensions.filters.http.router.v3.Router] "
      "error:router filter does not support config override]")
      << status;
}

//
// Fault injection filter tests
//

class XdsFaultInjectionFilterTest : public XdsHttpFilterTest {
 protected:
  XdsFaultInjectionFilterTest() {
    XdsExtension extension = MakeXdsExtension(HTTPFault());
    filter_ = GetFilter(extension.type);
    GPR_ASSERT(filter_ != nullptr);
  }

  const XdsHttpFilterImpl* filter_;
};

TEST_F(XdsFaultInjectionFilterTest, Accessors) {
  EXPECT_EQ(filter_->ConfigProtoName(),
            "envoy.extensions.filters.http.fault.v3.HTTPFault");
  EXPECT_EQ(filter_->OverrideConfigProtoName(), "");
  EXPECT_EQ(filter_->channel_filter(), &FaultInjectionFilter::kFilter);
  EXPECT_TRUE(filter_->IsSupportedOnClients());
  EXPECT_FALSE(filter_->IsSupportedOnServers());
  EXPECT_FALSE(filter_->IsTerminalFilter());
}

TEST_F(XdsFaultInjectionFilterTest, ModifyChannelArgs) {
  ChannelArgs args = filter_->ModifyChannelArgs(ChannelArgs());
  auto value = args.GetInt(GRPC_ARG_PARSE_FAULT_INJECTION_METHOD_CONFIG);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 1);
}

TEST_F(XdsFaultInjectionFilterTest, GenerateServiceConfigTopLevelConfig) {
  XdsHttpFilterImpl::FilterConfig config;
  config.config = Json::FromObject({{"foo", Json::FromString("bar")}});
  auto service_config = filter_->GenerateServiceConfig(config, nullptr);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_EQ(service_config->service_config_field_name, "faultInjectionPolicy");
  EXPECT_EQ(service_config->element, "{\"foo\":\"bar\"}");
}

TEST_F(XdsFaultInjectionFilterTest, GenerateServiceConfigOverrideConfig) {
  XdsHttpFilterImpl::FilterConfig top_config;
  top_config.config = Json::FromObject({{"foo", Json::FromString("bar")}});
  XdsHttpFilterImpl::FilterConfig override_config;
  override_config.config =
      Json::FromObject({{"baz", Json::FromString("quux")}});
  auto service_config =
      filter_->GenerateServiceConfig(top_config, &override_config);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_EQ(service_config->service_config_field_name, "faultInjectionPolicy");
  EXPECT_EQ(service_config->element, "{\"baz\":\"quux\"}");
}

// For the fault injection filter, GenerateFilterConfig() and
// GenerateFilterConfigOverride() accept the same input, so we want to
// run all tests for both.
class XdsFaultInjectionFilterConfigTest
    : public XdsFaultInjectionFilterTest,
      public ::testing::WithParamInterface<bool> {
 protected:
  absl::optional<XdsHttpFilterImpl::FilterConfig> GenerateConfig(
      XdsExtension extension) {
    if (GetParam()) {
      return filter_->GenerateFilterConfigOverride(
          decode_context_, std::move(extension), &errors_);
    }
    return filter_->GenerateFilterConfig(decode_context_, std::move(extension),
                                         &errors_);
  }
};

INSTANTIATE_TEST_SUITE_P(XdsFaultFilter, XdsFaultInjectionFilterConfigTest,
                         ::testing::Bool());

TEST_P(XdsFaultInjectionFilterConfigTest, EmptyConfig) {
  XdsExtension extension = MakeXdsExtension(HTTPFault());
  auto config = GenerateConfig(std::move(extension));
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json::FromObject({})) << JsonDump(config->config);
}

TEST_P(XdsFaultInjectionFilterConfigTest, BasicConfig) {
  HTTPFault fault;
  auto* abort = fault.mutable_abort();
  abort->set_grpc_status(GRPC_STATUS_UNAVAILABLE);
  abort->mutable_percentage()->set_numerator(75);
  auto* delay = fault.mutable_delay();
  auto* fixed_delay = delay->mutable_fixed_delay();
  fixed_delay->set_seconds(1);
  fixed_delay->set_nanos(500000000);
  delay->mutable_percentage()->set_numerator(25);
  fault.mutable_max_active_faults()->set_value(10);
  XdsExtension extension = MakeXdsExtension(fault);
  auto config = GenerateConfig(std::move(extension));
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(JsonDump(config->config),
            "{\"abortCode\":\"UNAVAILABLE\","
            "\"abortPercentageDenominator\":100,"
            "\"abortPercentageNumerator\":75,"
            "\"delay\":\"1.500000000s\","
            "\"delayPercentageDenominator\":100,"
            "\"delayPercentageNumerator\":25,"
            "\"maxFaults\":10}");
}

TEST_P(XdsFaultInjectionFilterConfigTest, HttpAbortCode) {
  HTTPFault fault;
  auto* abort = fault.mutable_abort();
  abort->set_http_status(404);
  XdsExtension extension = MakeXdsExtension(fault);
  auto config = GenerateConfig(std::move(extension));
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(JsonDump(config->config), "{\"abortCode\":\"UNIMPLEMENTED\"}");
}

TEST_P(XdsFaultInjectionFilterConfigTest, HeaderAbortAndDelay) {
  HTTPFault fault;
  fault.mutable_abort()->mutable_header_abort();
  fault.mutable_delay()->mutable_header_delay();
  XdsExtension extension = MakeXdsExtension(fault);
  auto config = GenerateConfig(std::move(extension));
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(
      JsonDump(config->config),
      "{\"abortCode\":\"OK\","
      "\"abortCodeHeader\":\"x-envoy-fault-abort-grpc-request\","
      "\"abortPercentageHeader\":\"x-envoy-fault-abort-percentage\","
      "\"delayHeader\":\"x-envoy-fault-delay-request\","
      "\"delayPercentageHeader\":\"x-envoy-fault-delay-request-percentage\"}");
}

TEST_P(XdsFaultInjectionFilterConfigTest, InvalidGrpcStatusCode) {
  HTTPFault fault;
  fault.mutable_abort()->set_grpc_status(17);
  XdsExtension extension = MakeXdsExtension(fault);
  auto config = GenerateConfig(std::move(extension));
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value[envoy.extensions.filters.http.fault.v3"
            ".HTTPFault].abort.grpc_status "
            "error:invalid gRPC status code: 17]")
      << status;
}

TEST_P(XdsFaultInjectionFilterConfigTest, InvalidDuration) {
  HTTPFault fault;
  fault.mutable_delay()->mutable_fixed_delay()->set_seconds(315576000001);
  XdsExtension extension = MakeXdsExtension(fault);
  auto config = GenerateConfig(std::move(extension));
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value[envoy.extensions.filters.http.fault.v3"
            ".HTTPFault].delay.fixed_delay.seconds "
            "error:value must be in the range [0, 315576000000]]")
      << status;
}

TEST_P(XdsFaultInjectionFilterConfigTest, TypedStruct) {
  XdsExtension extension = MakeXdsExtension(HTTPFault());
  extension.value = Json();
  auto config = GenerateConfig(std::move(extension));
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value[envoy.extensions.filters.http.fault.v3"
            ".HTTPFault] error:could not parse fault injection filter config]")
      << status;
}

TEST_P(XdsFaultInjectionFilterConfigTest, Unparseable) {
  XdsExtension extension = MakeXdsExtension(HTTPFault());
  std::string serialized_resource("\0", 1);
  extension.value = absl::string_view(serialized_resource);
  auto config = GenerateConfig(std::move(extension));
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value[envoy.extensions.filters.http.fault.v3"
            ".HTTPFault] error:could not parse fault injection filter config]")
      << status;
}

//
// RBAC filter tests
//

class XdsRbacFilterTest : public XdsHttpFilterTest {
 protected:
  XdsRbacFilterTest() {
    XdsExtension extension = MakeXdsExtension(RBAC());
    filter_ = GetFilter(extension.type);
    GPR_ASSERT(filter_ != nullptr);
  }

  const XdsHttpFilterImpl* filter_;
};

TEST_F(XdsRbacFilterTest, Accessors) {
  EXPECT_EQ(filter_->ConfigProtoName(),
            "envoy.extensions.filters.http.rbac.v3.RBAC");
  EXPECT_EQ(filter_->OverrideConfigProtoName(),
            "envoy.extensions.filters.http.rbac.v3.RBACPerRoute");
  EXPECT_EQ(filter_->channel_filter(), &RbacFilter::kFilterVtable);
  EXPECT_FALSE(filter_->IsSupportedOnClients());
  EXPECT_TRUE(filter_->IsSupportedOnServers());
  EXPECT_FALSE(filter_->IsTerminalFilter());
}

TEST_F(XdsRbacFilterTest, ModifyChannelArgs) {
  ChannelArgs args = filter_->ModifyChannelArgs(ChannelArgs());
  auto value = args.GetInt(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 1);
}

TEST_F(XdsRbacFilterTest, GenerateFilterConfig) {
  XdsExtension extension = MakeXdsExtension(RBAC());
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json::FromObject({})) << JsonDump(config->config);
}

TEST_F(XdsRbacFilterTest, GenerateFilterConfigTypedStruct) {
  XdsExtension extension = MakeXdsExtension(RBAC());
  extension.value = Json();
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value[envoy.extensions.filters.http.rbac.v3.RBAC] "
      "error:could not parse HTTP RBAC filter config]")
      << status;
}

TEST_F(XdsRbacFilterTest, GenerateFilterConfigUnparseable) {
  XdsExtension extension = MakeXdsExtension(RBAC());
  std::string serialized_resource("\0", 1);
  extension.value = absl::string_view(serialized_resource);
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value[envoy.extensions.filters.http.rbac.v3.RBAC] "
      "error:could not parse HTTP RBAC filter config]")
      << status;
}

TEST_F(XdsRbacFilterTest, GenerateFilterConfigOverride) {
  XdsExtension extension = MakeXdsExtension(RBACPerRoute());
  auto config = filter_->GenerateFilterConfigOverride(
      decode_context_, std::move(extension), &errors_);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->OverrideConfigProtoName());
  EXPECT_EQ(config->config, Json::FromObject({})) << JsonDump(config->config);
}

TEST_F(XdsRbacFilterTest, GenerateFilterConfigOverrideTypedStruct) {
  XdsExtension extension = MakeXdsExtension(RBACPerRoute());
  extension.value = Json();
  auto config = filter_->GenerateFilterConfigOverride(
      decode_context_, std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value[envoy.extensions.filters.http.rbac.v3"
            ".RBACPerRoute] error:could not parse RBACPerRoute]")
      << status;
}

TEST_F(XdsRbacFilterTest, GenerateFilterConfigOverrideUnparseable) {
  XdsExtension extension = MakeXdsExtension(RBACPerRoute());
  std::string serialized_resource("\0", 1);
  extension.value = absl::string_view(serialized_resource);
  auto config = filter_->GenerateFilterConfigOverride(
      decode_context_, std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value[envoy.extensions.filters.http.rbac.v3"
            ".RBACPerRoute] error:could not parse RBACPerRoute]")
      << status;
}

TEST_F(XdsRbacFilterTest, GenerateServiceConfig) {
  XdsHttpFilterImpl::FilterConfig hcm_config = {
      filter_->ConfigProtoName(),
      Json::FromObject({{"name", Json::FromString("foo")}})};
  auto config = filter_->GenerateServiceConfig(hcm_config, nullptr);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->service_config_field_name, "rbacPolicy");
  EXPECT_EQ(config->element,
            JsonDump(Json::FromObject({{"name", Json::FromString("foo")}})));
}

// For the RBAC filter, the override config is a superset of the
// top-level config, so we test all of the common fields as input for
// both GenerateFilterConfig() and GenerateFilterConfigOverride().
class XdsRbacFilterConfigTest : public XdsRbacFilterTest,
                                public ::testing::WithParamInterface<bool> {
 protected:
  absl::optional<XdsHttpFilterImpl::FilterConfig> GenerateConfig(RBAC rbac) {
    if (GetParam()) {
      RBACPerRoute rbac_per_route;
      *rbac_per_route.mutable_rbac() = rbac;
      XdsExtension extension = MakeXdsExtension(rbac_per_route);
      return filter_->GenerateFilterConfigOverride(
          decode_context_, std::move(extension), &errors_);
    }
    XdsExtension extension = MakeXdsExtension(rbac);
    return filter_->GenerateFilterConfig(decode_context_, std::move(extension),
                                         &errors_);
  }

  std::string FieldPrefix() {
    return absl::StrCat("http_filter.value[",
                        (GetParam() ? filter_->OverrideConfigProtoName()
                                    : filter_->ConfigProtoName()),
                        "]", (GetParam() ? ".rbac" : ""));
  }
};

INSTANTIATE_TEST_SUITE_P(XdsRbacFilter, XdsRbacFilterConfigTest,
                         ::testing::Bool());

TEST_P(XdsRbacFilterConfigTest, EmptyConfig) {
  auto config = GenerateConfig(RBAC());
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json::FromObject({})) << JsonDump(config->config);
}

TEST_P(XdsRbacFilterConfigTest, AllPermissionTypes) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto& policy = (*rules->mutable_policies())["policy_name"];
  // any
  policy.add_permissions()->set_any(true);
  // header exact match with invert
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name("header_name1");
  header->set_exact_match("exact_match");
  header->set_invert_match(true);
  // header regex match
  header = policy.add_permissions()->mutable_header();
  header->set_name("header_name2");
  header->mutable_safe_regex_match()->set_regex("regex_match");
  // header range match
  header = policy.add_permissions()->mutable_header();
  header->set_name("header_name3");
  auto* range = header->mutable_range_match();
  range->set_start(1);
  range->set_end(3);
  // header present match
  header = policy.add_permissions()->mutable_header();
  header->set_name("header_name4");
  header->set_present_match(true);
  // header prefix match
  header = policy.add_permissions()->mutable_header();
  header->set_name("header_name5");
  header->set_prefix_match("prefix_match");
  // header suffix match
  header = policy.add_permissions()->mutable_header();
  header->set_name("header_name6");
  header->set_suffix_match("suffix_match");
  // header contains match
  header = policy.add_permissions()->mutable_header();
  header->set_name("header_name7");
  header->set_contains_match("contains_match");
  // path exact match with ignore_case
  auto* string_matcher =
      policy.add_permissions()->mutable_url_path()->mutable_path();
  string_matcher->set_exact("exact_match");
  string_matcher->set_ignore_case(true);
  // path prefix match
  string_matcher = policy.add_permissions()->mutable_url_path()->mutable_path();
  string_matcher->set_prefix("prefix_match");
  // path suffix match
  string_matcher = policy.add_permissions()->mutable_url_path()->mutable_path();
  string_matcher->set_suffix("suffix_match");
  // path contains match
  string_matcher = policy.add_permissions()->mutable_url_path()->mutable_path();
  string_matcher->set_contains("contains_match");
  // path regex match
  string_matcher = policy.add_permissions()->mutable_url_path()->mutable_path();
  string_matcher->mutable_safe_regex()->set_regex("regex_match");
  // destination IP match with prefix len
  auto* cidr_range = policy.add_permissions()->mutable_destination_ip();
  cidr_range->set_address_prefix("127.0.0");
  cidr_range->mutable_prefix_len()->set_value(24);
  // destination IP match
  cidr_range = policy.add_permissions()->mutable_destination_ip();
  cidr_range->set_address_prefix("10.0.0");
  // destination port match
  policy.add_permissions()->set_destination_port(1234);
  // metadata match
  policy.add_permissions()->mutable_metadata();
  // metadata match with invert
  policy.add_permissions()->mutable_metadata()->set_invert(true);
  // requested server name
  string_matcher = policy.add_permissions()->mutable_requested_server_name();
  string_matcher->set_exact("exact_match");
  // not
  policy.add_permissions()->mutable_not_rule()->set_any(true);
  // and
  policy.add_permissions()->mutable_and_rules()->add_rules()->set_any(true);
  // or
  policy.add_permissions()->mutable_or_rules()->add_rules()->set_any(true);
  auto config = GenerateConfig(rbac);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(JsonDump(config->config),
            "{\"rules\":{"
            "\"action\":0,"
            "\"policies\":{"
            "\"policy_name\":{"
            "\"permissions\":["
            // any
            "{\"any\":true},"
            // header exact match with invert
            "{\"header\":"
            "{\"exactMatch\":\"exact_match\",\"invertMatch\":true,"
            "\"name\":\"header_name1\"}},"
            // header regex match
            "{\"header\":"
            "{\"invertMatch\":false,\"name\":\"header_name2\","
            "\"safeRegexMatch\":{\"regex\":\"regex_match\"}}},"
            // header range match
            "{\"header\":"
            "{\"invertMatch\":false,\"name\":\"header_name3\","
            "\"rangeMatch\":{\"end\":3,\"start\":1}}},"
            // header present match
            "{\"header\":"
            "{\"invertMatch\":false,\"name\":\"header_name4\","
            "\"presentMatch\":true}},"
            // header prefix match
            "{\"header\":"
            "{\"invertMatch\":false,\"name\":\"header_name5\","
            "\"prefixMatch\":\"prefix_match\"}},"
            // header suffix match
            "{\"header\":"
            "{\"invertMatch\":false,\"name\":\"header_name6\","
            "\"suffixMatch\":\"suffix_match\"}},"
            // header contains match
            "{\"header\":"
            "{\"containsMatch\":\"contains_match\",\"invertMatch\":false,"
            "\"name\":\"header_name7\"}},"
            // path exact match with ignore_case
            "{\"urlPath\":{\"path\":{"
            "\"exact\":\"exact_match\",\"ignoreCase\":true}}},"
            // path prefix match
            "{\"urlPath\":{\"path\":{"
            "\"ignoreCase\":false,\"prefix\":\"prefix_match\"}}},"
            // path suffix match
            "{\"urlPath\":{\"path\":{"
            "\"ignoreCase\":false,\"suffix\":\"suffix_match\"}}},"
            // path contains match
            "{\"urlPath\":{\"path\":{"
            "\"contains\":\"contains_match\",\"ignoreCase\":false}}},"
            // path regex match
            "{\"urlPath\":{\"path\":{"
            "\"ignoreCase\":false,\"safeRegex\":{\"regex\":\"regex_match\"}}}},"
            // destination IP match with prefix len
            "{\"destinationIp\":{"
            "\"addressPrefix\":\"127.0.0\",\"prefixLen\":24}},"
            // destination IP match
            "{\"destinationIp\":{\"addressPrefix\":\"10.0.0\"}},"
            // destination port match
            "{\"destinationPort\":1234},"
            // metadata match
            "{\"metadata\":{\"invert\":false}},"
            // metadata match with invert
            "{\"metadata\":{\"invert\":true}},"
            // requested server name
            "{\"requestedServerName\":{"
            "\"exact\":\"exact_match\",\"ignoreCase\":false}},"
            // not
            "{\"notRule\":{\"any\":true}},"
            // and
            "{\"andRules\":{\"rules\":[{\"any\":true}]}},"
            // or
            "{\"orRules\":{\"rules\":[{\"any\":true}]}}"
            "],"
            "\"principals\":[]"
            "}}}}");
}

TEST_P(XdsRbacFilterConfigTest, AllPrincipalTypes) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto& policy = (*rules->mutable_policies())["policy_name"];
  // any
  policy.add_principals()->set_any(true);
  // authenticated principal name
  // (not testing all possible string matchers here, since they're
  // covered in the AllPermissionTypes test above)
  auto* string_matcher = policy.add_principals()
                             ->mutable_authenticated()
                             ->mutable_principal_name();
  string_matcher->set_exact("exact_match");
  // source IP
  auto* cidr_range = policy.add_principals()->mutable_source_ip();
  cidr_range->set_address_prefix("127.0.0");
  // direct remote IP
  cidr_range = policy.add_principals()->mutable_direct_remote_ip();
  cidr_range->set_address_prefix("127.0.1");
  // remote IP
  cidr_range = policy.add_principals()->mutable_remote_ip();
  cidr_range->set_address_prefix("127.0.2");
  // header match
  // (not testing all possible header matchers here, since they're
  // covered in the AllPermissionTypes test above)
  auto* header = policy.add_principals()->mutable_header();
  header->set_name("header_name1");
  header->set_exact_match("exact_match");
  // path match
  // (not testing all possible string matchers here, since they're
  // covered in the AllPermissionTypes test above)
  string_matcher = policy.add_principals()->mutable_url_path()->mutable_path();
  string_matcher->set_exact("exact_match");
  // metadata match
  // (not testing invert here, since it's covered in the AllPermissionTypes
  // test above)
  policy.add_principals()->mutable_metadata();
  // not
  policy.add_principals()->mutable_not_id()->set_any(true);
  // and
  policy.add_principals()->mutable_and_ids()->add_ids()->set_any(true);
  // or
  policy.add_principals()->mutable_or_ids()->add_ids()->set_any(true);
  auto config = GenerateConfig(rbac);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(JsonDump(config->config),
            "{\"rules\":{"
            "\"action\":0,"
            "\"policies\":{"
            "\"policy_name\":{"
            "\"permissions\":[],"
            "\"principals\":["
            // any
            "{\"any\":true},"
            // authenticated principal name
            "{\"authenticated\":{\"principalName\":{"
            "\"exact\":\"exact_match\",\"ignoreCase\":false}}},"
            // source IP
            "{\"sourceIp\":{\"addressPrefix\":\"127.0.0\"}},"
            // direct remote IP
            "{\"directRemoteIp\":{\"addressPrefix\":\"127.0.1\"}},"
            // remote IP
            "{\"remoteIp\":{\"addressPrefix\":\"127.0.2\"}},"
            // header exact match with invert
            "{\"header\":"
            "{\"exactMatch\":\"exact_match\",\"invertMatch\":false,"
            "\"name\":\"header_name1\"}},"
            // path exact match
            "{\"urlPath\":{\"path\":{"
            "\"exact\":\"exact_match\",\"ignoreCase\":false}}},"
            // metadata match
            "{\"metadata\":{\"invert\":false}},"
            // not
            "{\"notId\":{\"any\":true}},"
            // and
            "{\"andIds\":{\"ids\":[{\"any\":true}]}},"
            // or
            "{\"orIds\":{\"ids\":[{\"any\":true}]}}"
            "]"
            "}}}}");
}

TEST_P(XdsRbacFilterConfigTest, AuditLoggingOptionsIgnoredWithFeatureDisabled) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      envoy::config::rbac::v3::RBAC_AuditLoggingOptions_AuditCondition_ON_DENY);
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      logger_config;
  auto* audit_logger = logger_config.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url(
      "/envoy.extensions.rbac.audit_loggers.stream.v3.StdoutAuditLog");
  *logging_options->add_logger_configs() = logger_config;
  auto config = GenerateConfig(rbac);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(JsonDump(config->config), "{\"rules\":{\"action\":0}}");
}

TEST_P(XdsRbacFilterConfigTest, AuditLoggingOptions) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      envoy::config::rbac::v3::RBAC_AuditLoggingOptions_AuditCondition_ON_DENY);
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      logger_config;
  auto* audit_logger = logger_config.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url(
      "/envoy.extensions.rbac.audit_loggers.stream.v3.StdoutAuditLog");
  *logging_options->add_logger_configs() = logger_config;
  auto config = GenerateConfig(rbac);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(JsonDump(config->config),
            "{\"rules\":{\"action\":0,"
            "\"audit_condition\":1,"
            "\"audit_loggers\":[{\"stdout_logger\":{}}]"
            "}}");
}

TEST_P(XdsRbacFilterConfigTest, InvalidAuditCondition) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto* logging_options = rules->mutable_audit_logging_options();
  logging_options->set_audit_condition(
      static_cast<
          envoy::config::rbac::v3::RBAC_AuditLoggingOptions_AuditCondition>(
          100));
  auto config = GenerateConfig(rbac);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            absl::StrCat("errors validating filter config: ["
                         "field:",
                         FieldPrefix(),
                         ".rules.audit_logging_options.audit_condition "
                         "error:invalid audit condition]"))
      << status;
}

TEST_P(XdsRbacFilterConfigTest, InvalidAuditLoggerConfig) {
  ScopedExperimentalEnvVar env_var("GRPC_EXPERIMENTAL_XDS_RBAC_AUDIT_LOGGING");
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto* logging_options = rules->mutable_audit_logging_options();
  envoy::config::rbac::v3::RBAC_AuditLoggingOptions::AuditLoggerConfig
      logger_config;
  auto* audit_logger = logger_config.mutable_audit_logger();
  audit_logger->mutable_typed_config()->set_type_url("/foo_logger");
  *logging_options->add_logger_configs() = logger_config;
  auto config = GenerateConfig(rbac);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            absl::StrCat("errors validating filter config: ["
                         "field:",
                         FieldPrefix(),
                         ".rules.audit_logging_options.logger_configs[0].audit_"
                         "logger.typed_config.value[foo_logger] "
                         "error:unsupported audit logger type]"))
      << status;
}

TEST_P(XdsRbacFilterConfigTest, InvalidFieldsInPolicy) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto& policy = (*rules->mutable_policies())["policy_name"];
  policy.mutable_condition();
  policy.mutable_checked_condition();
  auto config = GenerateConfig(rbac);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            absl::StrCat("errors validating filter config: ["
                         "field:",
                         FieldPrefix(),
                         ".rules.policies[policy_name].checked_condition "
                         "error:checked condition not supported; "
                         "field:",
                         FieldPrefix(),
                         ".rules.policies[policy_name].condition "
                         "error:condition not supported]"))
      << status;
}

TEST_P(XdsRbacFilterConfigTest, InvalidHeaderMatchers) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto& policy = (*rules->mutable_policies())["policy_name"];
  auto* header = policy.add_permissions()->mutable_header();
  header->set_name(":scheme");
  header->set_exact_match("exact_match");
  header = policy.add_principals()->mutable_header();
  header->set_name("grpc-foo");
  header->set_exact_match("exact_match");
  header = policy.add_principals()->mutable_header();
  header->set_name("header_name");
  auto config = GenerateConfig(rbac);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: ["
                   "field:",
                   FieldPrefix(),
                   ".rules.policies[policy_name].permissions[0].header.name "
                   "error:':scheme' not allowed in header; "
                   "field:",
                   FieldPrefix(),
                   ".rules.policies[policy_name].principals[0].header.name "
                   "error:'grpc-' prefixes not allowed in header; "
                   "field:",
                   FieldPrefix(),
                   ".rules.policies[policy_name].principals[1].header "
                   "error:invalid route header matcher specified]"))
      << status;
}

TEST_P(XdsRbacFilterConfigTest, InvalidStringMatchers) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto& policy = (*rules->mutable_policies())["policy_name"];
  policy.add_permissions()->mutable_url_path()->mutable_path();
  policy.add_principals()->mutable_url_path();
  auto config = GenerateConfig(rbac);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: ["
                   "field:",
                   FieldPrefix(),
                   ".rules.policies[policy_name].permissions[0].url_path.path "
                   "error:invalid match pattern; "
                   "field:",
                   FieldPrefix(),
                   ".rules.policies[policy_name].principals[0].url_path.path "
                   "error:field not present]"))
      << status;
}

TEST_P(XdsRbacFilterConfigTest, InvalidPermissionAndPrincipal) {
  RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(rules->ALLOW);
  auto& policy = (*rules->mutable_policies())["policy_name"];
  policy.add_permissions();
  policy.add_principals();
  auto config = GenerateConfig(rbac);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            absl::StrCat("errors validating filter config: ["
                         "field:",
                         FieldPrefix(),
                         ".rules.policies[policy_name].permissions[0] "
                         "error:invalid rule; "
                         "field:",
                         FieldPrefix(),
                         ".rules.policies[policy_name].principals[0] "
                         "error:invalid rule]"))
      << status;
}

//
// StatefulSession filter tests
//

using XdsStatefulSessionFilterDisabledTest = XdsHttpFilterTest;

TEST_F(XdsStatefulSessionFilterDisabledTest, FilterNotRegistered) {
  XdsExtension extension = MakeXdsExtension(StatefulSession());
  EXPECT_EQ(GetFilter(extension.type), nullptr);
}

class XdsStatefulSessionFilterTest : public XdsHttpFilterTest {
 protected:
  void SetUp() override {
    SetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_OVERRIDE_HOST", "true");
    registry_ = XdsHttpFilterRegistry();
    XdsExtension extension = MakeXdsExtension(StatefulSession());
    filter_ = GetFilter(extension.type);
    GPR_ASSERT(filter_ != nullptr);
  }

  void TearDown() override {
    UnsetEnv("GRPC_EXPERIMENTAL_XDS_ENABLE_OVERRIDE_HOST");
  }

  const XdsHttpFilterImpl* filter_;
};

TEST_F(XdsStatefulSessionFilterTest, Accessors) {
  EXPECT_EQ(
      filter_->ConfigProtoName(),
      "envoy.extensions.filters.http.stateful_session.v3.StatefulSession");
  EXPECT_EQ(filter_->OverrideConfigProtoName(),
            "envoy.extensions.filters.http.stateful_session.v3"
            ".StatefulSessionPerRoute");
  EXPECT_EQ(filter_->channel_filter(), &StatefulSessionFilter::kFilter);
  EXPECT_TRUE(filter_->IsSupportedOnClients());
  EXPECT_FALSE(filter_->IsSupportedOnServers());
  EXPECT_FALSE(filter_->IsTerminalFilter());
}

TEST_F(XdsStatefulSessionFilterTest, ModifyChannelArgs) {
  ChannelArgs args = filter_->ModifyChannelArgs(ChannelArgs());
  auto value = args.GetInt(GRPC_ARG_PARSE_STATEFUL_SESSION_METHOD_CONFIG);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 1);
}

TEST_F(XdsStatefulSessionFilterTest, OverrideConfigDisabled) {
  StatefulSessionPerRoute stateful_session_per_route;
  stateful_session_per_route.set_disabled(true);
  XdsExtension extension = MakeXdsExtension(stateful_session_per_route);
  auto config = filter_->GenerateFilterConfigOverride(
      decode_context_, std::move(extension), &errors_);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->OverrideConfigProtoName());
  EXPECT_EQ(config->config, Json::FromObject({})) << JsonDump(config->config);
}

TEST_F(XdsStatefulSessionFilterTest, GenerateServiceConfigNoOverride) {
  XdsHttpFilterImpl::FilterConfig hcm_config = {
      filter_->ConfigProtoName(),
      Json::FromObject({{"name", Json::FromString("foo")}})};
  auto config = filter_->GenerateServiceConfig(hcm_config, nullptr);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->service_config_field_name, "stateful_session");
  EXPECT_EQ(config->element,
            JsonDump(Json::FromObject({{"name", Json::FromString("foo")}})));
}

TEST_F(XdsStatefulSessionFilterTest, GenerateServiceConfigWithOverride) {
  XdsHttpFilterImpl::FilterConfig hcm_config = {
      filter_->ConfigProtoName(),
      Json::FromObject({{"name", Json::FromString("foo")}})};
  XdsHttpFilterImpl::FilterConfig override_config = {
      filter_->OverrideConfigProtoName(),
      Json::FromObject({{"name", Json::FromString("bar")}})};
  auto config = filter_->GenerateServiceConfig(hcm_config, &override_config);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_EQ(config->service_config_field_name, "stateful_session");
  EXPECT_EQ(config->element,
            JsonDump(Json::FromObject({{"name", Json::FromString("bar")}})));
}

TEST_F(XdsStatefulSessionFilterTest, GenerateFilterConfigTypedStruct) {
  XdsExtension extension = MakeXdsExtension(StatefulSession());
  extension.value = Json();
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value["
      "envoy.extensions.filters.http.stateful_session.v3.StatefulSession] "
      "error:could not parse stateful session filter config]")
      << status;
}

TEST_F(XdsStatefulSessionFilterTest, GenerateFilterConfigUnparseable) {
  XdsExtension extension = MakeXdsExtension(StatefulSession());
  std::string serialized_resource("\0", 1);
  extension.value = absl::string_view(serialized_resource);
  auto config = filter_->GenerateFilterConfig(decode_context_,
                                              std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value["
      "envoy.extensions.filters.http.stateful_session.v3.StatefulSession] "
      "error:could not parse stateful session filter config]")
      << status;
}

TEST_F(XdsStatefulSessionFilterTest, GenerateFilterConfigOverrideTypedStruct) {
  XdsExtension extension = MakeXdsExtension(StatefulSessionPerRoute());
  extension.value = Json();
  auto config = filter_->GenerateFilterConfigOverride(
      decode_context_, std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value["
            "envoy.extensions.filters.http.stateful_session.v3"
            ".StatefulSessionPerRoute] "
            "error:could not parse stateful session filter override config]")
      << status;
}

TEST_F(XdsStatefulSessionFilterTest, GenerateFilterConfigOverrideUnparseable) {
  XdsExtension extension = MakeXdsExtension(StatefulSessionPerRoute());
  std::string serialized_resource("\0", 1);
  extension.value = absl::string_view(serialized_resource);
  auto config = filter_->GenerateFilterConfigOverride(
      decode_context_, std::move(extension), &errors_);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "errors validating filter config: ["
            "field:http_filter.value["
            "envoy.extensions.filters.http.stateful_session.v3"
            ".StatefulSessionPerRoute] "
            "error:could not parse stateful session filter override config]")
      << status;
}

// For the stateful session filter, the override config is a superset of the
// top-level config, so we test all of the common fields as input for
// both GenerateFilterConfig() and GenerateFilterConfigOverride().
class XdsStatefulSessionFilterConfigTest
    : public XdsStatefulSessionFilterTest,
      public ::testing::WithParamInterface<bool> {
 protected:
  absl::optional<XdsHttpFilterImpl::FilterConfig> GenerateConfig(
      StatefulSession stateful_session) {
    if (GetParam()) {
      StatefulSessionPerRoute stateful_session_per_route;
      *stateful_session_per_route.mutable_stateful_session() = stateful_session;
      XdsExtension extension = MakeXdsExtension(stateful_session_per_route);
      return filter_->GenerateFilterConfigOverride(
          decode_context_, std::move(extension), &errors_);
    }
    XdsExtension extension = MakeXdsExtension(stateful_session);
    return filter_->GenerateFilterConfig(decode_context_, std::move(extension),
                                         &errors_);
  }

  std::string FieldPrefix() {
    return absl::StrCat("http_filter.value[",
                        (GetParam() ? filter_->OverrideConfigProtoName()
                                    : filter_->ConfigProtoName()),
                        "]", (GetParam() ? ".stateful_session" : ""));
  }
};

INSTANTIATE_TEST_SUITE_P(XdsStatefulSessionFilter,
                         XdsStatefulSessionFilterConfigTest, ::testing::Bool());

TEST_P(XdsStatefulSessionFilterConfigTest, MinimalConfig) {
  CookieBasedSessionState cookie_state;
  cookie_state.mutable_cookie()->set_name("foo");
  StatefulSession stateful_session;
  stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
      cookie_state);
  auto config = GenerateConfig(stateful_session);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(config->config,
            Json::FromObject({{"name", Json::FromString("foo")}}))
      << JsonDump(config->config);
}

TEST_P(XdsStatefulSessionFilterConfigTest, PathAndTtl) {
  CookieBasedSessionState cookie_state;
  auto* cookie = cookie_state.mutable_cookie();
  cookie->set_name("foo");
  cookie->set_path("/service/method");
  cookie->mutable_ttl()->set_seconds(3);
  StatefulSession stateful_session;
  stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
      cookie_state);
  auto config = GenerateConfig(stateful_session);
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json::FromObject({
                                {"name", Json::FromString("foo")},
                                {"path", Json::FromString("/service/method")},
                                {"ttl", Json::FromString("3.000000000s")},
                            }))
      << JsonDump(config->config);
}

TEST_P(XdsStatefulSessionFilterConfigTest, SessionStateUnset) {
  auto config = GenerateConfig(StatefulSession());
  ASSERT_TRUE(errors_.ok()) << errors_.status(
      absl::StatusCode::kInvalidArgument, "unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name,
            GetParam() ? filter_->OverrideConfigProtoName()
                       : filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json::FromObject({})) << JsonDump(config->config);
}

TEST_P(XdsStatefulSessionFilterConfigTest, CookieNotPresent) {
  StatefulSession stateful_session;
  stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
      CookieBasedSessionState());
  auto config = GenerateConfig(stateful_session);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: [field:", FieldPrefix(),
                   ".session_state.typed_config.value["
                   "envoy.extensions.http.stateful_session.cookie.v3"
                   ".CookieBasedSessionState].cookie "
                   "error:field not present]"))
      << status;
}

TEST_P(XdsStatefulSessionFilterConfigTest, CookieNameNotPresent) {
  CookieBasedSessionState cookie_state;
  cookie_state.mutable_cookie();
  StatefulSession stateful_session;
  stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
      cookie_state);
  auto config = GenerateConfig(stateful_session);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: [field:", FieldPrefix(),
                   ".session_state.typed_config.value["
                   "envoy.extensions.http.stateful_session.cookie.v3"
                   ".CookieBasedSessionState].cookie.name "
                   "error:field not present]"))
      << status;
}

TEST_P(XdsStatefulSessionFilterConfigTest, InvalidTtl) {
  CookieBasedSessionState cookie_state;
  auto* cookie = cookie_state.mutable_cookie();
  cookie->set_name("foo");
  cookie->mutable_ttl()->set_seconds(-1);
  StatefulSession stateful_session;
  stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
      cookie_state);
  auto config = GenerateConfig(stateful_session);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: [field:", FieldPrefix(),
                   ".session_state.typed_config.value["
                   "envoy.extensions.http.stateful_session.cookie.v3"
                   ".CookieBasedSessionState].cookie.ttl.seconds "
                   "error:value must be in the range [0, 315576000000]]"))
      << status;
}

TEST_P(XdsStatefulSessionFilterConfigTest, UnknownSessionStateType) {
  StatefulSession stateful_session;
  stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
      Router());
  auto config = GenerateConfig(stateful_session);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: [field:", FieldPrefix(),
                   ".session_state.typed_config.value["
                   "envoy.extensions.filters.http.router.v3.Router] "
                   "error:unsupported session state type]"))
      << status;
}

TEST_P(XdsStatefulSessionFilterConfigTest, TypedStructSessionState) {
  StatefulSession stateful_session;
  auto* typed_config =
      stateful_session.mutable_session_state()->mutable_typed_config();
  typed_config->PackFrom(CookieBasedSessionState());
  ::xds::type::v3::TypedStruct typed_struct;
  typed_struct.set_type_url(typed_config->type_url());
  typed_config->PackFrom(typed_struct);
  auto config = GenerateConfig(stateful_session);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: [field:", FieldPrefix(),
                   ".session_state.typed_config.value["
                   "xds.type.v3.TypedStruct].value["
                   "envoy.extensions.http.stateful_session.cookie.v3"
                   ".CookieBasedSessionState] "
                   "error:could not parse session state config]"))
      << status;
}

TEST_P(XdsStatefulSessionFilterConfigTest, UnparseableSessionState) {
  StatefulSession stateful_session;
  stateful_session.mutable_session_state()->mutable_typed_config()->PackFrom(
      CookieBasedSessionState());
  stateful_session.mutable_session_state()->mutable_typed_config()->set_value(
      std::string("\0", 1));
  auto config = GenerateConfig(stateful_session);
  absl::Status status = errors_.status(absl::StatusCode::kInvalidArgument,
                                       "errors validating filter config");
  ASSERT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      absl::StrCat("errors validating filter config: [field:", FieldPrefix(),
                   ".session_state.typed_config.value["
                   "envoy.extensions.http.stateful_session.cookie.v3"
                   ".CookieBasedSessionState] "
                   "error:could not parse session state config]"))
      << status;
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
