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
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>
#include <mutex>
#include <utility>

#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"

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

class KeyExchangeGroupCheckingVerifier : public ExternalCertificateVerifier {
 public:
  explicit KeyExchangeGroupCheckingVerifier(absl::string_view expected_group)
      : expected_group_(expected_group) {}

  ~KeyExchangeGroupCheckingVerifier() override = default;

  bool Verify(grpc::experimental::TlsCustomVerificationCheckRequest* request,
              std::function<void(grpc::Status)>,
              grpc::Status* sync_status) override {
    grpc::string_ref negotiated_group =
        request->negotiated_key_exchange_group();
    if (negotiated_group != expected_group_) {
      *sync_status = grpc::Status(
          grpc::StatusCode::UNAUTHENTICATED,
          "Key exchange group mismatch: expected " + expected_group_ +
              ", got " +
              std::string(negotiated_group.data(), negotiated_group.length()));
    } else {
      *sync_status = grpc::Status(grpc::StatusCode::OK, "");
    }
    return true;
  }

  void Cancel(grpc::experimental::TlsCustomVerificationCheckRequest*) override {
  }

 private:
  std::string expected_group_;
};

// Intercepts server Echo to capture the TLS Exported Keying Material from the
// server's AuthContext.
class EkmCapturingService : public TestServiceImpl {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    std::shared_ptr<const AuthContext> auth_context = context->auth_context();
    if (auth_context != nullptr) {
      std::vector<grpc::string_ref> properties =
          auth_context->FindPropertyValues(
              GRPC_SSL_EXPORTED_KEYING_MATERIAL_PROPERTY_NAME);
      if (!properties.empty()) {
        std::lock_guard<std::mutex> lock(mu_);
        server_ekm_ = std::string(properties[0].data(), properties[0].length());
      }
    }
    return TestServiceImpl::Echo(context, request, response);
  }

  std::string server_ekm() const {
    std::lock_guard<std::mutex> lock(mu_);
    return server_ekm_;
  }

 private:
  mutable std::mutex mu_;
  std::string server_ekm_;
};

class TlsCredentialsTest : public ::testing::Test {
 protected:
  void RunServer(absl::Notification* notification,
                 const std::vector<grpc_tls_key_exchange_group>*
                     key_exchange_groups = nullptr,
                 const std::string* ekm_label = nullptr, size_t ekm_length = 0,
                 grpc::Service* service = nullptr) {
    std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
    std::string server_key =
        grpc_core::testing::GetFileContents(kServerKeyPath);
    std::string server_cert =
        grpc_core::testing::GetFileContents(kServerCertPath);
    grpc::experimental::IdentityKeyCertPair key_cert_pair = {server_key,
                                                             server_cert};
    std::vector<grpc::experimental::IdentityKeyCertPair>
        identity_key_cert_pairs;
    identity_key_cert_pairs.push_back(key_cert_pair);
    auto certificate_provider =
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            root_cert, identity_key_cert_pairs);
    auto server_options_or =
        grpc::experimental::TlsServerCredentialsOptions::Create(
            certificate_provider);
    ASSERT_TRUE(server_options_or.ok());
    grpc::experimental::TlsServerCredentialsOptions server_options =
        *std::move(server_options_or);
    server_options.set_root_certificate_provider(certificate_provider);
    if (key_exchange_groups != nullptr) {
      server_options.set_key_exchange_groups(*key_exchange_groups);
    }
    if (ekm_label != nullptr) {
      server_options.set_exported_keying_material_options(*ekm_label,
                                                          ekm_length);
    }
    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        server_addr_, grpc::experimental::TlsServerCredentials(server_options));
    builder.RegisterService(service != nullptr ? service : &service_);
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
           const TlsChannelCredentialsOptions& tls_options,
           absl::string_view expected_key_exchange_group = "") {
  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(server_addr, TlsCredentials(tls_options));

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/10));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok()) << "Echo failed: " << result.error_code() << ", "
                           << result.error_message() << ", "
                           << result.error_details();
  EXPECT_EQ(response.message(), kMessage);
  if (!expected_key_exchange_group.empty()) {
    std::shared_ptr<const AuthContext> auth_context = context.auth_context();
    ASSERT_NE(auth_context, nullptr);
    std::vector<grpc::string_ref> properties = auth_context->FindPropertyValues(
        GRPC_SSL_NEGOTIATED_KEY_EXCHANGE_GROUP_PROPERTY_NAME);
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(expected_key_exchange_group,
              absl::string_view(properties[0].data(), properties[0].length()));
  }
}

// NOLINTNEXTLINE(clang-diagnostic-unused-function)
void DoRpcAndExpectFailure(const std::string& server_addr,
                           const TlsChannelCredentialsOptions& tls_options,
                           grpc::StatusCode expected_code,
                           const std::string& expected_message_substr = "") {
  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(server_addr, TlsCredentials(tls_options));

  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/10));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_EQ(result.error_code(), expected_code)
      << "Expected failure with code " << expected_code << ", but got code "
      << result.error_code() << ", message: " << result.error_message();
  if (!expected_message_substr.empty()) {
    EXPECT_NE(result.error_message().find(expected_message_substr),
              std::string::npos)
        << "Expected error message containing '" << expected_message_substr
        << "', got: '" << result.error_message() << "'";
  }
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

TEST_F(TlsCredentialsTest, ExportedKeyingMaterial) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  // Configure server to capture the server side EKM.
  EkmCapturingService service;
  const std::string kEkmLabel = "test_label";
  constexpr size_t kEkmLength = 32;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification, /*key_exchange_groups=*/nullptr, &kEkmLabel,
              kEkmLength, &service);
  });
  notification.WaitForNotification();

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_verify_server_certs(false);
  tls_options.set_exported_keying_material_options(kEkmLabel, kEkmLength);

  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(server_addr_, TlsCredentials(tls_options));
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/10));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok()) << "Echo failed: " << result.error_code() << ", "
                           << result.error_message() << ", "
                           << result.error_details();
  EXPECT_EQ(response.message(), kMessage);

  std::shared_ptr<const AuthContext> client_auth_context =
      context.auth_context();
  ASSERT_NE(client_auth_context, nullptr);
  // Extract EKM from the client AuthContext and verify it's valid and equal
  // to the EKM that the server derives.
  std::vector<grpc::string_ref> client_properties =
      client_auth_context->FindPropertyValues(
          GRPC_SSL_EXPORTED_KEYING_MATERIAL_PROPERTY_NAME);
  ASSERT_EQ(client_properties.size(), 1u);
  EXPECT_EQ(client_properties[0].length(), kEkmLength);
  std::string client_ekm(client_properties[0].data(),
                         client_properties[0].length());

  EXPECT_FALSE(client_ekm.empty());
  EXPECT_EQ(service.server_ekm(), client_ekm);
}

TEST_F(TlsCredentialsTest, ExportedKeyingMaterialNotConfigured) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() { RunServer(&notification); });
  notification.WaitForNotification();

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_verify_server_certs(false);
  // Don't set EKM options on tls_options here.

  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(server_addr_, TlsCredentials(tls_options));
  auto stub = grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  request.set_message(kMessage);
  ClientContext context;
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/10));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok()) << "Echo failed: " << result.error_code() << ", "
                           << result.error_message() << ", "
                           << result.error_details();
  EXPECT_EQ(response.message(), kMessage);

  // EKM should not be in the client AuthContext since it was not configured
  // in the TlsChannelCredentialsOptions.
  std::shared_ptr<const AuthContext> client_auth_context =
      context.auth_context();
  ASSERT_NE(client_auth_context, nullptr);
  std::vector<grpc::string_ref> client_properties =
      client_auth_context->FindPropertyValues(
          GRPC_SSL_EXPORTED_KEYING_MATERIAL_PROPERTY_NAME);
  EXPECT_TRUE(client_properties.empty());
}
#endif  // OPENSSL_VERSION_NUMBER >= 0x1100000

#if defined(OPENSSL_IS_BORINGSSL)
TEST_F(TlsCredentialsTest, KeyExchangeGroupMlkem) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  const std::vector<grpc_tls_key_exchange_group> key_exchange_groups = {
      GRPC_TLS_GROUP_X25519_MLKEM768};
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, &key_exchange_groups); });
  notification.WaitForNotification();
  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<KeyExchangeGroupCheckingVerifier>(
          "X25519MLKEM768"));
  tls_options.set_check_call_host(false);
  tls_options.set_key_exchange_groups({GRPC_TLS_GROUP_X25519_MLKEM768});
  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto client_certificate_provider =
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          root_cert);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");
  DoRpc(server_addr_, tls_options,
        /*expected_key_exchange_group=*/"X25519MLKEM768");
}

TEST_F(TlsCredentialsTest, KeyExchangeGroupX25519) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  const std::vector<grpc_tls_key_exchange_group> key_exchange_groups = {
      GRPC_TLS_GROUP_X25519};
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, &key_exchange_groups); });
  notification.WaitForNotification();
  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<KeyExchangeGroupCheckingVerifier>(
          "X25519"));
  tls_options.set_check_call_host(false);
  tls_options.set_key_exchange_groups({GRPC_TLS_GROUP_X25519});
  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto client_certificate_provider =
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          root_cert);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");
  DoRpc(server_addr_, tls_options, /*expected_key_exchange_group=*/"X25519");
}

TEST_F(TlsCredentialsTest, KeyExchangeGroupSECP256R1) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  const std::vector<grpc_tls_key_exchange_group> key_exchange_groups = {
      GRPC_TLS_GROUP_SECP256R1};
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, &key_exchange_groups); });
  notification.WaitForNotification();
  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<KeyExchangeGroupCheckingVerifier>(
          "prime256v1"));
  tls_options.set_check_call_host(false);
  tls_options.set_key_exchange_groups({GRPC_TLS_GROUP_SECP256R1});
  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto client_certificate_provider =
      std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
          root_cert);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");
  DoRpc(server_addr_, tls_options,
        /*expected_key_exchange_group=*/"prime256v1");
}

TEST_F(TlsCredentialsTest, KeyExchangeGroupMismatchFailsWithTestVerifier) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  const std::vector<grpc_tls_key_exchange_group> key_exchange_groups = {
      GRPC_TLS_GROUP_X25519};
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, &key_exchange_groups); });
  notification.WaitForNotification();
  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<KeyExchangeGroupCheckingVerifier>(
          "prime256v1"));
  tls_options.set_check_call_host(false);
  tls_options.set_key_exchange_groups({GRPC_TLS_GROUP_X25519});
  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  EXPECT_EQ(client_certificate_provider->UpdateRoot(root_cert),
            absl::OkStatus());
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");
  DoRpcAndExpectFailure(
      server_addr_, tls_options, grpc::StatusCode::UNAVAILABLE,
      "Key exchange group mismatch: expected prime256v1, got X25519");
}
#endif  // OPENSSL_IS_BORINGSSL

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
