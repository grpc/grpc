/*
 *
 * Copyright 2024 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/xds/grpc/xds_transport_grpc.h"

#include <grpc/grpc.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/certificate_provider_store_interface.h"
#include "src/core/xds/grpc/xds_server_grpc_interface.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

class FakeCertificateProviderStore : public CertificateProviderStoreInterface {
 public:
  RefCountedPtr<grpc_tls_certificate_provider> CreateOrGetCertificateProvider(
      absl::string_view /*key*/) override {
    return nullptr;
  }
};

class FakeXdsServerTarget : public GrpcXdsServerInterface {
 public:
  explicit FakeXdsServerTarget(std::string server_uri)
      : server_uri_(std::move(server_uri)) {
    ValidationErrors errors;
    channel_creds_config_ =
        CoreConfiguration::Get().channel_creds_registry().ParseConfig(
            "insecure", Json::FromObject({}), JsonArgs(), &errors);
    EXPECT_TRUE(errors.ok()) << errors.message("Validation error");
  }

  // XdsBootstrap::XdsServerTarget overrides
  const std::string& server_uri() const override { return server_uri_; }

  std::string Key() const override { return server_uri_; }
  bool Equals(const XdsServerTarget& other) const override {
    const auto* o = dynamic_cast<const FakeXdsServerTarget*>(&other);
    return o != nullptr && server_uri_ == o->server_uri_;
  }

  // GrpcXdsServerInterface overrides
  RefCountedPtr<const ChannelCredsConfig> channel_creds_config()
      const override {
    return channel_creds_config_;
  }
  const std::vector<RefCountedPtr<const CallCredsConfig>>& call_creds_configs()
      const override {
    return call_creds_configs_;
  }

 private:
  std::string server_uri_;
  RefCountedPtr<const ChannelCredsConfig> channel_creds_config_;
  std::vector<RefCountedPtr<const CallCredsConfig>> call_creds_configs_;
};

class TestServiceImpl : public grpc::CallbackGenericService {
 public:
  grpc::ServerGenericBidiReactor* CreateReactor(
      grpc::GenericCallbackServerContext* /*ctx*/) override {
    class Reactor : public grpc::ServerGenericBidiReactor {
     public:
      Reactor() { StartRead(&request_); }
      void OnReadDone(bool ok) override {
        if (!ok) return;
        std::vector<grpc::Slice> slices;
        if (!request_.Dump(&slices).ok()) return;
        std::string payload;
        for (const auto& slice : slices) {
          payload.append(reinterpret_cast<const char*>(slice.begin()),
                         slice.size());
        }

        if (payload == "return_error") {
          Finish(grpc::Status(grpc::StatusCode::INTERNAL, "test error"));
          return;
        }

        // Echo back based on content, or just simple echo
        response_ = request_;
        StartWrite(&response_);
      }
      void OnWriteDone(bool ok) override {
        if (!ok) return;
        Finish(grpc::Status::OK);
      }
      void OnDone() override { delete this; }

      grpc::ByteBuffer request_;
      grpc::ByteBuffer response_;
    };
    return new Reactor();
  }
};

class XdsTransportGrpcTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    grpc::ServerBuilder builder;
    int port;
    builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(),
                             &port);
    builder.RegisterCallbackGenericService(&service_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(server_, nullptr);
    ASSERT_GT(port, 0);
    server_uri_ = absl::StrCat("localhost:", port);

    factory_ = MakeRefCounted<GrpcXdsTransportFactory>(
        ChannelArgs(), MakeRefCounted<FakeCertificateProviderStore>());
  }

  void TearDown() override {
    factory_.reset();
    if (server_) server_->Shutdown();
    grpc_shutdown();
  }

  TestServiceImpl service_;
  std::unique_ptr<grpc::Server> server_;
  std::string server_uri_;
  RefCountedPtr<GrpcXdsTransportFactory> factory_;
};

TEST_F(XdsTransportGrpcTest, UnaryCallHasPayload) {
  ExecCtx exec_ctx;
  FakeXdsServerTarget target(server_uri_);
  absl::Status status;
  auto transport = factory_->GetTransport(target, &status);
  ASSERT_TRUE(status.ok());
  ASSERT_NE(transport, nullptr);

  auto call = transport->CreateUnaryCall("/test.Method/Unary");
  ASSERT_NE(call, nullptr);

  std::string payload = "hello world";
  auto result = call->SendMessage(payload);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, payload);
}

TEST_F(XdsTransportGrpcTest, UnaryCallLargePayload) {
  ExecCtx exec_ctx;
  FakeXdsServerTarget target(server_uri_);
  absl::Status status;
  auto transport = factory_->GetTransport(target, &status);
  ASSERT_TRUE(status.ok());

  auto call = transport->CreateUnaryCall("/test.Method/Unary");
  std::string payload(100 * 1024, 'a');  // 100KB
  auto result = call->SendMessage(payload);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, payload);
}

TEST_F(XdsTransportGrpcTest, UnaryCallReturnsError) {
  ExecCtx exec_ctx;
  FakeXdsServerTarget target(server_uri_);
  absl::Status status;
  auto transport = factory_->GetTransport(target, &status);
  ASSERT_TRUE(status.ok());

  auto call = transport->CreateUnaryCall("/test.Method/Unary");
  std::string payload = "return_error";
  auto result = call->SendMessage(payload);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(result.status().message(), "test error");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
