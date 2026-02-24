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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/private_key_signer.h>
#include <openssl/digest.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

#include <memory>
#include <string>

#include "src/core/tsi/ssl_transport_security.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"

namespace grpc_core {
namespace testing {

namespace {
constexpr absl::string_view kTestCredsRelativePath = "src/core/tsi/test_creds/";

bssl::UniquePtr<EVP_PKEY> LoadPrivateKeyFromString(
    absl::string_view private_pem) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(private_pem.data(), private_pem.size()));
  return bssl::UniquePtr<EVP_PKEY>(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

uint16_t GetBoringSslAlgorithm(
    PrivateKeySigner::SignatureAlgorithm signature_algorithm) {
  switch (signature_algorithm) {
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha256:
      return SSL_SIGN_RSA_PKCS1_SHA256;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha384:
      return SSL_SIGN_RSA_PKCS1_SHA384;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha512:
      return SSL_SIGN_RSA_PKCS1_SHA512;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp256r1Sha256:
      return SSL_SIGN_ECDSA_SECP256R1_SHA256;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp384r1Sha384:
      return SSL_SIGN_ECDSA_SECP384R1_SHA384;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp521r1Sha512:
      return SSL_SIGN_ECDSA_SECP521R1_SHA512;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha256:
      return SSL_SIGN_RSA_PSS_RSAE_SHA256;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha384:
      return SSL_SIGN_RSA_PSS_RSAE_SHA384;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha512:
      return SSL_SIGN_RSA_PSS_RSAE_SHA512;
  }
  return -1;
}

absl::StatusOr<std::string> SignWithBoringSSL(
    absl::string_view data_to_sign,
    PrivateKeySigner::SignatureAlgorithm signature_algorithm,
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
  return std::string(private_key_result.begin(), private_key_result.end());
}

class SyncTestPrivateKeySigner final : public PrivateKeySigner {
 public:
  enum class Mode { kSuccess, kError, kInvalidSignature };

  explicit SyncTestPrivateKeySigner(absl::string_view private_key,
                                    Mode mode = Mode::kSuccess)
      : pkey_(LoadPrivateKeyFromString(private_key)), mode_(mode) {}

  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete /*on_sign_complete*/) override {
    if (mode_ == Mode::kError) {
      return absl::InternalError("signer error");
    }
    if (mode_ == Mode::kInvalidSignature) {
      return "bad signature";
    }
    return SignWithBoringSSL(data_to_sign, signature_algorithm, pkey_.get());
  }

  void Cancel(std::shared_ptr<AsyncSigningHandle> /*handle*/) override {}

 private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
  Mode mode_;
};

class AsyncTestPrivateKeySigner final
    : public PrivateKeySigner,
      public std::enable_shared_from_this<AsyncTestPrivateKeySigner> {
 public:
  enum class Mode { kSuccess, kError, kCancellation };

  explicit AsyncTestPrivateKeySigner(absl::string_view private_key,
                                     Mode mode = Mode::kSuccess)
      : pkey_(LoadPrivateKeyFromString(private_key)), mode_(mode) {}

  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete on_sign_complete) override {
    if (mode_ == Mode::kCancellation) {
      return std::make_shared<AsyncSigningHandle>();
    }
    grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
        [self = shared_from_this(), data_to_sign = std::string(data_to_sign),
         signature_algorithm,
         on_sign_complete = std::move(on_sign_complete)]() mutable {
          if (self->mode_ == Mode::kError) {
            on_sign_complete(absl::InternalError("async signer error"));
          } else {
            on_sign_complete(SignWithBoringSSL(
                data_to_sign, signature_algorithm, self->pkey_.get()));
          }
        });
    return std::make_shared<AsyncSigningHandle>();
  }

  void Cancel(std::shared_ptr<AsyncSigningHandle> /*handle*/) override {
    if (!was_cancelled_.exchange(true)) {
      notification_.Notify();
    }
  }

  bool WasCancelled() { return was_cancelled_.load(); }

 private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
  Mode mode_;
  absl::Notification notification_;
  std::atomic<bool> was_cancelled_{false};
};

enum class OffloadParty {
  kClient,
  kServer,
  kNone,
};

class SslOffloadTsiTestFixture {
 public:
  SslOffloadTsiTestFixture(OffloadParty offload_party,
                           std::shared_ptr<PrivateKeySigner> signer,
                           tsi_tls_version tls_version)
      : offload_party_(offload_party),
        signer_(std::move(signer)),
        tls_version_(tls_version) {
    tsi_test_fixture_init(&base_);
    base_.test_unused_bytes = true;
    base_.vtable = &kVtable;
    ca_cert_ = GetFileContents(absl::StrCat(kTestCredsRelativePath, "ca.pem"));
    server_key_ =
        GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.key"));
    server_cert_ =
        GetFileContents(absl::StrCat(kTestCredsRelativePath, "server1.pem"));
    client_key_ =
        GetFileContents(absl::StrCat(kTestCredsRelativePath, "client.key"));
    client_cert_ =
        GetFileContents(absl::StrCat(kTestCredsRelativePath, "client.pem"));
    if (signer_ == nullptr) {
      if (offload_party_ == OffloadParty::kClient) {
        signer_ = std::make_shared<SyncTestPrivateKeySigner>(client_key_);
      } else if (offload_party_ == OffloadParty::kServer) {
        signer_ = std::make_shared<SyncTestPrivateKeySigner>(server_key_);
      }
    }
    server_pem_key_cert_pairs_.emplace_back(server_key_, server_cert_);
    client_pem_key_cert_pairs_.emplace_back(client_key_, client_cert_);
    server_pem_key_cert_pairs_with_signer_.emplace_back(signer_, server_cert_);
    client_pem_key_cert_pairs_with_signer_.emplace_back(signer_, client_cert_);
  }

  void Run(bool expect_success, bool expect_success_on_client) {
    expect_success_ = expect_success;
    expect_success_on_client_ = expect_success_on_client;
    tsi_test_do_handshake(&base_);
    absl::SleepFor(absl::Seconds(5));
    tsi_test_fixture_destroy(&base_);
  }

  void Shutdown() {
    MutexLock lock(&mu_);
    if (base_.client_handshaker != nullptr) {
      tsi_handshaker_shutdown(base_.client_handshaker);
    }
    if (base_.server_handshaker != nullptr) {
      tsi_handshaker_shutdown(base_.server_handshaker);
    }
  }

  ~SslOffloadTsiTestFixture() {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }

  tsi_test_fixture base_;  // MUST BE FIRST

 private:
  static void SetupHandshakers(tsi_test_fixture* fixture) {
    auto* self = reinterpret_cast<SslOffloadTsiTestFixture*>(fixture);
    self->SetupHandshakersImpl();
  }

  void SetupHandshakersImpl() {
    // Create client handshaker factory.
    tsi_ssl_client_handshaker_options client_options;
    client_options.root_cert_info =
        std::make_shared<tsi::RootCertInfo>(ca_cert_);
    client_options.min_tls_version = tls_version_;
    client_options.max_tls_version = tls_version_;
    if (offload_party_ == OffloadParty::kClient) {
      client_options.pem_key_cert_pair =
          &client_pem_key_cert_pairs_with_signer_[0];
    } else {
      client_options.pem_key_cert_pair = &client_pem_key_cert_pairs_[0];
    }
    ASSERT_EQ(tsi_create_ssl_client_handshaker_factory_with_options(
                  &client_options, &client_handshaker_factory_),
              TSI_OK);

    // Create server handshaker factory.
    tsi_ssl_server_handshaker_options server_options;
    server_options.root_cert_info =
        std::make_shared<tsi::RootCertInfo>(ca_cert_);
    server_options.client_certificate_request =
        TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    server_options.min_tls_version = tls_version_;
    server_options.max_tls_version = tls_version_;
    if (offload_party_ == OffloadParty::kServer) {
      server_options.pem_key_cert_pairs = server_pem_key_cert_pairs_with_signer_;
    } else {
      server_options.pem_key_cert_pairs = server_pem_key_cert_pairs_;
    }
    ASSERT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                  &server_options, &server_handshaker_factory_),
              TSI_OK);

    // Create handshakers.
    tsi_handshaker* client_hs;
    tsi_handshaker* server_hs;
    ASSERT_EQ(tsi_ssl_client_handshaker_factory_create_handshaker(
                  client_handshaker_factory_, nullptr, 0, 0, std::nullopt,
                  &client_hs),
              TSI_OK);
    ASSERT_EQ(tsi_ssl_server_handshaker_factory_create_handshaker(
                  server_handshaker_factory_, 0, 0, &server_hs),
              TSI_OK);
    {
      MutexLock lock(&mu_);
      base_.client_handshaker = client_hs;
      base_.server_handshaker = server_hs;
    }
  }

  static void CheckHandshakerPeers(tsi_test_fixture* fixture) {
    auto* self = reinterpret_cast<SslOffloadTsiTestFixture*>(fixture);
    self->CheckHandshakerPeersImpl();
  }

  void CheckHandshakerPeersImpl() {
    if (expect_success_) {
      tsi_peer peer;
      EXPECT_EQ(tsi_handshaker_result_extract_peer(base_.client_result, &peer),
                TSI_OK);
      tsi_peer_destruct(&peer);
      EXPECT_EQ(tsi_handshaker_result_extract_peer(base_.server_result, &peer),
                TSI_OK);
      tsi_peer_destruct(&peer);
    } else {
      EXPECT_EQ(base_.client_result != nullptr, expect_success_on_client_);
      EXPECT_EQ(base_.server_result, nullptr);
    }
  }

  static void Destruct(tsi_test_fixture* /*fixture*/) {
    // We don't delete here because we are managed by std::shared_ptr.
  }

  static struct tsi_test_fixture_vtable kVtable;

  tsi_ssl_server_handshaker_factory* server_handshaker_factory_ = nullptr;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_ = nullptr;
  std::string ca_cert_;
  std::string server_key_;
  std::string server_cert_;
  std::string client_key_;
  std::string client_cert_;
  std::vector<tsi_ssl_pem_key_cert_pair> server_pem_key_cert_pairs_;
  std::vector<tsi_ssl_pem_key_cert_pair> client_pem_key_cert_pairs_;
  std::vector<tsi_ssl_pem_key_cert_pair> server_pem_key_cert_pairs_with_signer_;
  std::vector<tsi_ssl_pem_key_cert_pair> client_pem_key_cert_pairs_with_signer_;
  OffloadParty offload_party_;
  std::shared_ptr<PrivateKeySigner> signer_;
  tsi_tls_version tls_version_;
  bool expect_success_ = false;
  bool expect_success_on_client_ = false;
  Mutex mu_;
};

struct tsi_test_fixture_vtable SslOffloadTsiTestFixture::kVtable = {
    &SslOffloadTsiTestFixture::SetupHandshakers,
    &SslOffloadTsiTestFixture::CheckHandshakerPeers,
    &SslOffloadTsiTestFixture::Destruct};

class PrivateKeyOffloadTest : public ::testing::TestWithParam<tsi_tls_version> {
 protected:
  void SetUp() override {
    event_engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
  }

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
};

// Verifies that server-side signing offload succeeds.
TEST_P(PrivateKeyOffloadTest, OffloadOnServerSucceeds) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kServer, nullptr, GetParam());
  fixture->Run(/*expect_success=*/true, /*expect_success_on_client*/ true);
}

// Verifies that client-side signing offload succeeds.
TEST_P(PrivateKeyOffloadTest, OffloadOnClientSucceeds) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kClient, nullptr, GetParam());
  fixture->Run(/*expect_success=*/true, /*expect_success_on_client*/ true);
}

// Verifies that providing a completely malformed signature string on the server
// fails the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithBadSignatureOnServer) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kServer,
      std::make_shared<SyncTestPrivateKeySigner>(
          "", SyncTestPrivateKeySigner::Mode::kInvalidSignature),
      GetParam());
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
}

// Verifies that providing a completely malformed signature string on the client
// fails the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithBadSignatureOnClient) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kClient,
      std::make_shared<SyncTestPrivateKeySigner>(
          "", SyncTestPrivateKeySigner::Mode::kInvalidSignature),
      GetParam());
  fixture->Run(
      /*expect_success=*/false,
      /*expect_success_on_client*/ GetParam() == tsi_tls_version::TSI_TLS1_3);
}

// Verifies that an error returned by a synchronous signer on the server fails
// the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithSignerErrorOnServer) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kServer,
      std::make_shared<SyncTestPrivateKeySigner>(
          "", SyncTestPrivateKeySigner::Mode::kError),
      GetParam());
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
}

// Verifies that an error returned by a synchronous signer on the client fails
// the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithSignerErrorOnClient) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kClient,
      std::make_shared<SyncTestPrivateKeySigner>(
          "", SyncTestPrivateKeySigner::Mode::kError),
      GetParam());
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
}

// Verifies that an error returned by an asynchronous signer on the server fails
// the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithAsyncSignerErrorOnServer) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kServer,
      std::make_shared<AsyncTestPrivateKeySigner>(
          "", AsyncTestPrivateKeySigner::Mode::kError),
      GetParam());
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
}

// Verifies that an error returned by an asynchronous signer on the client fails
// the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithAsyncSignerErrorOnClient) {
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kClient,
      std::make_shared<AsyncTestPrivateKeySigner>(
          "", AsyncTestPrivateKeySigner::Mode::kError),
      GetParam());
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
}

// Verifies that providing a signature from the wrong key synchronously on the
// server fails the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithInvalidSignatureOnServer) {
  std::string server_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server0.key"));
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kServer,
      std::make_shared<SyncTestPrivateKeySigner>(server_key), GetParam());
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
}

// Verifies that providing a signature from the wrong key synchronously on the
// client fails the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithInvalidSignatureOnClient) {
  std::string client_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "client1.key"));
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kClient,
      std::make_shared<SyncTestPrivateKeySigner>(client_key), GetParam());
  fixture->Run(
      /*expect_success=*/false,
      /*expect_success_on_client*/ GetParam() == tsi_tls_version::TSI_TLS1_3);
}

// Verifies that providing a signature from the wrong key asynchronously on the
// server fails the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithAsyncInvalidSignatureOnServer) {
  std::string server_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "server0.key"));
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kServer,
      std::make_shared<AsyncTestPrivateKeySigner>(server_key), GetParam());
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
}

// Verifies that providing a signature from the wrong key asynchronously on the
// client fails the handshake.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithAsyncInvalidSignatureOnClient) {
  std::string client_key =
      GetFileContents(absl::StrCat(kTestCredsRelativePath, "client1.key"));
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kClient,
      std::make_shared<AsyncTestPrivateKeySigner>(client_key), GetParam());
  fixture->Run(
      /*expect_success=*/false,
      /*expect_success_on_client*/ GetParam() == tsi_tls_version::TSI_TLS1_3);
}

// Verifies that server-side async signing is correctly cancelled when the
// handshaker is shut down.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithSignCancelledOnServer) {
  auto signer = std::make_shared<AsyncTestPrivateKeySigner>(
      "", AsyncTestPrivateKeySigner::Mode::kCancellation);
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kServer,
      std::static_pointer_cast<PrivateKeySigner>(signer), GetParam());
  event_engine_->RunAfter(std::chrono::seconds(1), [fixture]() {
    fixture->Shutdown();
  });
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
  EXPECT_TRUE(signer->WasCancelled());
}

// Verifies that client-side async signing is correctly cancelled when the
// handshaker is shut down.
TEST_P(PrivateKeyOffloadTest, OffloadFailsWithSignCancelledOnClient) {
  auto signer = std::make_shared<AsyncTestPrivateKeySigner>(
      "", AsyncTestPrivateKeySigner::Mode::kCancellation);
  auto fixture = std::make_shared<SslOffloadTsiTestFixture>(
      OffloadParty::kClient,
      std::static_pointer_cast<PrivateKeySigner>(signer), GetParam());
  event_engine_->RunAfter(std::chrono::seconds(1), [fixture]() {
    fixture->Shutdown();
  });
  fixture->Run(/*expect_success=*/false, /*expect_success_on_client*/ false);
  EXPECT_TRUE(signer->WasCancelled());
}

std::string TestNameSuffix(
    const ::testing::TestParamInfo<tsi_tls_version>& version) {
  if (version.param == tsi_tls_version::TSI_TLS1_2) return "TLS_1_2";
  return "TLS_1_3";
}

INSTANTIATE_TEST_SUITE_P(PrivateKeyOffloadTest, PrivateKeyOffloadTest,
                         ::testing::Values(tsi_tls_version::TSI_TLS1_2,
                                           tsi_tls_version::TSI_TLS1_3),
                         TestNameSuffix);

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
