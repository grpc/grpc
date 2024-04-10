//
//
// Copyright 2023 gRPC authors.
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
//
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/synchronization/notification.h"

#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::TlsChannelCredentialsOptions;

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kMessage[] = "Hello";

class NoOpCertificateVerifier : public ExternalCertificateVerifier {
 public:
  ~NoOpCertificateVerifier() override = default;

  bool Verify(grpc::experimental::TlsCustomVerificationCheckRequest*,
              std::function<void(grpc::Status)>,
              grpc::Status* sync_status) override {
    *sync_status = grpc::Status(grpc::StatusCode::OK, "");
    return true;
  }

  void Cancel(grpc::experimental::TlsCustomVerificationCheckRequest*) override {
  }
};

class TlsCredentialsTest : public ::testing::Test {
 protected:
  void RunServer(absl::Notification* notification) {
    std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
        grpc_core::testing::GetFileContents(kServerKeyPath),
        grpc_core::testing::GetFileContents(kServerCertPath)};
    grpc::SslServerCredentialsOptions ssl_options;
    ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
    ssl_options.pem_root_certs = root_cert;

    grpc::ServerBuilder builder;
    TestServiceImpl service_;

    builder
        .AddListeningPort(server_addr_, grpc::SslServerCredentials(ssl_options))
        .RegisterService(&service_);
    server_ = builder.BuildAndStart();
    notification->Notify();
    server_->Wait();
  }

  void TearDown() override {
    if (server_ != nullptr) {
      server_->Shutdown();
      server_thread_->join();
      delete server_thread_;
    }
  }

  TestServiceImpl service_;
  std::unique_ptr<Server> server_ = nullptr;
  std::thread* server_thread_ = nullptr;
  std::string server_addr_;
};

// NOLINTNEXTLINE(clang-diagnostic-unused-function)
void DoRpc(const std::string& server_addr,
           const TlsChannelCredentialsOptions& tls_options) {
  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(server_addr, TlsCredentials(tls_options));

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/10));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok());
  if (!result.ok()) {
    gpr_log(GPR_ERROR, "Echo failed: %d, %s, %s",
            static_cast<int>(result.error_code()),
            result.error_message().c_str(), result.error_details().c_str());
  }
  EXPECT_EQ(response.message(), kMessage);
}

// TODO(gregorycooke) - failing with OpenSSL1.0.2
#if OPENSSL_VERSION_NUMBER >= 0x10100000
// How do we test that skipping server certificate verification works as
// expected? Give the server credentials that chain up to a custom CA (that does
// not belong to the default or OS trust store), do not configure the client to
// have this CA in its trust store, and attempt to establish a connection
// between the client and server.
TEST_F(TlsCredentialsTest, SkipServerCertificateVerification) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(/*check_call_host=*/false);
  tls_options.set_verify_server_certs(/*verify_server_certs=*/false);

  DoRpc(server_addr_, tls_options);
}
#endif  // OPENSSL_VERSION_NUMBER >= 0x10100000

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
