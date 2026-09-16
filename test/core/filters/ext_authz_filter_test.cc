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
#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/transport/tls/tls_utils.h"
#include "src/core/handshaker/endpoint_info/endpoint_info_handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/matchers.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_common_types.h"
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
constexpr absl::string_view kPathHeader = ":path";
constexpr absl::string_view kFailureModeAllowedHeader =
    "x-envoy-auth-failure-mode-allowed";
constexpr absl::string_view kTrue = "true";
constexpr absl::string_view kCustomHeaderKey = "x-custom-header";
constexpr absl::string_view kCustomHeaderValue = "custom-value";
constexpr absl::string_view kCustomHeaderKey2 = "x-custom-header-2";
constexpr absl::string_view kCustomHeaderValue2 = "custom-value-2";
constexpr absl::string_view kServerHeaderKey = "server-header-key";
constexpr absl::string_view kServerHeaderValue = "server-header-value";
constexpr absl::string_view kTrailerKey = "trailer-key";
constexpr absl::string_view kTrailerValue = "trailer-value";
constexpr absl::string_view kTrailerKey2 = "trailer-key-2";
constexpr absl::string_view kTrailerValue2 = "trailer-value-2";
constexpr absl::string_view kZero = "0";
constexpr absl::string_view kPermissionDeniedCode = "7";
constexpr absl::string_view kUnavailableCode = "14";
constexpr absl::string_view kUnauthenticatedCode = "16";
constexpr absl::string_view kGrpcStatus = "grpc-status";
constexpr absl::string_view kGrpcMessage = "grpc-message";
constexpr absl::string_view kBackendErrorMessage = "backend error";
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
constexpr absl::string_view kHeaderMutationNotAllowedErrorMessage =
    "ExtAuthz header mutation is not allowed";
constexpr absl::string_view kTransportErrorMessage = "transport failure";
// Connection attributes used by the server-side tests.
constexpr char kUriSan[] = "spiffe://foo.com/bar/baz";
constexpr char kLocalUriSan[] = "spiffe://foo.com/server";
constexpr char kPeerAddressUri[] = "ipv4:1.2.3.4:1234";
constexpr absl::string_view kPeerAddressHost = "1.2.3.4";
constexpr int kPeerAddressPort = 1234;
constexpr char kLocalAddressUri[] = "ipv4:5.6.7.8:5678";
constexpr absl::string_view kLocalAddressHost = "5.6.7.8";
constexpr int kLocalAddressPort = 5678;
constexpr char kPemCert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "abc=\n"
    "-----END CERTIFICATE-----\n";
constexpr absl::string_view kEncodedCert =
    "-----BEGIN%20CERTIFICATE-----%0Aabc%3D%0A-----END%20CERTIFICATE-----%0A";

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
      std::function<void(FakeXdsTransportFactory::FakeStreamingCall* call)>
          responder) {
    return std::thread([this, responder = std::move(responder)]() {
      auto server_target = MakeServerTarget();
      RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> fake_call;
      for (int i = 0; i < 100; ++i) {
        if (fake_call == nullptr) {
          fake_call = transport_factory_->WaitForStream(
              server_target, std::string(kCheckMethod).c_str());
        }
        if (fake_call != nullptr && fake_call->HaveMessageFromClient()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      ASSERT_NE(fake_call, nullptr);
      responder(fake_call.get());
      fake_call->MaybeSendStatusToClient(absl::OkStatus());
    });
  }

  // Builds an auth context with the peer and local X.509 properties that
  // AttributeContext.source.principal and .destination.principal are derived
  // from.
  RefCountedPtr<grpc_auth_context> MakeAuthContext() {
    auto auth_context = MakeRefCounted<grpc_auth_context>(nullptr);
    auth_context->add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
    auth_context->add_cstring_property(GRPC_X509_LOCAL_URI_PROPERTY_NAME,
                                       kLocalUriSan);
    return auth_context;
  }

  // Channel args as seen by a filter instantiated on a server filter chain.
  // A null auth_context and with_endpoint_addresses=false model connections
  // for which that information is unavailable.
  ChannelArgs ServerArgs(RefCountedPtr<grpc_auth_context> auth_context,
                         bool with_endpoint_addresses = true) {
    ChannelArgs args = ChannelArgs().Set(GRPC_ARG_IS_SERVER_FILTER_STACK, 1);
    if (with_endpoint_addresses) {
      args = args.Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri)
                 .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kLocalAddressUri);
    }
    if (auth_context != nullptr) {
      args = args.SetObject(std::move(auth_context));
    }
    return args;
  }

  // Drives one allowed RPC through the filter and returns the serialized
  // CheckRequest that it sent to the authorization service.
  //
  // The responder thread only copies the bytes out and always replies; all
  // checking happens in the caller, after this has joined that thread, since
  // gtest assertions are not safe to run from it.
  std::string CaptureCheckRequest(Call& call, ClientMetadataHandle md) {
    std::string serialized;
    auto handler = HandleUnaryCall(
        [&serialized](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
          auto msg = unary_call->WaitForMessageFromClient();
          if (msg.has_value()) serialized = std::move(*msg);
          envoy::service::auth::v3::CheckResponse response;
          response.mutable_status()->set_code(0);
          response.mutable_ok_response();
          unary_call->SendMessageToClient(response.SerializeAsString());
        });
    EXPECT_EVENT(Started(&call, _));
    call.Start(std::move(md));
    handler.join();
    Step();
    return serialized;
  }

  envoy::service::auth::v3::CheckRequest ParseCheckRequest(
      const std::string& serialized) {
    envoy::service::auth::v3::CheckRequest parsed;
    // An empty string parses successfully as an empty message, so check
    // explicitly that the filter actually sent something.
    EXPECT_FALSE(serialized.empty()) << "no CheckRequest sent to the "
                                        "authorization service";
    EXPECT_TRUE(parsed.ParseFromString(serialized));
    return parsed;
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

TEST_F(ExtAuthzFilterTest, FilterDisabledAllows) {
  auto config = MakeConfig();
  config->filter_enabled = 0;
  config->deny_at_disable = false;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(
      Finished(&call, AllOf(HasMetadataResult(absl::OkStatus()),
                            HasMetadataKeyValue(kTrailerKey, kTrailerValue))));
  call.FinishNextFilter(call.NewServerMetadata(
      {{kGrpcStatus, kZero}, {kTrailerKey, kTrailerValue}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, FilterDisabledDenyAtDisableFails) {
  auto config = MakeConfig();
  config->filter_enabled = 0;
  config->deny_at_disable = true;
  config->status_on_error = GRPC_STATUS_PERMISSION_DENIED;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kDisabledErrorMessage))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, FilterDisabledCustomStatusOnErrorFails) {
  auto config = MakeConfig();
  config->filter_enabled = 0;
  config->deny_at_disable = true;
  config->status_on_error = GRPC_STATUS_UNAUTHENTICATED;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::UnauthenticatedError(
                                   kDisabledErrorMessage))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, FilterDisabledWithoutChannelAllows) {
  auto config = MakeConfig(/*failure_mode_allow=*/false,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_PERMISSION_DENIED,
                           /*with_channel=*/false);
  config->filter_enabled = 0;
  config->deny_at_disable = false;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, FilterDisabledServerTrailingMetadataErrorStatus) {
  auto config = MakeConfig();
  config->filter_enabled = 0;
  config->deny_at_disable = false;
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(Finished(
      &call, AllOf(HasMetadataResult(
                       absl::PermissionDeniedError(kBackendErrorMessage)),
                   HasMetadataKeyValue(kTrailerKey, kTrailerValue))));
  call.FinishNextFilter(
      call.NewServerMetadata({{kGrpcStatus, kPermissionDeniedCode},
                              {kGrpcMessage, kBackendErrorMessage},
                              {kTrailerKey, kTrailerValue}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, MissingChannelFailureModeDeny) {
  auto config = MakeConfig(/*failure_mode_allow=*/false,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_UNAVAILABLE,
                           /*with_channel=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::UnavailableError(
                                   kChannelUnavailableErrorMessage))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, MissingChannelFailureModeAllow) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_UNAVAILABLE,
                           /*with_channel=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, MissingChannelFailureModeAllowInjectsHeader) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/true,
                           GRPC_STATUS_UNAVAILABLE,
                           /*with_channel=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(kFailureModeAllowedHeader, kTrue)));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, TransportFailureFailureModeAllow) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/false);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        unary_call->MaybeSendStatusToClient(
            absl::UnavailableError(kTransportErrorMessage));
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, TransportFailureFailureModeAllowInjectsHeader) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/true);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        unary_call->MaybeSendStatusToClient(
            absl::UnavailableError(kTransportErrorMessage));
      });
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(kFailureModeAllowedHeader, kTrue)));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, FailureModeAllowServerTrailingMetadata) {
  auto config = MakeConfig(/*failure_mode_allow=*/true,
                           /*failure_mode_allow_header_add=*/true);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        unary_call->MaybeSendStatusToClient(
            absl::UnavailableError(kTransportErrorMessage));
      });
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(kFailureModeAllowedHeader, kTrue)));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(
      Finished(&call, AllOf(HasMetadataResult(absl::OkStatus()),
                            HasMetadataKeyValue(kTrailerKey, kTrailerValue))));
  call.FinishNextFilter(call.NewServerMetadata(
      {{kGrpcStatus, kZero}, {kTrailerKey, kTrailerValue}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, TransportFailureFailureModeDeny) {
  auto config = MakeConfig(/*failure_mode_allow=*/false,
                           /*failure_mode_allow_header_add=*/false,
                           GRPC_STATUS_PERMISSION_DENIED);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        unary_call->MaybeSendStatusToClient(
            absl::UnavailableError(kTransportErrorMessage));
      });
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kTransportErrorMessage))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseAllows) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(
      Finished(&call, AllOf(HasMetadataResult(absl::OkStatus()),
                            HasMetadataKeyValue(kTrailerKey, kTrailerValue))));
  call.FinishNextFilter(call.NewServerMetadata(
      {{kGrpcStatus, kZero}, {kTrailerKey, kTrailerValue}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseAdditionsBeforeRemovals) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        auto* ok = response.mutable_ok_response();
        auto* header1 = ok->add_headers();
        header1->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header1->mutable_header()->set_value(std::string(kCustomHeaderValue));
        auto* header2 = ok->add_headers();
        header2->mutable_header()->set_key(std::string(kCustomHeaderKey2));
        header2->mutable_header()->set_value(std::string(kCustomHeaderValue2));
        ok->add_headers_to_remove(std::string(kCustomHeaderKey));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(
      &call, AllOf(HasMetadataKeyValue(kCustomHeaderKey2, kCustomHeaderValue2),
                   LacksMetadataKey(kCustomHeaderKey))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  call.FinishNextFilter(call.NewServerMetadata({{kGrpcStatus, kZero}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseDisallowedMutationFailureModeAllow) {
  auto config = MakeConfig();
  config->failure_mode_allow = true;
  config->failure_mode_allow_header_add = true;
  HeaderMutationRules rules;
  rules.disallow_all = true;
  rules.disallow_is_error = true;
  config->decoder_header_mutation_rules = std::move(rules);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        auto* ok = response.mutable_ok_response();
        auto* header = ok->add_headers();
        header->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header->mutable_header()->set_value(std::string(kCustomHeaderValue));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(
      Started(&call, HasMetadataKeyValue(kFailureModeAllowedHeader, kTrue)));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  call.FinishNextFilter(call.NewServerMetadata({{kGrpcStatus, kZero}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseInjectsResponseHeaders) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        auto* ok = response.mutable_ok_response();
        auto* header1 = ok->add_response_headers_to_add();
        header1->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header1->mutable_header()->set_value(std::string(kCustomHeaderValue));
        auto* header2 = ok->add_response_headers_to_add();
        header2->mutable_header()->set_key(std::string(kCustomHeaderKey2));
        header2->mutable_header()->set_value(std::string(kCustomHeaderValue2));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call,
      AllOf(HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue),
            HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue),
            HasMetadataKeyValue(kCustomHeaderKey2, kCustomHeaderValue2))));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  call.FinishNextFilter(call.NewServerMetadata({{kGrpcStatus, kZero}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseTrailersOnlySkipsResponseHeaders) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        auto* ok = response.mutable_ok_response();
        auto* header = ok->add_response_headers_to_add();
        header->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header->mutable_header()->set_value(std::string(kCustomHeaderValue));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  auto md = call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}});
  md->Set(GrpcTrailersOnly(), true);
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, AllOf(HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue),
                   LacksMetadataKey(kCustomHeaderKey))));
  call.ForwardServerInitialMetadata(std::move(md));
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::OkStatus())));
  call.FinishNextFilter(call.NewServerMetadata({{kGrpcStatus, kZero}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseDisallowedResponseHeaderMutationFails) {
  auto config = MakeConfig();
  HeaderMutationRules rules;
  rules.disallow_all = true;
  rules.disallow_is_error = true;
  config->decoder_header_mutation_rules = std::move(rules);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        auto* ok = response.mutable_ok_response();
        auto* header = ok->add_response_headers_to_add();
        header->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header->mutable_header()->set_value(std::string(kCustomHeaderValue));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kHeaderMutationNotAllowedErrorMessage))));
  call.ForwardServerInitialMetadata(call.NewServerMetadata());
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseTrailersOnlyForwardsTrailers) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  auto md = call.NewServerMetadata(
      {{kGrpcStatus, kUnavailableCode}, {kTrailerKey, kTrailerValue}});
  md->Set(GrpcTrailersOnly(), true);
  EXPECT_EVENT(
      Finished(&call, AllOf(HasMetadataResult(absl::UnavailableError("")),
                            HasMetadataKeyValue(kTrailerKey, kTrailerValue))));
  call.FinishNextFilter(std::move(md));
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseServerTrailingMetadataOkStatus) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(Finished(
      &call, AllOf(HasMetadataResult(absl::OkStatus()),
                   HasMetadataKeyValue(kTrailerKey, kTrailerValue),
                   HasMetadataKeyValue(kTrailerKey2, kTrailerValue2))));
  call.FinishNextFilter(
      call.NewServerMetadata({{kGrpcStatus, kZero},
                              {kTrailerKey, kTrailerValue},
                              {kTrailerKey2, kTrailerValue2}}));
  Step();
}

TEST_F(ExtAuthzFilterTest, OkResponseServerTrailingMetadataErrorStatus) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  EXPECT_EVENT(ForwardedServerInitialMetadata(
      &call, HasMetadataKeyValue(kServerHeaderKey, kServerHeaderValue)));
  call.ForwardServerInitialMetadata(
      call.NewServerMetadata({{kServerHeaderKey, kServerHeaderValue}}));
  EXPECT_EVENT(Finished(
      &call,
      AllOf(HasMetadataResult(absl::UnauthenticatedError(kBackendErrorMessage)),
            HasMetadataKeyValue(kTrailerKey, kTrailerValue))));
  call.FinishNextFilter(
      call.NewServerMetadata({{kGrpcStatus, kUnauthenticatedCode},
                              {kGrpcMessage, kBackendErrorMessage},
                              {kTrailerKey, kTrailerValue}}));
  Step();
}

TEST_F(ExtAuthzFilterTest,
       OkResponseTrailersOnlyServerTrailingMetadataOkStatus) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(0);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Started(&call, _));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  auto md = call.NewServerMetadata(
      {{kGrpcStatus, kZero}, {kTrailerKey, kTrailerValue}});
  md->Set(GrpcTrailersOnly(), true);
  EXPECT_EVENT(
      Finished(&call, AllOf(HasMetadataResult(absl::OkStatus()),
                            HasMetadataKeyValue(kTrailerKey, kTrailerValue))));
  call.FinishNextFilter(std::move(md));
  Step();
}

TEST_F(ExtAuthzFilterTest, NonOkStatusWithOkResponseFails) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(7);
        response.mutable_ok_response();
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kDeniedResponseNotPresentErrorMessage))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, DeniedResponseFails) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
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
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, DeniedResponseDoesNotSendServerInitialMetadata) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(7);
        response.mutable_status()->set_message(
            std::string(kCheckResponseStatusErrorMessage));
        auto* denied = response.mutable_denied_response();
        denied->mutable_status()->set_code(
            envoy::type::v3::StatusCode::Forbidden);
        auto* header = denied->add_headers();
        header->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header->mutable_header()->set_value(std::string(kCustomHeaderValue));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(
      &call,
      AllOf(HasMetadataResult(absl::PermissionDeniedError(kDeniedErrorMessage)),
            HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest,
       DeniedResponseServerTrailingMetadataMultipleHeaders) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(7);
        response.mutable_status()->set_message(
            std::string(kCheckResponseStatusErrorMessage));
        auto* denied = response.mutable_denied_response();
        denied->mutable_status()->set_code(
            envoy::type::v3::StatusCode::Unauthorized);
        auto* header1 = denied->add_headers();
        header1->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header1->mutable_header()->set_value(std::string(kCustomHeaderValue));
        auto* header2 = denied->add_headers();
        header2->mutable_header()->set_key(std::string(kCustomHeaderKey2));
        header2->mutable_header()->set_value(std::string(kCustomHeaderValue2));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(
      &call,
      AllOf(HasMetadataResult(absl::UnauthenticatedError(kDeniedErrorMessage)),
            HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue),
            HasMetadataKeyValue(kCustomHeaderKey2, kCustomHeaderValue2))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, OkStatusWithDeniedResponseFails) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
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
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, DeniedResponseDisallowedMutationFailureModeAllow) {
  auto config = MakeConfig();
  // failure_mode_allow applies only to communication failures with the
  // ext_authz service.  A request that the service explicitly denied stays
  // denied even if the denied response headers cannot be applied.
  config->failure_mode_allow = true;
  config->failure_mode_allow_header_add = true;
  HeaderMutationRules rules;
  rules.disallow_all = true;
  rules.disallow_is_error = true;
  config->decoder_header_mutation_rules = std::move(rules);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(7);
        auto* denied = response.mutable_denied_response();
        denied->mutable_status()->set_code(
            envoy::type::v3::StatusCode::Unauthorized);
        auto* header = denied->add_headers();
        header->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header->mutable_header()->set_value(std::string(kCustomHeaderValue));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::PermissionDeniedError(
                                   kHeaderMutationNotAllowedErrorMessage))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

TEST_F(ExtAuthzFilterTest, DeniedResponseDisallowedMutationFailureModeDeny) {
  auto config = MakeConfig();
  config->failure_mode_allow = false;
  config->status_on_error = GRPC_STATUS_RESOURCE_EXHAUSTED;
  HeaderMutationRules rules;
  rules.disallow_all = true;
  rules.disallow_is_error = true;
  config->decoder_header_mutation_rules = std::move(rules);
  auto channel = MakeChannel(ChannelArgs(), config).value();
  Call call(channel);
  auto handler =
      HandleUnaryCall([](FakeXdsTransportFactory::FakeStreamingCall* unary_call) {
        auto msg = unary_call->WaitForMessageFromClient();
        ASSERT_TRUE(msg.has_value());
        envoy::service::auth::v3::CheckResponse response;
        response.mutable_status()->set_code(7);
        auto* denied = response.mutable_denied_response();
        denied->mutable_status()->set_code(
            envoy::type::v3::StatusCode::Unauthorized);
        auto* header = denied->add_headers();
        header->mutable_header()->set_key(std::string(kCustomHeaderKey));
        header->mutable_header()->set_value(std::string(kCustomHeaderValue));
        unary_call->SendMessageToClient(response.SerializeAsString());
      });
  EXPECT_EVENT(Finished(&call, HasMetadataResult(absl::ResourceExhaustedError(
                                   kHeaderMutationNotAllowedErrorMessage))));
  call.Start(call.NewClientMetadata({{kPathHeader, kPath}}));
  handler.join();
  Step();
}

//
// Server-side AttributeContext.source / .destination tests
//

TEST_F(ExtAuthzFilterTest, ServerCallPopulatesSourceAndDestination) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ServerArgs(MakeAuthContext()), config).value();
  Call call(channel);
  std::string serialized =
      CaptureCheckRequest(call, call.NewClientMetadata({{kPathHeader, kPath}}));
  auto request = ParseCheckRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  EXPECT_THAT(attr.source().principal(), ::testing::StrEq(kUriSan));
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_THAT(attr.source().address().socket_address().address(),
              ::testing::StrEq(kPeerAddressHost));
  EXPECT_EQ(attr.source().address().socket_address().port_value(),
            kPeerAddressPort);
  ASSERT_TRUE(attr.has_destination());
  // The destination principal comes from this endpoint's own certificate.
  EXPECT_THAT(attr.destination().principal(), ::testing::StrEq(kLocalUriSan));
  ASSERT_TRUE(attr.destination().address().has_socket_address());
  EXPECT_THAT(attr.destination().address().socket_address().address(),
              ::testing::StrEq(kLocalAddressHost));
  EXPECT_EQ(attr.destination().address().socket_address().port_value(),
            kLocalAddressPort);
}

// The same connection information is present in the channel args on the
// client side, but per gRFC A92 it must not be reported there.
TEST_F(ExtAuthzFilterTest, ClientCallOmitsSourceAndDestination) {
  auto config = MakeConfig();
  ChannelArgs args = ChannelArgs()
                         .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri)
                         .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kLocalAddressUri)
                         .SetObject(MakeAuthContext());
  auto channel = MakeChannel(args, config).value();
  Call call(channel);
  std::string serialized =
      CaptureCheckRequest(call, call.NewClientMetadata({{kPathHeader, kPath}}));
  auto request = ParseCheckRequest(serialized);
  ASSERT_TRUE(request.has_attributes());
  const auto& attr = request.attributes();
  EXPECT_TRUE(attr.has_request());
  EXPECT_FALSE(attr.has_source());
  EXPECT_FALSE(attr.has_destination());
}

TEST_F(ExtAuthzFilterTest, ServerCallIncludesPeerCertificateWhenConfigured) {
  auto config = MakeConfig();
  config->include_peer_certificate = true;
  auto auth_context = MakeAuthContext();
  auth_context->add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                     kPemCert);
  auto channel =
      MakeChannel(ServerArgs(std::move(auth_context)), config).value();
  Call call(channel);
  std::string serialized =
      CaptureCheckRequest(call, call.NewClientMetadata({{kPathHeader, kPath}}));
  auto request = ParseCheckRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().certificate(),
              ::testing::StrEq(kEncodedCert));
}

// The peer certificate is sent only when the config asks for it, even if the
// connection has one.
TEST_F(ExtAuthzFilterTest, ServerCallOmitsPeerCertificateByDefault) {
  auto config = MakeConfig();
  ASSERT_FALSE(config->include_peer_certificate);
  auto auth_context = MakeAuthContext();
  auth_context->add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                     kPemCert);
  auto channel =
      MakeChannel(ServerArgs(std::move(auth_context)), config).value();
  Call call(channel);
  std::string serialized =
      CaptureCheckRequest(call, call.NewClientMetadata({{kPathHeader, kPath}}));
  auto request = ParseCheckRequest(serialized);
  ASSERT_TRUE(request.attributes().has_source());
  EXPECT_THAT(request.attributes().source().certificate(),
              ::testing::IsEmpty());
}

// A connection need not have an auth context; addresses are still reported.
TEST_F(ExtAuthzFilterTest, ServerCallWithoutAuthContext) {
  auto config = MakeConfig();
  config->include_peer_certificate = true;
  auto channel = MakeChannel(ServerArgs(nullptr), config).value();
  Call call(channel);
  std::string serialized =
      CaptureCheckRequest(call, call.NewClientMetadata({{kPathHeader, kPath}}));
  auto request = ParseCheckRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  ASSERT_TRUE(attr.has_destination());
  EXPECT_THAT(attr.source().principal(), ::testing::IsEmpty());
  EXPECT_THAT(attr.source().certificate(), ::testing::IsEmpty());
  EXPECT_THAT(attr.destination().principal(), ::testing::IsEmpty());
  ASSERT_TRUE(attr.source().address().has_socket_address());
  EXPECT_THAT(attr.source().address().socket_address().address(),
              ::testing::StrEq(kPeerAddressHost));
  ASSERT_TRUE(attr.destination().address().has_socket_address());
  EXPECT_THAT(attr.destination().address().socket_address().address(),
              ::testing::StrEq(kLocalAddressHost));
}

// Without the endpoint address channel args, the peers are still reported --
// they carry the principals -- but with no address.
TEST_F(ExtAuthzFilterTest, ServerCallWithoutEndpointAddressArgs) {
  auto config = MakeConfig();
  auto channel = MakeChannel(ServerArgs(MakeAuthContext(),
                                        /*with_endpoint_addresses=*/false),
                             config)
                     .value();
  Call call(channel);
  std::string serialized =
      CaptureCheckRequest(call, call.NewClientMetadata({{kPathHeader, kPath}}));
  auto request = ParseCheckRequest(serialized);
  const auto& attr = request.attributes();
  ASSERT_TRUE(attr.has_source());
  ASSERT_TRUE(attr.has_destination());
  EXPECT_THAT(attr.source().principal(), ::testing::StrEq(kUriSan));
  EXPECT_THAT(attr.destination().principal(), ::testing::StrEq(kLocalUriSan));
  EXPECT_FALSE(attr.source().has_address());
  EXPECT_FALSE(attr.destination().has_address());
}

// The new server-side path does not disturb header filtering.
TEST_F(ExtAuthzFilterTest, ServerCallHeaderFilteringStillApplies) {
  auto config = MakeConfig();
  StringMatcher disallowed =
      StringMatcher::Create(StringMatcher::Type::kExact, kCustomHeaderKey2)
          .value();
  config->disallowed_headers.push_back(std::move(disallowed));
  auto channel = MakeChannel(ServerArgs(MakeAuthContext()), config).value();
  Call call(channel);
  std::string serialized = CaptureCheckRequest(
      call, call.NewClientMetadata({{kPathHeader, kPath},
                                    {kCustomHeaderKey, kCustomHeaderValue},
                                    {kCustomHeaderKey2, kCustomHeaderValue2}}));
  auto request = ParseCheckRequest(serialized);
  const auto& headers =
      request.attributes().request().http().header_map().headers();
  EXPECT_THAT(headers, ::testing::Contains(::testing::Property(
                           &envoy::config::core::v3::HeaderValue::key,
                           ::testing::StrEq(kCustomHeaderKey))));
  EXPECT_THAT(headers, ::testing::Not(::testing::Contains(::testing::Property(
                           &envoy::config::core::v3::HeaderValue::key,
                           ::testing::StrEq(kCustomHeaderKey2)))));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
