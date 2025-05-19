//
//
// Copyright 2025 gRPC authors.
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
#include <fcntl.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>

#include "absl/log/log.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/tls_test_utils.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {
namespace {

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::TlsChannelCredentialsOptions;

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kMessage[] = "Hello";

class FdCredentialsTest : public ::testing::Test {
 public:
  FdCredentialsTest() { create_sockets(fd_pair_); }

 protected:
  void RunServer(absl::Notification* notification) {
    grpc::ServerBuilder builder;
    TestServiceImpl service_;
    std::unique_ptr<experimental::PassiveListener> passive_listener_;
    builder.experimental().AddPassiveListener(GetServerCredentials(),
                                              passive_listener_);
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    auto status = passive_listener_->AcceptConnectedFd(fd_pair_[1]);
    EXPECT_EQ(status, absl::OkStatus());
    notification->Notify();
    server_->Wait();
  }

  std::shared_ptr<ServerCredentials> GetServerCredentials() {
    if (credential_type_ == testing::kInsecureCredentialsType) {
      return InsecureServerCredentials();
    }
    std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
    grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair = {
        grpc_core::testing::GetFileContents(kServerKeyPath),
        grpc_core::testing::GetFileContents(kServerCertPath)};
    grpc::SslServerCredentialsOptions ssl_options;
    ssl_options.pem_key_cert_pairs.push_back(key_cert_pair);
    ssl_options.pem_root_certs = root_cert;
    return grpc::SslServerCredentials(ssl_options);
  }

  void TearDown() override {
    if (server_ != nullptr) {
      server_->Shutdown();
      server_thread_->join();
      delete server_thread_;
    }
  }

  void credential_type(const std::string& type) {
    credential_type_ = type;
  }

  static void create_sockets(int sv[2]) {
    int flags;
    grpc_create_socketpair_if_unix(sv);
    flags = fcntl(sv[0], F_GETFL, 0);
    CHECK_EQ(fcntl(sv[0], F_SETFL, flags | O_NONBLOCK), 0);
    flags = fcntl(sv[1], F_GETFL, 0);
    CHECK_EQ(fcntl(sv[1], F_SETFL, flags | O_NONBLOCK), 0);
    CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[0]) == absl::OkStatus());
    CHECK(grpc_set_socket_no_sigpipe_if_possible(sv[1]) == absl::OkStatus());
  }

  int fd_pair_[2];
  std::string credential_type_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_ = nullptr;
  std::thread* server_thread_ = nullptr;
};

void DoRpc(const std::shared_ptr<ChannelCredentials>& creds, int fd) {
  std::shared_ptr<Channel> channel =
      grpc::experimental::CreateChannelFromFd(fd, creds, ChannelArguments());
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  //   context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/10));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok());
  if (!result.ok()) {
    LOG(ERROR) << "Echo failed: " << result.error_code() << ", "
               << result.error_message() << ", " << result.error_details();
  }
  EXPECT_EQ(response.message(), kMessage);
}

TEST_F(FdCredentialsTest, InsecureVerification) {
  absl::Notification notification;
  credential_type(testing::kInsecureCredentialsType);
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();
  DoRpc(InsecureChannelCredentials(), fd_pair_[0]);
}

TEST_F(FdCredentialsTest, CertificateVerification) {
  absl::Notification notification;
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();
  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(/*check_call_host=*/false);
  tls_options.set_verify_server_certs(/*verify_server_certs=*/false);
  DoRpc(TlsCredentials(tls_options), fd_pair_[0]);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
