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

#include <memory>
#include <string>

#include "src/core/tsi/ssl_transport_security.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

extern "C" {
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
}

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

class BoringSslPrivateKeySigner
    : public PrivateKeySigner,
      public std::enable_shared_from_this<BoringSslPrivateKeySigner> {
 public:
  explicit BoringSslPrivateKeySigner(absl::string_view private_key)
      : pkey_(LoadPrivateKeyFromString(private_key)) {}

  bool Sign(absl::string_view data_to_sign,
            SignatureAlgorithm signature_algorithm,
            OnSignComplete on_sign_complete) override {
    on_sign_complete(SignWithBoringSSL(data_to_sign, signature_algorithm,
                                             pkey_.get()));
    return true;
  }

 private:
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
    if (!EVP_DigestSign(ctx.get(), private_key_result.data(), &len, in,
                        in_len)) {
      return absl::InternalError("EVP_DigestSign failed");
    }
    private_key_result.resize(len);
    std::string private_key_result_str(private_key_result.begin(),
                                       private_key_result.end());
    return std::string(private_key_result.begin(), private_key_result.end());
  }

  bssl::UniquePtr<EVP_PKEY> pkey_;
};

class BadSignatureSigner : public PrivateKeySigner {
 public:
  bool Sign(absl::string_view /*data_to_sign*/,
            SignatureAlgorithm /*signature_algorithm*/,
            OnSignComplete on_sign_complete) override {
    on_sign_complete("bad signature");
    return true;
  }
};

class ErrorSigner : public PrivateKeySigner {
 public:
  bool Sign(absl::string_view /*data_to_sign*/,
            SignatureAlgorithm /*signature_algorithm*/,
            OnSignComplete on_sign_complete) override {
    on_sign_complete(absl::InternalError("signer error"));
    return true;
  }
};

enum class OffloadParty {
  kClient,
  kServer,
  kNone,
};

class PrivateKeyOffloadTest : public ::testing::TestWithParam<tsi_tls_version> {
 protected:
  class SslOffloadTsiTestFixture {
   public:
    explicit SslOffloadTsiTestFixture(OffloadParty offload_party,
                                      std::shared_ptr<PrivateKeySigner> signer)
        : offload_party_(offload_party), signer_(std::move(signer)) {
      tsi_test_fixture_init(&base_);
      base_.test_unused_bytes = true;
      base_.vtable = &kVtable;
      ca_cert_ =
          GetFileContents(absl::StrCat(kTestCredsRelativePath, "ca.pem"));
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
          signer_ = std::make_shared<BoringSslPrivateKeySigner>(client_key_);
        } else if (offload_party_ == OffloadParty::kServer) {
          signer_ = std::make_shared<BoringSslPrivateKeySigner>(server_key_);
        }
      }
      server_pem_key_cert_pairs_.emplace_back(server_key_, server_cert_);
      client_pem_key_cert_pairs_.emplace_back(client_key_, client_cert_);
      server_pem_key_cert_pairs_with_signer_.emplace_back(signer_,
                                                          server_cert_);
      client_pem_key_cert_pairs_with_signer_.emplace_back(signer_,
                                                          client_cert_);
    }

    void Run(bool expect_success) {
      expect_success_ = expect_success;
      tsi_test_do_handshake(&base_);
      tsi_test_fixture_destroy(&base_);
    }

    ~SslOffloadTsiTestFixture() {
      tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
      tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
    }

   private:
    static void SetupHandshakers(tsi_test_fixture* fixture) {
      auto* self = reinterpret_cast<SslOffloadTsiTestFixture*>(fixture);
      self->SetupHandshakers();
    }

    void SetupHandshakers() {
      // Create client handshaker factory.
      tsi_ssl_client_handshaker_options client_options;
      client_options.root_cert_info = std::make_shared<RootCertInfo>(ca_cert_);
      client_options.min_tls_version = GetParam();
      client_options.max_tls_version = GetParam();
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
      server_options.root_cert_info = std::make_shared<RootCertInfo>(ca_cert_);
      server_options.client_certificate_request =
          TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
      server_options.min_tls_version = GetParam();
      server_options.max_tls_version = GetParam();
      if (offload_party_ == OffloadParty::kServer) {
        server_options.pem_key_cert_pairs =
            server_pem_key_cert_pairs_with_signer_;
      } else {
        server_options.pem_key_cert_pairs = server_pem_key_cert_pairs_;
      }
      ASSERT_EQ(tsi_create_ssl_server_handshaker_factory_with_options(
                    &server_options, &server_handshaker_factory_),
                TSI_OK);

      // Create handshakers.
      ASSERT_EQ(tsi_ssl_client_handshaker_factory_create_handshaker(
                    client_handshaker_factory_, nullptr, 0, 0, std::nullopt,
                    &base_.client_handshaker),
                TSI_OK);
      ASSERT_EQ(tsi_ssl_server_handshaker_factory_create_handshaker(
                    server_handshaker_factory_, 0, 0, &base_.server_handshaker),
                TSI_OK);
    }

    static void CheckHandshakerPeers(tsi_test_fixture* fixture) {
      auto* self = reinterpret_cast<SslOffloadTsiTestFixture*>(fixture);
      self->CheckHandshakerPeers();
    }

    void CheckHandshakerPeers() {
      if (expect_success_) {
        tsi_peer peer;
        EXPECT_EQ(
            tsi_handshaker_result_extract_peer(base_.client_result, &peer),
            TSI_OK);
        tsi_peer_destruct(&peer);
        EXPECT_EQ(
            tsi_handshaker_result_extract_peer(base_.server_result, &peer),
            TSI_OK);
        tsi_peer_destruct(&peer);
      } else {
#if OPENSSL_VERSION_NUMBER >= 0x10100000
        // When negotiating TLS 1.3, the client-side handshake succeeds
        //  because server verification of the client certificate occurs after
        //  the client-side handshake is complete.
        bool expect_client_success = GetParam() == tsi_tls_version::TSI_TLS1_2
                                    ? expect_client_success_1_2_
                                    : expect_client_success_1_3_;
#else
        //  If using OpenSSL version < 1.1, the CRL revocation won't
        //  be enabled anyways, so we always expect the connection to
        //  be successful.
        expect_server_success = true;
        expect_client_success = expect_server_success;
#endif
        EXPECT_EQ(base_.client_result, nullptr);
        EXPECT_EQ(base_.server_result, nullptr);
      }
    }

    static void Destruct(tsi_test_fixture* fixture) {
      delete reinterpret_cast<SslOffloadTsiTestFixture*>(fixture);
    }

    static struct tsi_test_fixture_vtable kVtable;

    tsi_test_fixture base_;
    tsi_ssl_server_handshaker_factory* server_handshaker_factory_ = nullptr;
    tsi_ssl_client_handshaker_factory* client_handshaker_factory_ = nullptr;
    std::string ca_cert_;
    std::string server_key_;
    std::string server_cert_;
    std::string client_key_;
    std::string client_cert_;
    std::vector<tsi_ssl_pem_key_cert_pair> server_pem_key_cert_pairs_;
    std::vector<tsi_ssl_pem_key_cert_pair> client_pem_key_cert_pairs_;
    std::vector<tsi_ssl_pem_key_cert_pair>
        server_pem_key_cert_pairs_with_signer_;
    std::vector<tsi_ssl_pem_key_cert_pair>
        client_pem_key_cert_pairs_with_signer_;
    OffloadParty offload_party_;
    std::shared_ptr<PrivateKeySigner> signer_;
    bool expect_success_ = false;
  };
};

struct tsi_test_fixture_vtable
    PrivateKeyOffloadTest::SslOffloadTsiTestFixture::kVtable = {
        &PrivateKeyOffloadTest::SslOffloadTsiTestFixture::SetupHandshakers,
        &PrivateKeyOffloadTest::SslOffloadTsiTestFixture::CheckHandshakerPeers,
        &PrivateKeyOffloadTest::SslOffloadTsiTestFixture::Destruct};

TEST_P(PrivateKeyOffloadTest, OffloadOnServerSucceeds) {
  auto* fixture = new SslOffloadTsiTestFixture(OffloadParty::kServer, nullptr);
  fixture->Run(/*expect_success=*/true);
}

TEST_P(PrivateKeyOffloadTest, OffloadOnClientSucceeds) {
  auto* fixture = new SslOffloadTsiTestFixture(OffloadParty::kClient, nullptr);
  fixture->Run(/*expect_success=*/true);
}

TEST_P(PrivateKeyOffloadTest, OffloadFailsWithBadSignatureOnServer) {
  auto* fixture = new SslOffloadTsiTestFixture(
      OffloadParty::kServer, std::make_shared<BadSignatureSigner>());
  fixture->Run(/*expect_success=*/false);
}

TEST_P(PrivateKeyOffloadTest, OffloadFailsWithBadSignatureOnClient) {
  auto* fixture = new SslOffloadTsiTestFixture(
      OffloadParty::kClient, std::make_shared<BadSignatureSigner>());
  fixture->Run(/*expect_success=*/false);
}

TEST_P(PrivateKeyOffloadTest, OffloadFailsWithSignerErrorOnServer) {
  auto* fixture = new SslOffloadTsiTestFixture(OffloadParty::kServer,
                                               std::make_shared<ErrorSigner>());
  fixture->Run(/*expect_success=*/false);
}

TEST_P(PrivateKeyOffloadTest, OffloadFailsWithSignerErrorOnClient) {
  auto* fixture = new SslOffloadTsiTestFixture(OffloadParty::kClient,
                                               std::make_shared<ErrorSigner>());
  fixture->Run(/*expect_success=*/false);
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