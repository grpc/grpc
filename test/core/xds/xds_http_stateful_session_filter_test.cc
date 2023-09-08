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

#include <string>
#include <utility>

#include <google/protobuf/any.pb.h>
#include <google/protobuf/duration.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/stateful_session/stateful_session_filter.h"
#include "src/core/ext/filters/stateful_session/stateful_session_service_config_parser.h"
#include "src/core/ext/xds/xds_common_types.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_writer.h"
#include "src/proto/grpc/testing/xds/v3/cookie.pb.h"
#include "src/proto/grpc/testing/xds/v3/extension.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.pb.h"
#include "src/proto/grpc/testing/xds/v3/stateful_session.pb.h"
#include "src/proto/grpc/testing/xds/v3/stateful_session_cookie.pb.h"
#include "src/proto/grpc/testing/xds/v3/typed_struct.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/xds/xds_http_filter_test_lib.h"

// IWYU pragma: no_include <google/protobuf/message.h>

namespace grpc_core {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::router::v3::Router;
using ::envoy::extensions::filters::http::stateful_session::v3::StatefulSession;
using ::envoy::extensions::filters::http::stateful_session::v3 ::
    StatefulSessionPerRoute;
using ::envoy::extensions::http::stateful_session::cookie::v3 ::
    CookieBasedSessionState;

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
