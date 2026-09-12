// Copyright 2026 gRPC authors.
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

#include "src/core/ext/filters/ext_authz/ext_authz_filter.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test_v2.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::testing::_;
using ::testing::AllOf;

constexpr absl::string_view kExtAuthzServerUri = "ipv4:127.0.0.1:10000";
constexpr absl::string_view kCheckMethod =
    "/envoy.service.auth.v3.Authorization/Check";
constexpr absl::string_view kPath = "/service/method";
constexpr absl::string_view kFailureModeAllowedHeader =
    "x-envoy-auth-failure-mode-allowed";
constexpr absl::string_view kTrue = "true";
constexpr absl::string_view kCustomHeaderKey = "x-custom-header";
constexpr absl::string_view kCustomHeaderValue = "custom-value";
constexpr absl::string_view kCheckResponseStatusErrorMessage = "token expired";
constexpr absl::string_view kDeniedErrorMessage =
    "ExtAuthz request is denied, error message: token expired";
constexpr absl::string_view kDisabledErrorMessage =
    "ExtAuthz filter is not enabled";
constexpr absl::string_view kChannelUnavailableErrorMessage =
    "ext_authz channel or transport not available";
constexpr absl::string_view kFilterConfigNotSetErrorMessage =
    "ext_authz: filter config not set";
constexpr absl::string_view kFilterConfigWrongTypeErrorMessage =
    "wrong config type passed to ext_authz filter: wrong_config";
constexpr absl::string_view kDeniedResponseNotPresentErrorMessage =
    "denied_response not present in CheckResponse";
constexpr absl::string_view kOkResponseNotPresentErrorMessage =
    "ok_response not present in CheckResponse";
constexpr absl::string_view kTransportErrorMessage = "transport failure";

MATCHER_P2(StatusIs, code, message, "") {
  return arg.code() == code && arg.message() == message;
}

class ExtAuthzFilterTest : public FilterTestV2<ExtAuthzFilter> {
 protected:
  void SetUp() override {
    auto fuzzing_ee =
        std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>(
            DownCast<grpc_event_engine::experimental::FuzzingEventEngine*>(
                event_engine()),
            [](grpc_event_engine::experimental::FuzzingEventEngine*) {});
    transport_factory_ =
        MakeRefCounted<FakeXdsTransportFactory>([]() {}, fuzzing_ee);
  }

  GrpcXdsServerTarget MakeServerTarget() {
    return GrpcXdsServerTarget(std::string(kExtAuthzServerUri), nullptr, {});
  }

  RefCountedPtr<ExtAuthzFilter::Config> MakeConfig(
      bool failure_mode_allow = false,
      bool failure_mode_allow_header_add = false,
      grpc_status_code status_on_error = GRPC_STATUS_PERMISSION_DENIED,
      bool with_channel = true) {
    auto config = MakeRefCounted<ExtAuthzFilter::Config>();
    config->failure_mode_allow = failure_mode_allow;
    config->failure_mode_allow_header_add = failure_mode_allow_header_add;
    config->status_on_error = status_on_error;
    if (with_channel) {
      auto server_target = MakeServerTarget();
      auto transport = transport_factory_->GetTransport(server_target, nullptr);
      config->channel_info = MakeRefCounted<ExtAuthzFilter::ExtAuthzChannel>(
          server_target, std::move(transport));
    }
    return config;
  }

  std::thread HandleUnaryCall(
      std::function<void(FakeXdsTransportFactory::FakeUnaryCall* call)>
          responder) {
    return std::thread([this, responder = std::move(responder)]() {
      auto server_target = MakeServerTarget();
      RefCountedPtr<FakeXdsTransportFactory::FakeUnaryCall> fake_call;
      for (int i = 0; i < 100; ++i) {
        fake_call = transport_factory_->WaitForUnaryCall(
            server_target, std::string(kCheckMethod).c_str());
        if (fake_call != nullptr) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      ASSERT_NE(fake_call, nullptr);
      responder(fake_call.get());
    });
  }

  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
};

TEST_F(ExtAuthzFilterTest, CreateSucceeds) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config);
  EXPECT_THAT(channel.status(), StatusIs(absl::StatusCode::kOk, ""));
}

TEST_F(ExtAuthzFilterTest, CreateFailsWithoutConfig) {
  auto channel = MakeChannel(ChannelArgs(), nullptr);
  EXPECT_THAT(channel.status(), StatusIs(absl::StatusCode::kInternal,
                                         kFilterConfigNotSetErrorMessage));
}

TEST_F(ExtAuthzFilterTest, CreateFailsWithWrongConfigType) {
  struct WrongConfig : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("wrong_config");
    }
    UniqueTypeName type() const override { return Type(); }
    std::string ToString() const override { return "wrong"; }
    bool Equals(const FilterConfig&) const override { return true; }
  };
  auto config = MakeRefCounted<WrongConfig>();
  auto channel = MakeChannel(ChannelArgs(), config);
  EXPECT_THAT(channel.status(), StatusIs(absl::StatusCode::kInternal,
                                         kFilterConfigWrongTypeErrorMessage));
}

TEST_F(ExtAuthzFilterTest, FilterDisabledAllowsCall) {
  auto config = MakeConfig();
  config->filter_enabled = 0;
  config->deny_at_disable = false;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, FilterDisabledWithDenyAtDisableFailsCall) {
  auto config = MakeConfig();
  config->filter_enabled = 0;
  config->deny_at_disable = true;
  config->status_on_error = GRPC_STATUS_PERMISSION_DENIED;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kDisabledErrorMessage))));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, FilterDisabledFailsWithCustomStatusOnError) {
  auto config = MakeConfig();
  config->filter_enabled = 0;
  config->deny_at_disable = true;
  config->status_on_error = GRPC_STATUS_UNAUTHENTICATED;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::UnauthenticatedError(
                                   kDisabledErrorMessage))));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, FilterDisabledWithoutChannelAllowsCall) {
  auto config = MakeConfig(/*failure_mode_allow=*/false,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_PERMISSION_DENIED,
                           /*with_channel=*/false);
  config->filter_enabled = 0;
  config->deny_at_disable = false;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, MissingChannelFailsCallWhenFailureModeDeny) {
  auto config = MakeConfig(/*failure_mode_allow=*/false,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_UNAVAILABLE,
                           /*with_channel=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::UnavailableError(
                                   kChannelUnavailableErrorMessage))));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, MissingChannelAllowsCallWhenFailureModeAllow) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_UNAVAILABLE,
                           /*with_channel=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest,
       MissingChannelInjectsHeaderWhenFailureModeAllowWithHeader) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/true,
                           GRPC_STATUS_UNAVAILABLE,
                           /*with_channel=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(kFailureModeAllowedHeader, kTrue)));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, TransportFailureAllowsCallWhenFailureModeAllow) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeUnaryCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        unary_call->MaybeSendStatusToClient(
            absl::UnavailableError(kTransportErrorMessage));
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest,
       TransportFailureInjectsHeaderWhenFailureModeAllowWithHeader) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/true);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeUnaryCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        unary_call->MaybeSendStatusToClient(
            absl::UnavailableError(kTransportErrorMessage));
      });
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(kFailureModeAllowedHeader, kTrue)));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, TransportFailureFailsCallWhenFailureModeDeny) {
  auto config = MakeConfig(/*failure_mode_allow=*/false,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_PERMISSION_DENIED);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeUnaryCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        unary_call->MaybeSendStatusToClient(
            absl::UnavailableError(kTransportErrorMessage));
      });
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kTransportErrorMessage))));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseAllowsCall) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeUnaryCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, NonOkStatusWithOkResponseFailsCall) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeUnaryCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(7);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kDeniedResponseNotPresentErrorMessage))));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, DeniedResponseFailsCall) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeUnaryCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(7);
        response.mutable_status()->set_message(
            std::string(kCheckResponseStatusErrorMessage));
        auto* denied = response.mutable_denied_response();
        denied->mutable_status()->set_code(
            envoy::type::v3::StatusCode::Unauthorized);
        auto* header = denied->add_headers();
        header->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header->mutable_header()->set_value(std::string(kCustomHeaderValue));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(
      &call,
      AllOf(HasMetadataResult(absl::UnauthenticatedError(kDeniedErrorMessage)),
            HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue))));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, OkStatusWithDeniedResponseFailsCall) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeUnaryCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        auto* denied = response.mutable_denied_response();
        denied->mutable_status()->set_code(
            envoy::type::v3::StatusCode::Unauthorized);
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kOkResponseNotPresentErrorMessage))));
  call.Start(call.NewClientMetadata({{":path", kPath}}));
  handler.join();
  Step();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
