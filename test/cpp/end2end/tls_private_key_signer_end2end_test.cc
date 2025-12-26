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

#include <grpc/event_engine/event_engine.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_certificate_provider.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/security/tls_private_key_signer.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>
#include <openssl/digest.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {
namespace {

constexpr absl::string_view kMessage = "Hello";
constexpr absl::string_view kCaPemPath = "src/core/tsi/test_creds/ca.pem";
constexpr absl::string_view kServerKeyPath =
    "src/core/tsi/test_creds/server1.key";
constexpr absl::string_view kServerCertPath =
    "src/core/tsi/test_creds/server1.pem";
constexpr absl::string_view kClientKeyPath =
    "src/core/tsi/test_creds/client.key";
constexpr absl::string_view kClientCertPath =
    "src/core/tsi/test_creds/client.pem";

bssl::UniquePtr<EVP_PKEY> LoadPrivateKeyFromString(
    absl::string_view private_pem) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(private_pem.data(), private_pem.size()));
  return bssl::UniquePtr<EVP_PKEY>(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

class TlsPrivateKeyOffloadTest : public ::testing::Test {
 protected:
  void RunServer(absl::Notification* notification,
                 std::shared_ptr<experimental::CertificateProviderInterface>
                     server_certificate_provider) {
    grpc::experimental::TlsServerCredentialsOptions options(
        std::move(server_certificate_provider));
    options.watch_root_certs();
    options.set_root_cert_name("root");
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("identity");
    options.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    auto server_credentials = grpc::experimental::TlsServerCredentials(options);
    CHECK_NE(server_credentials.get(), nullptr);

    grpc::ServerBuilder builder;

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
  std::shared_ptr<grpc_core::PrivateKeySigner> signer_;
};

void DoRpc(const std::string& server_addr,
           const experimental::TlsChannelCredentialsOptions& tls_options) {
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
  context.set_deadline(grpc_timeout_seconds_to_deadline(/*time_s=*/40));
  grpc::Status result = stub->Echo(&context, request, &response);
  EXPECT_TRUE(result.ok()) << result.error_message().c_str() << ", "
                           << result.error_details().c_str();
  EXPECT_EQ(response.message(), kMessage);
}

uint16_t GetBoringSslAlgorithm(
    grpc_core::PrivateKeySigner::SignatureAlgorithm signature_algorithm) {
  switch (signature_algorithm) {
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha256:
      return SSL_SIGN_RSA_PKCS1_SHA256;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha384:
      return SSL_SIGN_RSA_PKCS1_SHA384;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha512:
      return SSL_SIGN_RSA_PKCS1_SHA512;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp256r1Sha256:
      return SSL_SIGN_ECDSA_SECP256R1_SHA256;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp384r1Sha384:
      return SSL_SIGN_ECDSA_SECP384R1_SHA384;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp521r1Sha512:
      return SSL_SIGN_ECDSA_SECP521R1_SHA512;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha256:
      return SSL_SIGN_RSA_PSS_RSAE_SHA256;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha384:
      return SSL_SIGN_RSA_PSS_RSAE_SHA384;
    case grpc_core::PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha512:
      return SSL_SIGN_RSA_PSS_RSAE_SHA512;
  }
  return -1;
}

absl::StatusOr<std::string> SignWithBoringSSL(
    absl::string_view data_to_sign,
    grpc_core::PrivateKeySigner::SignatureAlgorithm signature_algorithm,
    EVP_PKEY* private_key) {
  const uint8_t* in = reinterpret_cast<const uint8_t*>(data_to_sign.data());
  const size_t in_len = data_to_sign.size();

  uint16_t boring_signature_algorithm =
      GetBoringSslAlgorithm(signature_algorithm);
  if (EVP_PKEY_id(private_key) !=
      SSL_get_signature_algorithm_key_type(boring_signature_algorithm)) {
    fprintf(stderr, "Key type does not match signature algorithm.\n");
  }

  // Determine the hash.
  const EVP_MD* md =
      SSL_get_signature_algorithm_digest(boring_signature_algorithm);
  bssl::ScopedEVP_MD_CTX ctx;
  EVP_PKEY_CTX* pctx;
  if (!EVP_DigestSignInit(ctx.get(), &pctx, md, nullptr, private_key)) {
    return absl::InternalError("EVP_DigestSignInit failed");
  }

  // Configure additional signature parameters.
  if (SSL_is_signature_algorithm_rsa_pss(boring_signature_algorithm)) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
        !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1)) {
      return absl::InternalError("EVP_PKEY_CTX failed");
    }
  }

  size_t len = 0;
  if (!EVP_DigestSign(ctx.get(), nullptr, &len, in, in_len)) {
    return absl::InternalError("EVP_DigestSign failed");
  }
  std::vector<uint8_t> private_key_result;
  private_key_result.resize(len);
  if (!EVP_DigestSign(ctx.get(), private_key_result.data(), &len, in, in_len)) {
    return absl::InternalError("EVP_DigestSign failed");
  }
  private_key_result.resize(len);
  std::string private_key_result_str(private_key_result.begin(),
                                     private_key_result.end());
  return std::string(private_key_result.begin(), private_key_result.end());
}

class TestPrivateKeySignerAsync final
    : public grpc::experimental::PrivateKeySigner,
      public std::enable_shared_from_this<TestPrivateKeySignerAsync> {
 public:
  explicit TestPrivateKeySignerAsync(absl::string_view private_key)
      : pkey_(LoadPrivateKeyFromString(private_key)) {}

  bool Sign(absl::string_view data_to_sign,
            SignatureAlgorithm signature_algorithm,
            OnSignComplete on_sign_complete) override {
    auto event_engine =
        grpc_event_engine::experimental::GetDefaultEventEngine();
    event_engine->Run(
        [self = shared_from_this(), data_to_sign = std::string(data_to_sign),
         signature_algorithm,
         on_sign_complete = std::move(on_sign_complete)]() mutable {
          on_sign_complete(SignWithBoringSSL(data_to_sign, signature_algorithm,
                                             self->pkey_.get()));
        });
    return false;
  }

 private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

class TestPrivateKeySignerSync final
    : public grpc::experimental::PrivateKeySigner {
 public:
  explicit TestPrivateKeySignerSync(absl::string_view private_key)
      : pkey_(LoadPrivateKeyFromString(private_key)) {}

  bool Sign(absl::string_view data_to_sign,
            SignatureAlgorithm signature_algorithm,
            OnSignComplete on_sign_complete) override {
    on_sign_complete(
        SignWithBoringSSL(data_to_sign, signature_algorithm, pkey_.get()));
    return true;
  }

 private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

TEST_F(TlsPrivateKeyOffloadTest, DefaultNoOffload) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  experimental::IdentityKeyCertPair server_key_cert_pair;
  server_key_cert_pair.private_key = server_key;
  server_key_cert_pair.certificate_chain = server_cert;
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(server_key_cert_pair);
  auto server_certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          ca_cert, server_identity_key_cert_pairs);

  absl::Notification notification;
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_certificate_provider); });
  notification.WaitForNotification();

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  experimental::IdentityKeyCertPair client_key_cert_pair;
  client_key_cert_pair.private_key = client_key;
  client_key_cert_pair.certificate_chain = client_cert;
  std::vector<experimental::IdentityKeyCertPair> client_identity_key_cert_pairs;
  client_identity_key_cert_pairs.emplace_back(client_key_cert_pair);
  auto client_certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          ca_cert, client_identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerAsync) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  experimental::IdentityKeyCertPair server_key_cert_pair;
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  signer_ = std::make_shared<TestPrivateKeySignerAsync>(server_key);
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyCertPair{signer_, server_cert});
  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  server_certificate_provider->UpdateIdentity(server_identity_key_cert_pairs);
  server_certificate_provider->UpdateRoot(ca_cert);

  absl::Notification notification;
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_certificate_provider); });
  notification.WaitForNotification();

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = client_key;
  key_cert_pair.certificate_chain = client_cert;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto client_certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          ca_cert, identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerClientAsync) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  experimental::IdentityKeyCertPair server_key_cert_pair;
  server_key_cert_pair.private_key = server_key;
  server_key_cert_pair.certificate_chain = server_cert;
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(server_key_cert_pair);
  auto server_certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          ca_cert, server_identity_key_cert_pairs);

  absl::Notification notification;
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_certificate_provider); });
  notification.WaitForNotification();

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  signer_ = std::make_shared<TestPrivateKeySignerAsync>(client_key);
  std::vector<grpc::experimental::IdentityKeyCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyCertPair{signer_, client_cert});
  client_certificate_provider->UpdateIdentity(identity_pairs);
  client_certificate_provider->UpdateRoot(ca_cert);

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerSync) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  experimental::IdentityKeyCertPair server_key_cert_pair;
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  signer_ = std::make_shared<TestPrivateKeySignerSync>(server_key);
  server_identity_key_cert_pairs.emplace_back(
      grpc::experimental::IdentityKeyCertPair{signer_, server_cert});
  auto server_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  server_certificate_provider->UpdateIdentity(server_identity_key_cert_pairs);
  server_certificate_provider->UpdateRoot(ca_cert);

  absl::Notification notification;
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_certificate_provider); });
  notification.WaitForNotification();

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = client_key;
  key_cert_pair.certificate_chain = client_cert;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto client_certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          ca_cert, identity_key_cert_pairs);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
}

TEST_F(TlsPrivateKeyOffloadTest, OffloadWithCustomKeySignerClientSync) {
  server_addr_ = absl::StrCat("localhost:",
                              std::to_string(grpc_pick_unused_port_or_die()));
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string ca_cert =
      grpc_core::testing::GetFileContents(std::string(kCaPemPath));

  experimental::IdentityKeyCertPair server_key_cert_pair;
  server_key_cert_pair.private_key = server_key;
  server_key_cert_pair.certificate_chain = server_cert;
  std::vector<experimental::IdentityKeyCertPair> server_identity_key_cert_pairs;
  server_identity_key_cert_pairs.emplace_back(server_key_cert_pair);
  auto server_certificate_provider =
      std::make_shared<experimental::StaticDataCertificateProvider>(
          ca_cert, server_identity_key_cert_pairs);

  absl::Notification notification;
  server_thread_ = new std::thread(
      [&]() { RunServer(&notification, server_certificate_provider); });
  notification.WaitForNotification();

  std::string client_key =
      grpc_core::testing::GetFileContents(std::string(kClientKeyPath));
  std::string client_cert =
      grpc_core::testing::GetFileContents(std::string(kClientCertPath));
  auto client_certificate_provider =
      std::make_shared<experimental::InMemoryCertificateProvider>();
  signer_ = std::make_shared<TestPrivateKeySignerSync>(client_key);
  std::vector<grpc::experimental::IdentityKeyCertPair> identity_pairs;
  identity_pairs.emplace_back(
      grpc::experimental::IdentityKeyCertPair{signer_, client_cert});
  client_certificate_provider->UpdateIdentity(identity_pairs);
  client_certificate_provider->UpdateRoot(ca_cert);

  grpc::experimental::TlsChannelCredentialsOptions options;
  options.set_certificate_provider(client_certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name("root");
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name("identity");
  options.set_check_call_host(false);

  DoRpc(server_addr_, options);
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