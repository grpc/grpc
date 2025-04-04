//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/xds/grpc/xds_certificate_provider.h"

#include <grpc/grpc.h>

#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

namespace grpc_core {
namespace testing {
namespace {

constexpr const char* kRootCert1 = "root_cert_1_contents";
constexpr const char* kRootCert2 = "root_cert_2_contents";
constexpr const char* kIdentityCert1PrivateKey = "identity_private_key_1";
constexpr const char* kIdentityCert1 = "identity_cert_1_contents";
constexpr const char* kIdentityCert2PrivateKey = "identity_private_key_2";
constexpr const char* kIdentityCert2 = "identity_cert_2_contents";
constexpr const char* kRootErrorMessage = "root_error_message";
constexpr const char* kIdentityErrorMessage = "identity_error_message";

PemKeyCertPairList MakeKeyCertPairsType1() {
  return MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1);
}

PemKeyCertPairList MakeKeyCertPairsType2() {
  return MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2);
}

class TestCertProvider : public grpc_tls_certificate_provider {
 public:
  TestCertProvider()
      : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()) {}

  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("Xds");
    return kFactory.Create();
  }

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

 private:
  int CompareImpl(const grpc_tls_certificate_provider* other) const override {
    return QsortCompare(this, static_cast<const TestCertProvider*>(other));
  }

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
};

class TestCertificatesWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  ~TestCertificatesWatcher() override {}

  void OnCertificatesChanged(
      std::optional<absl::string_view> root_certs,
      std::optional<PemKeyCertPairList> key_cert_pairs) override {
    if (root_certs.has_value()) {
      if (!root_certs_.has_value() ||
          (root_certs_.has_value() &&
           std::string(root_certs.value()) != root_certs_.value())) {
        root_cert_error_ = absl::OkStatus();
      }
      root_certs_.emplace(std::string(root_certs.value()));
    }
    if (key_cert_pairs.has_value()) {
      if (key_cert_pairs != key_cert_pairs_) {
        identity_cert_error_ = absl::OkStatus();
        key_cert_pairs_ = key_cert_pairs;
      }
    }
  }

  void OnError(grpc_error_handle root_cert_error,
               grpc_error_handle identity_cert_error) override {
    root_cert_error_ = root_cert_error;
    identity_cert_error_ = identity_cert_error;
  }

  const std::optional<std::string>& root_certs() const { return root_certs_; }

  const std::optional<PemKeyCertPairList>& key_cert_pairs() const {
    return key_cert_pairs_;
  }

  grpc_error_handle root_cert_error() const { return root_cert_error_; }

  grpc_error_handle identity_cert_error() const { return identity_cert_error_; }

 private:
  std::optional<std::string> root_certs_;
  std::optional<PemKeyCertPairList> key_cert_pairs_;
  grpc_error_handle root_cert_error_;
  grpc_error_handle identity_cert_error_;
};

TEST(
    XdsCertificateProviderTest,
    RootCertDistributorDifferentFromIdentityCertDistributorDifferentCertNames) {
  auto root_provider = MakeRefCounted<TestCertProvider>();
  auto identity_provider = MakeRefCounted<TestCertProvider>();
  XdsCertificateProvider provider(root_provider, "root", identity_provider,
                                  "identity", {});
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), std::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), std::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root certs and identity certs
  root_provider->distributor()->SetKeyMaterials("root", kRootCert1,
                                                std::nullopt);
  identity_provider->distributor()->SetKeyMaterials("identity", std::nullopt,
                                                    MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for just root certs
  root_provider->distributor()->SetKeyMaterials(
      "root", kRootCert2,
      MakeKeyCertPairsType2() /* does not have an effect */);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for identity certs
  identity_provider->distributor()->SetKeyMaterials(
      "identity", kRootCert1 /* does not have an effect */,
      MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for both root and identity
  root_provider->distributor()->SetErrorForCert(
      "root", GRPC_ERROR_CREATE(kRootErrorMessage), std::nullopt);
  identity_provider->distributor()->SetErrorForCert(
      "identity", std::nullopt, GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for root certs. Test that the root cert error is reset.
  root_provider->distributor()->SetKeyMaterials("root", kRootCert1,
                                                std::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for identity certs. Test that the identity cert error is
  // reset.
  identity_provider->distributor()->SetKeyMaterials("identity", std::nullopt,
                                                    MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
}

TEST(XdsCertificateProviderTest,
     RootCertDistributorDifferentFromIdentityCertDistributorSameCertNames) {
  auto root_provider = MakeRefCounted<TestCertProvider>();
  auto identity_provider = MakeRefCounted<TestCertProvider>();
  XdsCertificateProvider provider(root_provider, "test", identity_provider,
                                  "test", {});
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), std::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), std::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root certs and identity certs
  root_provider->distributor()->SetKeyMaterials("test", kRootCert1,
                                                std::nullopt);
  identity_provider->distributor()->SetKeyMaterials("test", std::nullopt,
                                                    MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for just root certs
  root_provider->distributor()->SetKeyMaterials("test", kRootCert2,
                                                std::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for identity certs
  identity_provider->distributor()->SetKeyMaterials("test", std::nullopt,
                                                    MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for both root and identity
  root_provider->distributor()->SetErrorForCert(
      "test", GRPC_ERROR_CREATE(kRootErrorMessage), std::nullopt);
  identity_provider->distributor()->SetErrorForCert(
      "test", std::nullopt, GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for root certs. Test that the root cert error is reset.
  root_provider->distributor()->SetKeyMaterials("test", kRootCert1,
                                                std::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for identity certs. Test that the identity cert error is
  // reset.
  identity_provider->distributor()->SetKeyMaterials("test", std::nullopt,
                                                    MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Test update on unwatched cert name
  identity_provider->distributor()->SetKeyMaterials("identity", kRootCert2,
                                                    MakeKeyCertPairsType2());
  root_provider->distributor()->SetKeyMaterials("root", kRootCert1,
                                                MakeKeyCertPairsType1());
}

TEST(XdsCertificateProviderTest,
     RootCertDistributorSameAsIdentityCertDistributorDifferentCertNames) {
  auto root_and_identity_provider = MakeRefCounted<TestCertProvider>();
  auto distributor = root_and_identity_provider->distributor();
  XdsCertificateProvider provider(root_and_identity_provider, "root",
                                  root_and_identity_provider, "identity", {});
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), std::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), std::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root certs and identity certs
  distributor->SetKeyMaterials("root", kRootCert1, MakeKeyCertPairsType2());
  distributor->SetKeyMaterials("identity", kRootCert2, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for just root certs
  distributor->SetKeyMaterials("root", kRootCert2, MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for identity certs
  distributor->SetKeyMaterials("identity", kRootCert1, MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for root
  distributor->SetErrorForCert("root", GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kRootErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  distributor->SetErrorForCert("identity",
                               GRPC_ERROR_CREATE(kIdentityErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for root
  distributor->SetKeyMaterials("root", kRootCert1, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for identity
  distributor->SetKeyMaterials("identity", kRootCert2, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
}

TEST(XdsCertificateProviderTest,
     RootCertDistributorSameAsIdentityCertDistributorSameCertNames) {
  auto root_and_identity_provider = MakeRefCounted<TestCertProvider>();
  auto distributor = root_and_identity_provider->distributor();
  XdsCertificateProvider provider(root_and_identity_provider, "",
                                  root_and_identity_provider, "", {});
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), std::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), std::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root certs and identity certs
  distributor->SetKeyMaterials("", kRootCert1, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for just root certs
  distributor->SetKeyMaterials("", kRootCert2, std::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for identity certs
  distributor->SetKeyMaterials("", std::nullopt, MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for root
  distributor->SetErrorForCert("", GRPC_ERROR_CREATE(kRootErrorMessage),
                               std::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for identity
  distributor->SetErrorForCert("", std::nullopt,
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for root
  distributor->SetKeyMaterials("", kRootCert1, std::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for identity
  distributor->SetKeyMaterials("", std::nullopt, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
}

TEST(XdsCertificateProviderTest, UnknownCertName) {
  XdsCertificateProvider provider(nullptr, "", nullptr, "", {});
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "test", "test");
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(
                  "No certificate provider available for root certificates"));
  EXPECT_THAT(
      StatusToString(watcher->identity_cert_error()),
      ::testing::HasSubstr(
          "No certificate provider available for identity certificates"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
