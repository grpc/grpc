//
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
//

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/security/tls_private_key_signer.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/end2end/tls_test_certificate_selector.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#if defined(OPENSSL_IS_BORINGSSL)

#include <openssl/digest.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_selector.h"
#include "test/cpp/end2end/tls_test_private_key_signer.h"

namespace grpc {
namespace testing {
namespace {

using ::grpc::experimental::CertificateProviderInterface;
using ::grpc::experimental::PrivateKeySigner;
using ::grpc::experimental::TlsChannelCredentialsOptions;
using ::grpc::experimental::TlsServerCredentials;
using ::grpc::experimental::TlsServerCredentialsOptions;
using ::grpc_core::CertificateSelector;
using ::testing::HasSubstr;

constexpr absl::string_view kMessage = "Hello";
constexpr absl::string_view kServerName = "foo.test.google.fr";
constexpr absl::string_view kCaPemPath = "src/core/tsi/test_creds/ca.pem";
constexpr absl::string_view kServerKeyPath =
    "src/core/tsi/test_creds/server1.key";
constexpr absl::string_view kServerCertPath =
    "src/core/tsi/test_creds/server1.pem";

class TestIdentityCertificateProvider : public CertificateProviderInterface {
 public:
  explicit TestIdentityCertificateProvider(
      std::shared_ptr<CertificateSelector> certificate_selector) {
    c_certificate_provider_ = new grpc_core::InMemoryCertificateProvider();
    CHECK_OK(c_certificate_provider_->UpdateIdentityKeyCertPair(
        std::move(certificate_selector)));
  }

  ~TestIdentityCertificateProvider() override {
    grpc_tls_certificate_provider_release(c_certificate_provider_);
  }

  grpc_tls_certificate_provider* c_provider() override {
    return c_certificate_provider_;
  }

 private:
  grpc_core::InMemoryCertificateProvider* c_certificate_provider_ = nullptr;
};

class TlsCertSelectionOffloadTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_key_ =
        grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
    server_cert_ =
        grpc_core::testing::GetFileContents(std::string(kServerCertPath));
    std::string ca_cert =
        grpc_core::testing::GetFileContents(std::string(kCaPemPath));
    auto client_certificate_provider =
        std::make_shared<experimental::InMemoryCertificateProvider>();
    ASSERT_TRUE(client_certificate_provider->UpdateRoot(ca_cert).ok());
    channel_options_.set_root_certificate_provider(client_certificate_provider);
  }

  void StartServer(
      std::shared_ptr<CertificateProviderInterface> certificate_provider,
      int handshake_timeout_ms = 0) {
    absl::Notification notification;
    server_thread_ = std::make_unique<std::thread>(
        [this, &notification,
         certificate_provider = std::move(certificate_provider),
         handshake_timeout_ms]() {
          RunServer(&notification, certificate_provider, handshake_timeout_ms);
        });
    notification.WaitForNotification();
  }

  void RunServer(absl::Notification* notification,
                 std::shared_ptr<experimental::CertificateProviderInterface>
                     server_certificate_provider,
                 int handshake_timeout_ms = 0) {
    absl::StatusOr<TlsServerCredentialsOptions> options =
        TlsServerCredentialsOptions::Create(
            std::move(server_certificate_provider));
    ASSERT_TRUE(options.ok());
    options->set_identity_cert_name("identity");
    auto server_credentials = TlsServerCredentials(*options);
    CHECK_NE(server_credentials.get(), nullptr);

    grpc::ServerBuilder builder;

    if (handshake_timeout_ms > 0) {
      builder.AddChannelArgument(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS,
                                 handshake_timeout_ms);
    }

    builder.AddListeningPort(server_addr_, server_credentials);
    builder.RegisterService(std::string(kServerName), &service_);
    server_ = builder.BuildAndStart();
    notification->Notify();
    server_->Wait();
  }

  void TearDown() override {
    if (server_ != nullptr) {
      server_->Shutdown();
    }
    if (server_thread_ != nullptr) {
      server_thread_->join();
    }
  }

  std::string server_key_;
  std::string server_cert_;
  TlsChannelCredentialsOptions channel_options_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<std::thread> server_thread_;
  std::string server_addr_;
};

grpc::Status DoRpc(
    const std::string& server_addr,
    const experimental::TlsChannelCredentialsOptions& tls_options,
    const std::string& target_override, grpc::testing::EchoResponse& response) {
  ChannelArguments channel_args;
  channel_args.SetSslTargetNameOverride(target_override);
  std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
      server_addr, grpc::experimental::TlsCredentials(tls_options),
      channel_args);
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/5));
  return stub->Echo(&context, request, &response);
}

TEST_F(TlsCertSelectionOffloadTest,
       SyncCertificateSelectionOffloadWithKeyStringSuccess) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::shared_ptr<CertificateSelector> certificate_selector =
      std::make_shared<SyncTestCertificateSelector>(server_cert_, server_key_,
                                                    kServerName);
  auto server_certificate_provider =
      std::make_shared<TestIdentityCertificateProvider>(certificate_selector);
  StartServer(server_certificate_provider);
  grpc::testing::EchoResponse response;
  grpc::Status result =
      DoRpc(server_addr_, channel_options_, std::string(kServerName), response);
  EXPECT_TRUE(result.ok()) << result.error_message() << ", "
                           << result.error_details();
  EXPECT_EQ(response.message(), kMessage);
}

TEST_F(TlsCertSelectionOffloadTest,
       SyncCertificateSelectionOffloadWithSignerSuccess) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::shared_ptr<PrivateKeySigner> private_key_signer =
      std::make_shared<SyncTestPrivateKeySigner>(server_key_);
  std::shared_ptr<CertificateSelector> certificate_selector =
      std::make_shared<SyncTestCertificateSelector>(
          server_cert_, private_key_signer, kServerName);
  auto server_certificate_provider =
      std::make_shared<TestIdentityCertificateProvider>(certificate_selector);
  StartServer(server_certificate_provider);
  grpc::testing::EchoResponse response;
  grpc::Status result =
      DoRpc(server_addr_, channel_options_, std::string(kServerName), response);
  EXPECT_TRUE(result.ok()) << result.error_message() << ", "
                           << result.error_details();
  EXPECT_EQ(response.message(), kMessage);
}

TEST_F(TlsCertSelectionOffloadTest, SyncCertificateSelectionOffloadFailure) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::shared_ptr<CertificateSelector> certificate_selector =
      std::make_shared<SyncTestCertificateSelector>(server_cert_, server_key_,
                                                    kServerName);
  auto server_certificate_provider =
      std::make_shared<TestIdentityCertificateProvider>(certificate_selector);
  StartServer(server_certificate_provider);
  grpc::testing::EchoResponse response;
  // The selector will return error because the SNI is invalid.
  grpc::Status result =
      DoRpc(server_addr_, channel_options_, "bad_target", response);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.error_message(), HasSubstr("Handshake read failed"));
  EXPECT_TRUE(response.message().empty());
}

TEST_F(TlsCertSelectionOffloadTest,
       AsyncCertificateSelectionOffloadWithKeyStringSuccess) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::shared_ptr<CertificateSelector> certificate_selector =
      std::make_shared<AsyncTestCertificateSelector>(server_cert_, server_key_,
                                                     kServerName);
  auto server_certificate_provider =
      std::make_shared<TestIdentityCertificateProvider>(certificate_selector);
  StartServer(server_certificate_provider);
  grpc::testing::EchoResponse response;
  grpc::Status result =
      DoRpc(server_addr_, channel_options_, std::string(kServerName), response);
  EXPECT_TRUE(result.ok()) << result.error_message() << ", "
                           << result.error_details();
  EXPECT_EQ(response.message(), kMessage);
}

TEST_F(TlsCertSelectionOffloadTest,
       AsyncCertificateSelectionOffloadWithSignerSuccess) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::shared_ptr<PrivateKeySigner> private_key_signer =
      std::make_shared<SyncTestPrivateKeySigner>(server_key_);
  std::shared_ptr<CertificateSelector> certificate_selector =
      std::make_shared<AsyncTestCertificateSelector>(
          server_cert_, private_key_signer, kServerName);
  auto server_certificate_provider =
      std::make_shared<TestIdentityCertificateProvider>(certificate_selector);
  StartServer(server_certificate_provider);
  grpc::testing::EchoResponse response;
  grpc::Status result =
      DoRpc(server_addr_, channel_options_, std::string(kServerName), response);
  EXPECT_TRUE(result.ok()) << result.error_message() << ", "
                           << result.error_details();
  EXPECT_EQ(response.message(), kMessage);
}

TEST_F(TlsCertSelectionOffloadTest, AsyncCertificateSelectionOffloadFailure) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  std::shared_ptr<AsyncTestCertificateSelector> certificate_selector =
      std::make_shared<AsyncTestCertificateSelector>(server_cert_, server_key_,
                                                     kServerName);
  auto server_certificate_provider =
      std::make_shared<TestIdentityCertificateProvider>(certificate_selector);
  StartServer(server_certificate_provider);
  grpc::testing::EchoResponse response;
  // The selector will return error because the SNI is invalid.
  grpc::Status result =
      DoRpc(server_addr_, channel_options_, "bad_target", response);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.error_message(), HasSubstr("Handshake read failed"));
  EXPECT_TRUE(response.message().empty());
  // The event engine task handle should not be cancellable any more.
  EXPECT_FALSE(certificate_selector->WasCancelled());
}

TEST_F(TlsCertSelectionOffloadTest, AsyncCertificateSelectionOffloadTimeout) {
  server_addr_ = absl::StrCat("localhost:", grpc_pick_unused_port_or_die());
  // Add a 10-second delay.
  std::shared_ptr<AsyncTestCertificateSelector> certificate_selector =
      std::make_shared<AsyncTestCertificateSelector>(
          server_cert_, server_key_, kServerName, /*delay=*/absl::Seconds(10));
  auto server_certificate_provider =
      std::make_shared<TestIdentityCertificateProvider>(certificate_selector);
  // Start server with a short handshake timeout.
  StartServer(server_certificate_provider, /*handshake_timeout_ms=*/500);
  grpc::testing::EchoResponse response;
  // The selector will return error because the SNI is invalid.
  grpc::Status result =
      DoRpc(server_addr_, channel_options_, std::string(kServerName), response);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.error_message(), HasSubstr("Handshake read failed"));
  EXPECT_TRUE(response.message().empty());
  EXPECT_TRUE(certificate_selector->WasCancelled());
}

}  // namespace
}  // namespace testing
}  // namespace grpc

#endif  // OPENSSL_IS_BORINGSSL

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
