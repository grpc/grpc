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

#include <algorithm>
#include <string>
#include <vector>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "upb/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/proto/grpc/testing/xds/v3/fault.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_filter_rbac.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/xds/no_op_http_filter.h"
#include "test/cpp/util/config_grpc_cli.h"

namespace grpc_core {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::fault::v3::HTTPFault;
using ::envoy::extensions::filters::http::rbac::v3::RBAC;
using ::envoy::extensions::filters::http::router::v3::Router;

//
// base class for filter tests
//

class XdsHttpFilterTest : public ::testing::Test {
 protected:
  XdsHttpFilterTest() { XdsHttpFilterRegistry::Init(); }
  ~XdsHttpFilterTest() { XdsHttpFilterRegistry::Shutdown(); }

  XdsExtension MakeXdsExtension(const google::protobuf::Message& message) {
    any_storage_.PackFrom(message);
    absl::string_view type =
        absl::StripPrefix(any_storage_.type_url(), "type.googleapis.com/");
    ValidationErrors::ScopedField field(
        &errors_, absl::StrCat("http_filter.value[", type, "]"));
    XdsExtension extension;
    extension.type = type;
    extension.value = absl::string_view(any_storage_.value());
    extension.validation_fields.push_back(std::move(field));
    return extension;
  }

  static const XdsHttpFilterImpl* GetFilter(absl::string_view type) {
    return XdsHttpFilterRegistry::GetFilterForType(
        absl::StripPrefix(type, "type.googleapis.com/"));
  }

  ValidationErrors errors_;
  upb::Arena arena_;
  google::protobuf::Any any_storage_;
};

//
// XdsHttpFilterRegistry tests
//

using XdsHttpFilterRegistryTest = XdsHttpFilterTest;

TEST_F(XdsHttpFilterRegistryTest, Basic) {
  constexpr char kFilterName[] = "package.MyFilter";
  // Returns null when a filter has not yet been registered.
  EXPECT_EQ(XdsHttpFilterRegistry::GetFilterForType(kFilterName), nullptr);
  // Now register the filter.
  auto filter =
      std::make_unique<NoOpHttpFilter>(kFilterName, true, true, false);
  auto* filter_ptr = filter.get();
  XdsHttpFilterRegistry::RegisterFilter(std::move(filter));
  // And check that it is now present.
  EXPECT_EQ(XdsHttpFilterRegistry::GetFilterForType(kFilterName), filter_ptr);
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

TEST_F(XdsRouterFilterTest, Basic) {
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
  auto config = filter_->GenerateFilterConfig(std::move(extension),
                                              arena_.ptr(), &errors_);
  ASSERT_TRUE(errors_.ok()) << errors_.status("unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json()) << config->config.Dump();
}

TEST_F(XdsRouterFilterTest, GenerateFilterConfigTypedStruct) {
  XdsExtension extension = MakeXdsExtension(Router());
  extension.value = Json();
  auto config = filter_->GenerateFilterConfig(std::move(extension),
                                              arena_.ptr(), &errors_);
  absl::Status status = errors_.status("errors validating filter config");
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
  auto config = filter_->GenerateFilterConfig(std::move(extension),
                                              arena_.ptr(), &errors_);
  absl::Status status = errors_.status("errors validating filter config");
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
  auto config = filter_->GenerateFilterConfigOverride(std::move(extension),
                                                      arena_.ptr(), &errors_);
  absl::Status status = errors_.status("errors validating filter config");
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

TEST_F(XdsFaultInjectionFilterTest, Basic) {
  EXPECT_EQ(filter_->ConfigProtoName(),
            "envoy.extensions.filters.http.fault.v3.HTTPFault");
  EXPECT_EQ(filter_->OverrideConfigProtoName(), "");
  EXPECT_NE(filter_->channel_filter(), nullptr);
  EXPECT_TRUE(filter_->IsSupportedOnClients());
  EXPECT_FALSE(filter_->IsSupportedOnServers());
  EXPECT_FALSE(filter_->IsTerminalFilter());
}

TEST_F(XdsFaultInjectionFilterTest, GenerateServiceConfigTopLevelConfig) {
  XdsHttpFilterImpl::FilterConfig config;
  config.config = Json::Object{{"foo", "bar"}};
  auto service_config = filter_->GenerateServiceConfig(config, nullptr);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_EQ(service_config->service_config_field_name, "faultInjectionPolicy");
  EXPECT_EQ(service_config->element, "{\"foo\":\"bar\"}");
}

TEST_F(XdsFaultInjectionFilterTest, GenerateServiceConfigOverrideConfig) {
  XdsHttpFilterImpl::FilterConfig top_config;
  top_config.config = Json::Object{{"foo", "bar"}};
  XdsHttpFilterImpl::FilterConfig override_config;
  override_config.config = Json::Object{{"baz", "quux"}};
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
      return filter_->GenerateFilterConfigOverride(std::move(extension),
                                                   arena_.ptr(), &errors_);
    }
    return filter_->GenerateFilterConfig(std::move(extension), arena_.ptr(),
                                         &errors_);
  }
};

INSTANTIATE_TEST_SUITE_P(XdsFaultFilter, XdsFaultInjectionFilterConfigTest,
                         ::testing::Bool());

TEST_P(XdsFaultInjectionFilterConfigTest, EmptyConfig) {
  XdsExtension extension = MakeXdsExtension(HTTPFault());
  auto config = GenerateConfig(std::move(extension));
  ASSERT_TRUE(errors_.ok()) << errors_.status("unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(config->config, Json(Json::Object())) << config->config.Dump();
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
  ASSERT_TRUE(errors_.ok()) << errors_.status("unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(config->config.Dump(),
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
  ASSERT_TRUE(errors_.ok()) << errors_.status("unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(config->config.Dump(), "{\"abortCode\":\"UNIMPLEMENTED\"}");
}

TEST_P(XdsFaultInjectionFilterConfigTest, HeaderAbortAndDelay) {
  HTTPFault fault;
  fault.mutable_abort()->mutable_header_abort();
  fault.mutable_delay()->mutable_header_delay();
  XdsExtension extension = MakeXdsExtension(fault);
  auto config = GenerateConfig(std::move(extension));
  ASSERT_TRUE(errors_.ok()) << errors_.status("unexpected errors");
  ASSERT_TRUE(config.has_value());
  EXPECT_EQ(config->config_proto_type_name, filter_->ConfigProtoName());
  EXPECT_EQ(
      config->config.Dump(),
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
  absl::Status status = errors_.status("errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
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
  absl::Status status = errors_.status("errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
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
  absl::Status status = errors_.status("errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
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
  absl::Status status = errors_.status("errors validating filter config");
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      status.message(),
      "errors validating filter config: ["
      "field:http_filter.value[envoy.extensions.filters.http.fault.v3"
      ".HTTPFault] error:could not parse fault injection filter config]")
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
