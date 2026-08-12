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
constexpr char kServerEcdsaCertPath[] =
    "src/core/tsi/test_creds/server_ecdsa.pem";
constexpr char kServerEcdsaKeyPath[] =
    "src/core/tsi/test_creds/server_ecdsa.key";
constexpr char kClientCertPath[] = "src/core/tsi/test_creds/client.pem";
constexpr char kClientKeyPath[] = "src/core/tsi/test_creds/client.key";
constexpr char kClientEcdsaCertPath[] =
    "src/core/tsi/test_creds/client_ecdsa.pem";
constexpr char kClientEcdsaKeyPath[] =
    "src/core/tsi/test_creds/client_ecdsa.key";
constexpr char kBadClientCertPath[] = "src/core/tsi/test_creds/badclient.pem";
constexpr char kBadClientKeyPath[] = "src/core/tsi/test_creds/badclient.key";
constexpr char kBadClientEcdsaCertPath[] =
    "src/core/tsi/test_creds/badclient_ecdsa.pem";
constexpr char kBadClientEcdsaKeyPath[] =
    "src/core/tsi/test_creds/badclient_ecdsa.key";
constexpr char kSni1CertPath[] = "src/core/tsi/test_creds/sni1.pem";
constexpr char kSni1KeyPath[] = "src/core/tsi/test_creds/sni1.key";
constexpr char kSni2CertPath[] = "src/core/tsi/test_creds/sni2.pem";
constexpr char kSni2KeyPath[] = "src/core/tsi/test_creds/sni2.key";
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

class TlsCredentialsTest : public ::testing::Test {
 protected:
  void RunServer(absl::Notification* notification,
                 grpc_ssl_client_certificate_request_type cert_request_type =
                     GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
                 const std::vector<grpc_tls_key_exchange_group>*
                     key_exchange_groups = nullptr) {
    std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
    std::string server_key =
        grpc_core::testing::GetFileContents(kServerKeyPath);
    std::string server_cert =
        grpc_core::testing::GetFileContents(kServerCertPath);
    auto certificate_provider =
        std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    ASSERT_TRUE(certificate_provider->UpdateRoot(root_cert).ok());
    ASSERT_TRUE(certificate_provider
                    ->UpdateIdentityKeyCertPair(
                        {grpc::experimental::IdentityKeyOrSignerCertPair{
                            server_key, server_cert}})
                    .ok());
    auto server_options_or =
        grpc::experimental::TlsServerCredentialsOptions::Create(
            certificate_provider);
    ASSERT_TRUE(server_options_or.ok());
    grpc::experimental::TlsServerCredentialsOptions server_options =
        *std::move(server_options_or);
    server_options.set_root_certificate_provider(certificate_provider);
    server_options.set_cert_request_type(cert_request_type);
    server_options.set_send_client_ca_list(true);
    if (key_exchange_groups != nullptr) {
      server_options.set_key_exchange_groups(*key_exchange_groups);
    }
    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        server_addr_, grpc::experimental::TlsServerCredentials(server_options));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    notification->Notify();
    server_->Wait();
  }

  void RunServer(
      absl::Notification* notification,
      const std::vector<grpc_tls_key_exchange_group>* key_exchange_groups) {
    RunServer(notification, GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
              key_exchange_groups);
  }

  void RunServerWithMultipleCerts(
      absl::Notification* notification,
      const std::vector<grpc::experimental::IdentityKeyOrSignerCertPair>&
          identity_pairs,
      grpc_ssl_client_certificate_request_type cert_request_type =
          GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE) {
    std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
    auto certificate_provider =
        std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    ASSERT_TRUE(certificate_provider->UpdateRoot(root_cert).ok());
    ASSERT_TRUE(
        certificate_provider->UpdateIdentityKeyCertPair(identity_pairs).ok());
    auto server_options_or =
        grpc::experimental::TlsServerCredentialsOptions::Create(
            certificate_provider);
    ASSERT_TRUE(server_options_or.ok());
    grpc::experimental::TlsServerCredentialsOptions server_options =
        *std::move(server_options_or);
    server_options.set_root_certificate_provider(certificate_provider);
    server_options.set_cert_request_type(cert_request_type);
    server_options.set_send_client_ca_list(true);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(
        server_addr_, grpc::experimental::TlsServerCredentials(server_options));
    builder.RegisterService(&service_);
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
// NOLINTNEXTLINE(clang-diagnostic-unused-function)
grpc::Status SendRpc(const std::string& server_addr,
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
  if (result.ok()) {
    EXPECT_EQ(response.message(), kMessage);
    if (!expected_key_exchange_group.empty()) {
      std::shared_ptr<const AuthContext> auth_context = context.auth_context();
      EXPECT_NE(auth_context, nullptr);
      if (auth_context != nullptr) {
        std::vector<grpc::string_ref> properties =
            auth_context->FindPropertyValues(
                GRPC_SSL_NEGOTIATED_KEY_EXCHANGE_GROUP_PROPERTY_NAME);
        EXPECT_EQ(properties.size(), 1u);
        if (!properties.empty()) {
          EXPECT_EQ(
              expected_key_exchange_group,
              absl::string_view(properties[0].data(), properties[0].length()));
        }
      }
    }
  }
  return result;
}

// NOLINTNEXTLINE(clang-diagnostic-unused-function)
void DoRpc(const std::string& server_addr,
           const TlsChannelCredentialsOptions& tls_options,
           absl::string_view expected_key_exchange_group = "") {
  grpc::Status result =
      SendRpc(server_addr, tls_options, expected_key_exchange_group);
  EXPECT_TRUE(result.ok()) << "Echo failed: " << result.error_code() << ", "
                           << result.error_message() << ", "
                           << result.error_details();
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
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());
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
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());
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
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());
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
TEST_F(TlsCredentialsTest, ServerMultipleCertsSelectFirstPasses) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string sni1_key = grpc_core::testing::GetFileContents(kSni1KeyPath);
  std::string sni1_cert = grpc_core::testing::GetFileContents(kSni1CertPath);
  std::string sni2_key = grpc_core::testing::GetFileContents(kSni2KeyPath);
  std::string sni2_cert = grpc_core::testing::GetFileContents(kSni2CertPath);

  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServerWithMultipleCerts(
        &notification,
        {grpc::experimental::IdentityKeyOrSignerCertPair{sni1_key, sni1_cert},
         grpc::experimental::IdentityKeyOrSignerCertPair{sni2_key, sni2_cert}});
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo");

  grpc::Status status = SendRpc(server_addr_, tls_options);
  EXPECT_TRUE(status.ok()) << "Expected RPC with SNI foo to succeed: "
                           << status.error_code() << ", "
                           << status.error_message();
}

TEST_F(TlsCredentialsTest, ServerMultipleCertsSelectSecondPasses) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string sni1_key = grpc_core::testing::GetFileContents(kSni1KeyPath);
  std::string sni1_cert = grpc_core::testing::GetFileContents(kSni1CertPath);
  std::string sni2_key = grpc_core::testing::GetFileContents(kSni2KeyPath);
  std::string sni2_cert = grpc_core::testing::GetFileContents(kSni2CertPath);

  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServerWithMultipleCerts(
        &notification,
        {grpc::experimental::IdentityKeyOrSignerCertPair{sni1_key, sni1_cert},
         grpc::experimental::IdentityKeyOrSignerCertPair{sni2_key, sni2_cert}});
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("bar");

  grpc::Status status = SendRpc(server_addr_, tls_options);
  EXPECT_TRUE(status.ok()) << "Expected RPC with SNI bar to succeed: "
                           << status.error_code() << ", "
                           << status.error_message();
}

TEST_F(TlsCredentialsTest, ServerMultipleCertsUnmatchedFails) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string sni1_key = grpc_core::testing::GetFileContents(kSni1KeyPath);
  std::string sni1_cert = grpc_core::testing::GetFileContents(kSni1CertPath);
  std::string sni2_key = grpc_core::testing::GetFileContents(kSni2KeyPath);
  std::string sni2_cert = grpc_core::testing::GetFileContents(kSni2CertPath);

  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServerWithMultipleCerts(
        &notification,
        {grpc::experimental::IdentityKeyOrSignerCertPair{sni1_key, sni1_cert},
         grpc::experimental::IdentityKeyOrSignerCertPair{sni2_key, sni2_cert}});
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_check_call_host(true);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("unmatched.domain.com");

  grpc::Status status = SendRpc(server_addr_, tls_options);
  EXPECT_FALSE(status.ok()) << "Expected RPC with unmatched SNI to fail";
}

TEST_F(TlsCredentialsTest, ClientSingleValidCertPasses) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification,
              GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  std::string valid_client_key =
      grpc_core::testing::GetFileContents(kClientKeyPath);
  std::string valid_client_cert =
      grpc_core::testing::GetFileContents(kClientCertPath);

  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(
                      {grpc::experimental::IdentityKeyOrSignerCertPair{
                          valid_client_key, valid_client_cert}})
                  .ok());

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_identity_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");

  grpc::Status status = SendRpc(server_addr_, tls_options);
  EXPECT_TRUE(status.ok()) << "Expected RPC to succeed with valid client cert: "
                           << status.error_code() << ", "
                           << status.error_message();
}

TEST_F(TlsCredentialsTest, ClientSingleBadCertFails) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification,
              GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  std::string bad_client_key =
      grpc_core::testing::GetFileContents(kBadClientKeyPath);
  std::string bad_client_cert =
      grpc_core::testing::GetFileContents(kBadClientCertPath);

  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(
                      {grpc::experimental::IdentityKeyOrSignerCertPair{
                          bad_client_key, bad_client_cert}})
                  .ok());

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_identity_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");

  grpc::Status status = SendRpc(server_addr_, tls_options);
  EXPECT_FALSE(status.ok()) << "Expected RPC to fail with bad client cert";
}

TEST_F(TlsCredentialsTest,
       ClientMultipleCerts_ValidEcdsaFirst_BadRsaSecond_Passes) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification,
              GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  std::string bad_rsa_key =
      grpc_core::testing::GetFileContents(kBadClientKeyPath);
  std::string bad_rsa_cert =
      grpc_core::testing::GetFileContents(kBadClientCertPath);
  std::string valid_ecdsa_key =
      grpc_core::testing::GetFileContents(kClientEcdsaKeyPath);
  std::string valid_ecdsa_cert =
      grpc_core::testing::GetFileContents(kClientEcdsaCertPath);

  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(
                      {grpc::experimental::IdentityKeyOrSignerCertPair{
                           valid_ecdsa_key, valid_ecdsa_cert},
                       grpc::experimental::IdentityKeyOrSignerCertPair{
                           bad_rsa_key, bad_rsa_cert}})
                  .ok());

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_identity_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");

  grpc::Status status = SendRpc(server_addr_, tls_options);
  EXPECT_TRUE(status.ok())
      << "Expected RPC to succeed with valid ECDSA client cert: "
      << status.error_code() << ", " << status.error_message();
}

TEST_F(TlsCredentialsTest, ClientMultipleCertsAllInvalidFails) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  server_thread_ = new std::thread([&]() {
    RunServer(&notification,
              GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);
  std::string bad_rsa_key =
      grpc_core::testing::GetFileContents(kBadClientKeyPath);
  std::string bad_rsa_cert =
      grpc_core::testing::GetFileContents(kBadClientCertPath);
  std::string bad_ecdsa_key =
      grpc_core::testing::GetFileContents(kBadClientEcdsaKeyPath);
  std::string bad_ecdsa_cert =
      grpc_core::testing::GetFileContents(kBadClientEcdsaCertPath);

  auto client_certificate_provider =
      std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
  ASSERT_TRUE(client_certificate_provider->UpdateRoot(root_cert).ok());
  ASSERT_TRUE(client_certificate_provider
                  ->UpdateIdentityKeyCertPair(
                      {grpc::experimental::IdentityKeyOrSignerCertPair{
                           bad_rsa_key, bad_rsa_cert},
                       grpc::experimental::IdentityKeyOrSignerCertPair{
                           bad_ecdsa_key, bad_ecdsa_cert}})
                  .ok());

  TlsChannelCredentialsOptions tls_options;
  tls_options.set_certificate_verifier(
      ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
  tls_options.set_check_call_host(false);
  tls_options.set_root_certificate_provider(client_certificate_provider);
  tls_options.set_identity_certificate_provider(client_certificate_provider);
  tls_options.set_sni_override("foo.test.google.fr");

  grpc::Status status = SendRpc(server_addr_, tls_options);
  EXPECT_FALSE(status.ok()) << "Expected RPC to fail with all invalid certs";
}

TEST_F(TlsCredentialsTest,
       ServerMultipleCerts_RsaAndEcdsa_ConnectWithRsaClientAndEcdsaClient) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  absl::Notification notification;
  std::string server_rsa_key =
      grpc_core::testing::GetFileContents(kServerKeyPath);
  std::string server_rsa_cert =
      grpc_core::testing::GetFileContents(kServerCertPath);
  std::string server_ecdsa_key =
      grpc_core::testing::GetFileContents(kServerEcdsaKeyPath);
  std::string server_ecdsa_cert =
      grpc_core::testing::GetFileContents(kServerEcdsaCertPath);

  server_thread_ = new std::thread([&]() {
    RunServerWithMultipleCerts(
        &notification,
        {grpc::experimental::IdentityKeyOrSignerCertPair{server_rsa_key,
                                                         server_rsa_cert},
         grpc::experimental::IdentityKeyOrSignerCertPair{server_ecdsa_key,
                                                         server_ecdsa_cert}},
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  });
  notification.WaitForNotification();

  std::string root_cert = grpc_core::testing::GetFileContents(kCaCertPath);

  // 1. Connect with client containing ONLY an RSA cert.
  {
    std::string client_rsa_key =
        grpc_core::testing::GetFileContents(kClientKeyPath);
    std::string client_rsa_cert =
        grpc_core::testing::GetFileContents(kClientCertPath);
    auto rsa_client_cert_provider =
        std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    ASSERT_TRUE(rsa_client_cert_provider->UpdateRoot(root_cert).ok());
    ASSERT_TRUE(rsa_client_cert_provider
                    ->UpdateIdentityKeyCertPair(
                        {grpc::experimental::IdentityKeyOrSignerCertPair{
                            client_rsa_key, client_rsa_cert}})
                    .ok());

    TlsChannelCredentialsOptions tls_options;
    tls_options.set_certificate_verifier(
        ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
    tls_options.set_check_call_host(false);
    tls_options.set_root_certificate_provider(rsa_client_cert_provider);
    tls_options.set_identity_certificate_provider(rsa_client_cert_provider);
    tls_options.set_sni_override("foo.test.google.fr");

    grpc::Status status = SendRpc(server_addr_, tls_options);
    EXPECT_TRUE(status.ok())
        << "Expected RPC to succeed with RSA client cert: "
        << status.error_code() << ", " << status.error_message();
  }

  // 2. Connect with client containing ONLY an ECDSA cert.
  {
    std::string client_ecdsa_key =
        grpc_core::testing::GetFileContents(kClientEcdsaKeyPath);
    std::string client_ecdsa_cert =
        grpc_core::testing::GetFileContents(kClientEcdsaCertPath);
    auto ecdsa_client_cert_provider =
        std::make_shared<grpc::experimental::InMemoryCertificateProvider>();
    ASSERT_TRUE(ecdsa_client_cert_provider->UpdateRoot(root_cert).ok());
    ASSERT_TRUE(ecdsa_client_cert_provider
                    ->UpdateIdentityKeyCertPair(
                        {grpc::experimental::IdentityKeyOrSignerCertPair{
                            client_ecdsa_key, client_ecdsa_cert}})
                    .ok());

    TlsChannelCredentialsOptions tls_options;
    tls_options.set_certificate_verifier(
        ExternalCertificateVerifier::Create<NoOpCertificateVerifier>());
    tls_options.set_check_call_host(false);
    tls_options.set_root_certificate_provider(ecdsa_client_cert_provider);
    tls_options.set_identity_certificate_provider(ecdsa_client_cert_provider);
    tls_options.set_sni_override("foo.test.google.fr");

    grpc::Status status = SendRpc(server_addr_, tls_options);
    EXPECT_TRUE(status.ok())
        << "Expected RPC to succeed with ECDSA client cert: "
        << status.error_code() << ", " << status.error_message();
  }
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
