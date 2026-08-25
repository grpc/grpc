//
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
//

#include "src/core/ext/filters/ext_authz/ext_authz_client.h"

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "google/rpc/status.pb.h"
#include "src/core/ext/filters/ext_authz/ext_authz_messages.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"

namespace grpc_core {
namespace {

using ::envoy::service::auth::v3::CheckRequest;
using ::envoy::service::auth::v3::CheckResponse;
using ::envoy::service::auth::v3::DeniedHttpResponse;
using ::envoy::service::auth::v3::OkHttpResponse;
using ::envoy::type::v3::HttpStatus;

CheckResponse CreateOkCheckResponse() {
  CheckResponse response;
  response.mutable_status()->set_code(0);  // OK
  response.mutable_ok_response();
  return response;
}

CheckResponse CreateDeniedCheckResponse(envoy::type::v3::StatusCode http_code,
                                       int grpc_code = 7) {
  CheckResponse response;
  response.mutable_status()->set_code(grpc_code);
  auto* denied = response.mutable_denied_response();
  denied->mutable_status()->set_code(http_code);
  return response;
}

class ExtAuthzClientTest : public ::testing::Test {
 protected:
  class FakeXdsServerTarget : public XdsBootstrap::XdsServerTarget {
   public:
    explicit FakeXdsServerTarget(std::string server_uri)
        : server_uri_(std::move(server_uri)) {}
    const std::string& server_uri() const override { return server_uri_; }
    std::string Key() const override { return server_uri_; }
    bool Equals(const XdsServerTarget& other) const override {
      return server_uri_ ==
             static_cast<const FakeXdsServerTarget&>(other).server_uri_;
    }

   private:
    std::string server_uri_;
  };

  void SetUp() override {
    event_engine_ =
        std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
            grpc_event_engine::experimental::FuzzingEventEngine::Options(),
            fuzzing_event_engine::Actions());
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        /*too_many_pending_reads_callback=*/[]() {}, event_engine_);
    auto server =
        std::make_unique<FakeXdsServerTarget>("dns:///ext_authz_server:8080");
    client_ =
        MakeRefCounted<ExtAuthzClient>(transport_factory_, std::move(server));
  }

  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<ExtAuthzClient> client_;
};

TEST_F(ExtAuthzClientTest, ServerUri) {
  EXPECT_EQ(client_->server_uri(), "dns:///ext_authz_server:8080");
}

TEST_F(ExtAuthzClientTest, ResetBackoff) {
  client_->ResetBackoff();
}

TEST_F(ExtAuthzClientTest, Check_Success) {
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.is_client_call = true;
  params.path = "/check/path";

  FakeXdsServerTarget target("dns:///ext_authz_server:8080");
  absl::Status status;
  transport_factory_->GetTransport(target, &status);

  auto future =
      std::async(std::launch::async, [&]() { return client_->Check(params); });

  // Wait for unary call to be created.
  RefCountedPtr<FakeXdsTransportFactory::FakeUnaryCall> call;
  for (int i = 0; i < 50; ++i) {
    call = transport_factory_->WaitForUnaryCall(
        target, "/envoy.service.auth.v3.Authorization/Check");
    if (call != nullptr) break;
    absl::SleepFor(absl::Milliseconds(100));
  }
  ASSERT_NE(call, nullptr);

  // Verify request
  auto request_payload = call->WaitForMessageFromClient();
  ASSERT_TRUE(request_payload.has_value());
  CheckRequest request;
  ASSERT_TRUE(request.ParseFromString(*request_payload));
  EXPECT_EQ(request.attributes().request().http().path(), "/check/path");

  // Send response
  auto response = CreateOkCheckResponse();
  std::string response_payload;
  EXPECT_TRUE(response.SerializeToString(&response_payload));

  call->SendMessageToClient(response_payload);

  // Wait for result
  auto result_or = future.get();
  ASSERT_TRUE(result_or.ok());
  EXPECT_EQ(result_or->status_code, GRPC_STATUS_OK);
  EXPECT_TRUE(std::holds_alternative<ExtAuthzResponse::OkResponse>(
      result_or->response));
}

TEST_F(ExtAuthzClientTest, Check_Denied) {
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.is_client_call = true;
  params.path = "/check/denied";

  FakeXdsServerTarget target("dns:///ext_authz_server:8080");
  absl::Status status;
  transport_factory_->GetTransport(target, &status);

  auto future =
      std::async(std::launch::async, [&]() { return client_->Check(params); });

  RefCountedPtr<FakeXdsTransportFactory::FakeUnaryCall> call;
  for (int i = 0; i < 50; ++i) {
    call = transport_factory_->WaitForUnaryCall(
        target, "/envoy.service.auth.v3.Authorization/Check");
    if (call != nullptr) break;
    absl::SleepFor(absl::Milliseconds(100));
  }
  ASSERT_NE(call, nullptr);

  auto request_payload = call->WaitForMessageFromClient();
  ASSERT_TRUE(request_payload.has_value());

  // Send Denied response
  auto response = CreateDeniedCheckResponse(envoy::type::v3::Forbidden);
  std::string response_payload;
  EXPECT_TRUE(response.SerializeToString(&response_payload));

  call->SendMessageToClient(response_payload);

  auto result_or = future.get();
  ASSERT_TRUE(result_or.ok());
  EXPECT_EQ(result_or->status_code, GRPC_STATUS_PERMISSION_DENIED);
  const auto* denied =
      std::get_if<ExtAuthzResponse::DeniedResponse>(&result_or->response);
  ASSERT_NE(denied, nullptr);
  EXPECT_EQ(denied->status, GRPC_STATUS_PERMISSION_DENIED);
}

TEST_F(ExtAuthzClientTest, Check_TransportError) {
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.is_client_call = true;
  params.path = "/check/error";

  FakeXdsServerTarget target("dns:///ext_authz_server:8080");
  absl::Status status;
  transport_factory_->GetTransport(target, &status);

  auto future =
      std::async(std::launch::async, [&]() { return client_->Check(params); });

  RefCountedPtr<FakeXdsTransportFactory::FakeUnaryCall> call;
  for (int i = 0; i < 50; ++i) {
    call = transport_factory_->WaitForUnaryCall(
        target, "/envoy.service.auth.v3.Authorization/Check");
    if (call != nullptr) break;
    absl::SleepFor(absl::Milliseconds(100));
  }
  ASSERT_NE(call, nullptr);

  auto request_payload = call->WaitForMessageFromClient();
  ASSERT_TRUE(request_payload.has_value());

  // Send transport error
  call->MaybeSendStatusToClient(absl::UnavailableError("RPC failed"));

  auto result_or = future.get();
  EXPECT_FALSE(result_or.ok());
  EXPECT_EQ(result_or.status().code(), absl::StatusCode::kUnavailable);
}

TEST_F(ExtAuthzClientTest, Check_InvalidResponse) {
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.is_client_call = true;
  params.path = "/check/invalid";

  FakeXdsServerTarget target("dns:///ext_authz_server:8080");
  absl::Status status;
  transport_factory_->GetTransport(target, &status);

  auto future =
      std::async(std::launch::async, [&]() { return client_->Check(params); });

  RefCountedPtr<FakeXdsTransportFactory::FakeUnaryCall> call;
  for (int i = 0; i < 50; ++i) {
    call = transport_factory_->WaitForUnaryCall(
        target, "/envoy.service.auth.v3.Authorization/Check");
    if (call != nullptr) break;
    absl::SleepFor(absl::Milliseconds(100));
  }
  ASSERT_NE(call, nullptr);

  auto request_payload = call->WaitForMessageFromClient();
  ASSERT_TRUE(request_payload.has_value());

  // Send invalid non-proto payload
  call->SendMessageToClient("not a valid proto payload");

  auto result_or = future.get();
  EXPECT_FALSE(result_or.ok());
}

TEST_F(ExtAuthzClientTest, CreateExtAuthzRequest_Direct) {
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.is_client_call = true;
  params.path = "/test/method";
  std::string payload = client_->CreateExtAuthzRequest(params);
  EXPECT_FALSE(payload.empty());
  CheckRequest request;
  ASSERT_TRUE(request.ParseFromString(payload));
  EXPECT_EQ(request.attributes().request().http().path(), "/test/method");
}

TEST_F(ExtAuthzClientTest, ParseExtAuthzResponse_Direct) {
  auto response = CreateOkCheckResponse();
  std::string payload;
  ASSERT_TRUE(response.SerializeToString(&payload));
  auto parsed = client_->ParseExtAuthzResponse(payload);
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed->status_code, GRPC_STATUS_OK);
  EXPECT_TRUE(
      std::holds_alternative<ExtAuthzResponse::OkResponse>(parsed->response));
}

TEST_F(ExtAuthzClientTest, ParseExtAuthzResponse_Invalid) {
  auto parsed = client_->ParseExtAuthzResponse("invalid payload");
  EXPECT_FALSE(parsed.ok());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
