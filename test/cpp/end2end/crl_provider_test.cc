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
#include <grpc/grpc_crl_provider.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"

// CRL Providers not supported for <1.1
#if OPENSSL_VERSION_NUMBER >= 0x10100000
namespace grpc {
namespace testing {
namespace {

const char* kRootPath = "test/core/tsi/test_creds/crl_data/ca.pem";
const char* kRevokedKeyPath = "test/core/tsi/test_creds/crl_data/revoked.key";
const char* kRevokedCertPath = "test/core/tsi/test_creds/crl_data/revoked.pem";
const char* kValidKeyPath = "test/core/tsi/test_creds/crl_data/valid.key";
const char* kValidCertPath = "test/core/tsi/test_creds/crl_data/valid.pem";
const char* kRootCrlPath = "test/core/tsi/test_creds/crl_data/crls/current.crl";
const char* kCrlDirectoryPath =
    "test/core/tsi/test_creds/crl_data/crl_provider_test_dir/";
constexpr char kMessage[] = "Hello";

// This test must be at the top of the file because the
// DirectoryReloaderCrlProvider gets the default event engine on construction.
// To get the default event engine, grpc_init must have been called, otherwise a
// segfault occurs. This test checks that no segfault occurs while getting the
// default event engine during the construction of a
// DirectoryReloaderCrlProvider. `grpc_init` is global state, so if another test
// runs first, then this test could pass because of another test modifying the
// global state
TEST(DirectoryReloaderCrlProviderTestNoFixture, Construction) {
  auto provider = grpc_core::experimental::CreateDirectoryReloaderCrlProvider(
      kCrlDirectoryPath, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok()) << provider.status();
}

class CrlProviderTest : public ::testing::Test {
 protected:
  void RunServer(absl::Notification* notification, absl::string_view server_key,
                 absl::string_view server_cert) {
    experimental::IdentityKeyCertPair key_cert_pair;
    std::string root = grpc_core::testing::GetFileContents(kRootPath);
    key_cert_pair.private_key = server_key.data();
    key_cert_pair.certificate_chain = server_cert.data();
    std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
    identity_key_cert_pairs.emplace_back(key_cert_pair);
    auto certificate_provider =
        std::make_shared<experimental::StaticDataCertificateProvider>(
            root, identity_key_cert_pairs);
    grpc::experimental::TlsServerCredentialsOptions options(
        certificate_provider);
    options.watch_root_certs();
    options.set_root_cert_name("root");
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");
    options.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    auto server_credentials = grpc::experimental::TlsServerCredentials(options);
    CHECK_NE(server_credentials.get(), nullptr);

    grpc::ServerBuilder builder;
    TestServiceImpl service_;

    builder.AddListeningPort(server_addr_, server_credentials);
    builder.RegisterService("foo.test.google.fr", &service_);
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

void DoRpc(const std::string& server_addr,
           const experimental::TlsChannelCredentialsOptions& tls_options,
           bool expect_success) {
  ChannelArguments channel_args;
  channel_args.SetSslTargetNameOverride("foo.test.google.fr");
  std::shared_ptr<Channel> channel = grpc::CreateCustomChannel(
      server_addr, grpc::experimental::TlsCredentials(tls_options),
      channel_args);

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/15));
  grpc::Status result = stub->Echo(&context, request, &response);
  if (expect_success) {
    EXPECT_TRUE(result.ok());
    if (!result.ok()) {
      LOG(ERROR) << result.error_message().c_str() << ", "
                 << result.error_details().c_str();
    }
    EXPECT_EQ(response.message(), kMessage);
  } else {
    EXPECT_FALSE(result.ok());
  }
}

TEST_F(CrlProviderTest, CrlProviderValidStaticProvider) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  std::string server_key = grpc_core::testing::GetFileContents(kValidKeyPath);
  std::string server_cert = grpc_core::testing::GetFileContents(kValidCertPath);
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_key, server_cert); });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kRootPath);
  std::string client_key = grpc_core::testing::GetFileContents(kValidKeyPath);
  std::string client_cert = grpc_core::testing::GetFileContents(kValidCertPath);
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = client_key;
  key_cert_pair.certificate_chain = client_cert;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          root_cert, identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  std::string root_crl = grpc_core::testing::GetFileContents(kRootCrlPath);

  absl::StatusOr<std::shared_ptr<grpc_core::experimental::CrlProvider>>
      provider = grpc_core::experimental::CreateStaticCrlProvider({root_crl});
  ASSERT_TRUE(provider.ok());

  options.set_crl_provider(*provider);
  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  DoRpc(server_addr_, options, true);
}

TEST_F(CrlProviderTest, CrlProviderRevokedServer) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  std::string server_key = grpc_core::testing::GetFileContents(kRevokedKeyPath);
  std::string server_cert =
      grpc_core::testing::GetFileContents(kRevokedCertPath);
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_key, server_cert); });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kRootPath);
  std::string client_key = grpc_core::testing::GetFileContents(kValidKeyPath);
  std::string client_cert = grpc_core::testing::GetFileContents(kValidCertPath);
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = client_key;
  key_cert_pair.certificate_chain = client_cert;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          root_cert, identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  std::string root_crl = grpc_core::testing::GetFileContents(kRootCrlPath);

  absl::StatusOr<std::shared_ptr<grpc_core::experimental::CrlProvider>>
      provider = grpc_core::experimental::CreateStaticCrlProvider({root_crl});
  ASSERT_TRUE(provider.ok());

  options.set_crl_provider(*provider);
  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  DoRpc(server_addr_, options, false);
}

TEST_F(CrlProviderTest, CrlProviderValidReloaderProvider) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  std::string server_key = grpc_core::testing::GetFileContents(kValidKeyPath);
  std::string server_cert = grpc_core::testing::GetFileContents(kValidCertPath);
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_key, server_cert); });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kRootPath);
  std::string client_key = grpc_core::testing::GetFileContents(kValidKeyPath);
  std::string client_cert = grpc_core::testing::GetFileContents(kValidCertPath);
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = client_key;
  key_cert_pair.certificate_chain = client_cert;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          root_cert, identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");

  absl::StatusOr<std::shared_ptr<grpc_core::experimental::CrlProvider>>
      provider = grpc_core::experimental::CreateDirectoryReloaderCrlProvider(
          kCrlDirectoryPath, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok());

  options.set_crl_provider(*provider);
  options.set_check_call_host(false);
  auto verifier = std::make_shared<experimental::NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);

  DoRpc(server_addr_, options, true);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

#endif  // OPENSSL_VERSION_NUMBER >= 0x10100000

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
