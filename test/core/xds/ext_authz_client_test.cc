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

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "google/rpc/status.pb.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::envoy::config::core::v3::HeaderValueOption;
using ::envoy::service::auth::v3::AttributeContext;
using ::envoy::service::auth::v3::CheckResponse;
using ::envoy::service::auth::v3::DeniedHttpResponse;
using ::envoy::service::auth::v3::OkHttpResponse;
using ::envoy::type::v3::HttpStatus;

HeaderValueOption CreateHeaderValueOption(
    absl::string_view key, absl::string_view value,
    std::optional<bool> append = std::nullopt) {
  HeaderValueOption option;
  auto* header = option.mutable_header();
  header->set_key(std::string(key));
  header->set_value(std::string(value));
  if (append.has_value()) {
    option.mutable_append()->set_value(*append);
  }
  return option;
}

OkHttpResponse CreateOkHttpResponse(
    const std::vector<HeaderValueOption>& headers,
    const std::vector<std::string>& headers_to_remove,
    const std::vector<HeaderValueOption>& response_headers_to_add) {
  OkHttpResponse response;
  for (const auto& header : headers) {
    *response.add_headers() = header;
  }
  for (const auto& header : headers_to_remove) {
    response.add_headers_to_remove(header);
  }
  for (const auto& header : response_headers_to_add) {
    *response.add_response_headers_to_add() = header;
  }
  return response;
}

DeniedHttpResponse CreateDeniedHttpResponse(
    HttpStatus& status, const std::vector<HeaderValueOption>& headers,
    const std::string& body) {
  DeniedHttpResponse response;
  *response.mutable_status() = status;
  for (const auto& header : headers) {
    *response.add_headers() = header;
  }
  response.set_body(body);
  return response;
}

CheckResponse CreateCheckResponse(const OkHttpResponse& ok_response) {
  CheckResponse response;
  response.mutable_status()->set_code(0);  // OK
  *response.mutable_ok_response() = ok_response;
  return response;
}

CheckResponse CreateCheckResponse(const DeniedHttpResponse& denied_response,
                                  int status_code = 7) {  // PERMISSION_DENIED
  CheckResponse response;
  response.mutable_status()->set_code(status_code);
  *response.mutable_denied_response() = denied_response;
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

TEST_F(ExtAuthzClientTest, ParseExtAuthzResponse_Ok) {
  std::vector<HeaderValueOption> headers = {
      CreateHeaderValueOption("x-custom-added-1", "added-value-1"),
      CreateHeaderValueOption("x-custom-added-2", "added-value-2"),
      CreateHeaderValueOption("x-custom-added-3", "added-value-3"),
  };
  std::vector<HeaderValueOption> response_headers_to_add = {
      CreateHeaderValueOption("x-custom-response-1", "response-value-1"),
      CreateHeaderValueOption("x-custom-response-2", "response-value-2"),
      CreateHeaderValueOption("x-custom-response-3", "response-value-3"),
  };
  std::vector<std::string> headers_to_remove = {
      "x-custom-removed-1",
      "x-custom-removed-2",
      "x-custom-removed-3",
  };
  OkHttpResponse ok_response =
      CreateOkHttpResponse(headers, headers_to_remove, response_headers_to_add);
  CheckResponse proto = CreateCheckResponse(ok_response);

  std::string serialized_proto;
  proto.SerializeToString(&serialized_proto);

  auto result_or = client_->ParseExtAuthzResponse(serialized_proto);
  ASSERT_TRUE(result_or.ok());
  auto result = result_or.value();
  EXPECT_EQ(result.status_code, GRPC_STATUS_OK);

  // Verify headers
  ASSERT_EQ(result.ok_response.headers.size(), 3);
  EXPECT_EQ(result.ok_response.headers[0].header.key, "x-custom-added-1");
  EXPECT_EQ(result.ok_response.headers[0].header.value, "added-value-1");
  EXPECT_EQ(result.ok_response.headers[1].header.key, "x-custom-added-2");
  EXPECT_EQ(result.ok_response.headers[1].header.value, "added-value-2");
  EXPECT_EQ(result.ok_response.headers[2].header.key, "x-custom-added-3");
  EXPECT_EQ(result.ok_response.headers[2].header.value, "added-value-3");

  // Verify headers to remove
  ASSERT_EQ(result.ok_response.headers_to_remove.size(), 3);
  EXPECT_EQ(result.ok_response.headers_to_remove[0], "x-custom-removed-1");
  EXPECT_EQ(result.ok_response.headers_to_remove[1], "x-custom-removed-2");
  EXPECT_EQ(result.ok_response.headers_to_remove[2], "x-custom-removed-3");

  // Verify response headers to add
  ASSERT_EQ(result.ok_response.response_headers_to_add.size(), 3);
  EXPECT_EQ(result.ok_response.response_headers_to_add[0].header.key,
            "x-custom-response-1");
  EXPECT_EQ(result.ok_response.response_headers_to_add[0].header.value,
            "response-value-1");
  EXPECT_EQ(result.ok_response.response_headers_to_add[1].header.key,
            "x-custom-response-2");
  EXPECT_EQ(result.ok_response.response_headers_to_add[1].header.value,
            "response-value-2");
  EXPECT_EQ(result.ok_response.response_headers_to_add[2].header.key,
            "x-custom-response-3");
  EXPECT_EQ(result.ok_response.response_headers_to_add[2].header.value,
            "response-value-3");
}

TEST_F(ExtAuthzClientTest, ParseExtAuthzResponse_Denied) {
  envoy::type::v3::HttpStatus status;
  status.set_code(envoy::type::v3::Unauthorized);
  auto header = CreateHeaderValueOption("key", "value");

  auto denied_response =
      CreateDeniedHttpResponse(status, {header}, "denied body");
  auto proto = CreateCheckResponse(denied_response);

  std::string serialized_proto;
  proto.SerializeToString(&serialized_proto);

  auto result_or = client_->ParseExtAuthzResponse(serialized_proto);
  ASSERT_TRUE(result_or.ok());
  auto result = result_or.value();
  EXPECT_EQ(result.status_code, GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_EQ(result.denied_response.status, GRPC_STATUS_UNAUTHENTICATED);
  ASSERT_EQ(result.denied_response.headers.size(), 1);
  EXPECT_EQ(result.denied_response.headers[0].header.key, "key");
  EXPECT_EQ(result.denied_response.headers[0].header.value, "value");
}

TEST_F(ExtAuthzClientTest, ParseExtAuthzResponse_Invalid) {
  absl::string_view invalid_payload = "not a proto";
  auto result_or = client_->ParseExtAuthzResponse(invalid_payload);
  EXPECT_FALSE(result_or.ok());
}

TEST_F(ExtAuthzClientTest, Check_Success) {
  ExtAuthzClient::ExtAuthzRequestParams params;
  params.is_client_call = true;
  params.path = "/check/path";

  // Ensure transport is created so WaitForUnaryCall doesn't fail immediately.
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

  // Send response
  auto response = CreateCheckResponse(CreateOkHttpResponse({}, {}, {}));
  std::string response_payload;
  response.SerializeToString(&response_payload);

  call->SendMessageToClient(response_payload);

  // Wait for result
  auto result_or = future.get();
  ASSERT_TRUE(result_or.ok());
  EXPECT_EQ(result_or.value().status_code, GRPC_STATUS_OK);
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
