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

#include "src/core/ext/xds/xds_certificate_provider.h"

#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

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

class TestCertificatesWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  ~TestCertificatesWatcher() override {}

  void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<PemKeyCertPairList> key_cert_pairs) override {
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

  const absl::optional<std::string>& root_certs() const { return root_certs_; }

  const absl::optional<PemKeyCertPairList>& key_cert_pairs() const {
    return key_cert_pairs_;
  }

  grpc_error_handle root_cert_error() const { return root_cert_error_; }

  grpc_error_handle identity_cert_error() const { return identity_cert_error_; }

 private:
  absl::optional<std::string> root_certs_;
  absl::optional<PemKeyCertPairList> key_cert_pairs_;
  grpc_error_handle root_cert_error_;
  grpc_error_handle identity_cert_error_;
};

TEST(
    XdsCertificateProviderTest,
    RootCertDistributorDifferentFromIdentityCertDistributorDifferentCertNames) {
  auto root_cert_distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  auto identity_cert_distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  XdsCertificateProvider provider;
  provider.UpdateRootCertNameAndDistributor("", "root", root_cert_distributor);
  provider.UpdateIdentityCertNameAndDistributor("", "identity",
                                                identity_cert_distributor);
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), absl::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), absl::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root certs and identity certs
  root_cert_distributor->SetKeyMaterials("root", kRootCert1, absl::nullopt);
  identity_cert_distributor->SetKeyMaterials("identity", absl::nullopt,
                                             MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for just root certs
  root_cert_distributor->SetKeyMaterials(
      "root", kRootCert2,
      MakeKeyCertPairsType2() /* does not have an effect */);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for identity certs
  identity_cert_distributor->SetKeyMaterials(
      "identity", kRootCert1 /* does not have an effect */,
      MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for both root and identity
  root_cert_distributor->SetErrorForCert(
      "root", GRPC_ERROR_CREATE(kRootErrorMessage), absl::nullopt);
  identity_cert_distributor->SetErrorForCert(
      "identity", absl::nullopt, GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for root certs. Test that the root cert error is reset.
  root_cert_distributor->SetKeyMaterials("root", kRootCert1, absl::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for identity certs. Test that the identity cert error is
  // reset.
  identity_cert_distributor->SetKeyMaterials("identity", absl::nullopt,
                                             MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
}

TEST(XdsCertificateProviderTest,
     RootCertDistributorDifferentFromIdentityCertDistributorSameCertNames) {
  auto root_cert_distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  auto identity_cert_distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  XdsCertificateProvider provider;
  provider.UpdateRootCertNameAndDistributor("", "test", root_cert_distributor);
  provider.UpdateIdentityCertNameAndDistributor("", "test",
                                                identity_cert_distributor);
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), absl::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), absl::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root certs and identity certs
  root_cert_distributor->SetKeyMaterials("test", kRootCert1, absl::nullopt);
  identity_cert_distributor->SetKeyMaterials("test", absl::nullopt,
                                             MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for just root certs
  root_cert_distributor->SetKeyMaterials("test", kRootCert2, absl::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for identity certs
  identity_cert_distributor->SetKeyMaterials("test", absl::nullopt,
                                             MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for both root and identity
  root_cert_distributor->SetErrorForCert(
      "test", GRPC_ERROR_CREATE(kRootErrorMessage), absl::nullopt);
  identity_cert_distributor->SetErrorForCert(
      "test", absl::nullopt, GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for root certs. Test that the root cert error is reset.
  root_cert_distributor->SetKeyMaterials("test", kRootCert1, absl::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for identity certs. Test that the identity cert error is
  // reset.
  identity_cert_distributor->SetKeyMaterials("test", absl::nullopt,
                                             MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Test update on unwatched cert name
  identity_cert_distributor->SetKeyMaterials("identity", kRootCert2,
                                             MakeKeyCertPairsType2());
  root_cert_distributor->SetKeyMaterials("root", kRootCert1,
                                         MakeKeyCertPairsType1());
}

TEST(XdsCertificateProviderTest,
     RootCertDistributorSameAsIdentityCertDistributorDifferentCertNames) {
  auto distributor = MakeRefCounted<grpc_tls_certificate_distributor>();
  XdsCertificateProvider provider;
  provider.UpdateRootCertNameAndDistributor("", "root", distributor);
  provider.UpdateIdentityCertNameAndDistributor("", "identity", distributor);
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), absl::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), absl::nullopt);
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
  auto distributor = MakeRefCounted<grpc_tls_certificate_distributor>();
  XdsCertificateProvider provider;
  provider.UpdateRootCertNameAndDistributor("", "", distributor);
  provider.UpdateIdentityCertNameAndDistributor("", "", distributor);
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  EXPECT_EQ(watcher->root_certs(), absl::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), absl::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root certs and identity certs
  distributor->SetKeyMaterials("", kRootCert1, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for just root certs
  distributor->SetKeyMaterials("", kRootCert2, absl::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Second update for identity certs
  distributor->SetKeyMaterials("", absl::nullopt, MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for root
  distributor->SetErrorForCert("", GRPC_ERROR_CREATE(kRootErrorMessage),
                               absl::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for identity
  distributor->SetErrorForCert("", absl::nullopt,
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for root
  distributor->SetKeyMaterials("", kRootCert1, absl::nullopt);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update for identity
  distributor->SetKeyMaterials("", absl::nullopt, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
}

TEST(XdsCertificateProviderTest, SwapOutDistributorsMultipleTimes) {
  auto distributor = MakeRefCounted<grpc_tls_certificate_distributor>();
  distributor->SetKeyMaterials("", kRootCert1, MakeKeyCertPairsType1());
  XdsCertificateProvider provider;
  auto* watcher = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher), "", "");
  // Initially there are no certificate providers.
  EXPECT_EQ(watcher->root_certs(), absl::nullopt);
  EXPECT_EQ(watcher->key_cert_pairs(), absl::nullopt);
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(
                  "No certificate provider available for root certificates"));
  EXPECT_THAT(
      StatusToString(watcher->identity_cert_error()),
      ::testing::HasSubstr(
          "No certificate provider available for identity certificates"));
  // Update root cert distributor.
  provider.UpdateRootCertNameAndDistributor("", "", distributor);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), absl::nullopt);
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_THAT(
      StatusToString(watcher->identity_cert_error()),
      ::testing::HasSubstr(
          "No certificate provider available for identity certificates"));
  // Update identity cert distributor
  provider.UpdateIdentityCertNameAndDistributor("", "", distributor);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Update both root and identity certs
  distributor->SetKeyMaterials("", kRootCert2, MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Set error for both root and identity
  distributor->SetErrorForCert("", GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(kRootErrorMessage));
  EXPECT_THAT(StatusToString(watcher->identity_cert_error()),
              ::testing::HasSubstr(kIdentityErrorMessage));
  // Send an update again
  distributor->SetKeyMaterials("", kRootCert1, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Remove root cert provider
  provider.UpdateRootCertNameAndDistributor("", "", nullptr);
  distributor->SetKeyMaterials("", kRootCert2, MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);  // not updated
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(
                  "No certificate provider available for root certificates"));
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Remove identity cert provider too
  provider.UpdateIdentityCertNameAndDistributor("", "", nullptr);
  distributor->SetKeyMaterials("", kRootCert1, MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());  // not updated
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(
                  "No certificate provider available for root certificates"));
  EXPECT_THAT(
      StatusToString(watcher->identity_cert_error()),
      ::testing::HasSubstr(
          "No certificate provider available for identity certificates"));
  // Change certificate names being watched, without any certificate updates.
  provider.UpdateRootCertNameAndDistributor("", "root", distributor);
  provider.UpdateIdentityCertNameAndDistributor("", "identity", distributor);
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_THAT(StatusToString(watcher->root_cert_error()),
              ::testing::HasSubstr(
                  "No certificate provider available for root certificates"));
  EXPECT_THAT(
      StatusToString(watcher->identity_cert_error()),
      ::testing::HasSubstr(
          "No certificate provider available for identity certificates"));
  // Send out certificate updates.
  distributor->SetKeyMaterials("root", kRootCert2, absl::nullopt);
  distributor->SetKeyMaterials("identity", absl::nullopt,
                               MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Swap in new certificate distributors with different certificate names and
  // existing updates.
  auto root_cert_distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  auto identity_cert_distributor =
      MakeRefCounted<grpc_tls_certificate_distributor>();
  provider.UpdateRootCertNameAndDistributor("", "root", root_cert_distributor);
  provider.UpdateIdentityCertNameAndDistributor("", "identity",
                                                identity_cert_distributor);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Change certificate names without any certificate updates.
  provider.UpdateRootCertNameAndDistributor("", "test", root_cert_distributor);
  provider.UpdateIdentityCertNameAndDistributor("", "test",
                                                identity_cert_distributor);
  EXPECT_EQ(watcher->root_certs(), kRootCert2);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
  // Send out certificate updates.
  root_cert_distributor->SetKeyMaterials("test", kRootCert1,
                                         MakeKeyCertPairsType1());
  identity_cert_distributor->SetKeyMaterials("test", kRootCert2,
                                             MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_certs(), kRootCert1);
  EXPECT_EQ(watcher->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher->identity_cert_error(), absl::OkStatus());
}

TEST(XdsCertificateProviderTest, MultipleCertNames) {
  XdsCertificateProvider provider;
  // Start watch for "test1".  There are no underlying distributors for
  // that cert name, so it will return an error.
  auto* watcher1 = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher1), "test1", "test1");
  EXPECT_EQ(watcher1->root_certs(), absl::nullopt);
  EXPECT_EQ(watcher1->key_cert_pairs(), absl::nullopt);
  EXPECT_THAT(StatusToString(watcher1->root_cert_error()),
              ::testing::HasSubstr(
                  "No certificate provider available for root certificates"));
  EXPECT_THAT(
      StatusToString(watcher1->identity_cert_error()),
      ::testing::HasSubstr(
          "No certificate provider available for identity certificates"));
  // Add distributor for "test1".  This will return data to the watcher.
  auto cert_distributor1 = MakeRefCounted<grpc_tls_certificate_distributor>();
  cert_distributor1->SetKeyMaterials("root", kRootCert1, absl::nullopt);
  cert_distributor1->SetKeyMaterials("identity", absl::nullopt,
                                     MakeKeyCertPairsType1());
  provider.UpdateRootCertNameAndDistributor("test1", "root", cert_distributor1);
  provider.UpdateIdentityCertNameAndDistributor("test1", "identity",
                                                cert_distributor1);
  EXPECT_EQ(watcher1->root_certs(), kRootCert1);
  EXPECT_EQ(watcher1->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher1->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher1->identity_cert_error(), absl::OkStatus());
  // Add distributor for "test2".
  auto cert_distributor2 = MakeRefCounted<grpc_tls_certificate_distributor>();
  cert_distributor2->SetKeyMaterials("root2", kRootCert2, absl::nullopt);
  cert_distributor2->SetKeyMaterials("identity2", absl::nullopt,
                                     MakeKeyCertPairsType2());
  provider.UpdateRootCertNameAndDistributor("test2", "root2",
                                            cert_distributor2);
  provider.UpdateIdentityCertNameAndDistributor("test2", "identity2",
                                                cert_distributor2);
  // Add watcher for "test2".  This one should return data immediately.
  auto* watcher2 = new TestCertificatesWatcher;
  provider.distributor()->WatchTlsCertificates(
      std::unique_ptr<TestCertificatesWatcher>(watcher2), "test2", "test2");
  EXPECT_EQ(watcher2->root_certs(), kRootCert2);
  EXPECT_EQ(watcher2->key_cert_pairs(), MakeKeyCertPairsType2());
  EXPECT_EQ(watcher2->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher2->identity_cert_error(), absl::OkStatus());
  // The presence of "test2" should not affect "test1".
  EXPECT_EQ(watcher1->root_certs(), kRootCert1);
  EXPECT_EQ(watcher1->key_cert_pairs(), MakeKeyCertPairsType1());
  EXPECT_EQ(watcher1->root_cert_error(), absl::OkStatus());
  EXPECT_EQ(watcher1->identity_cert_error(), absl::OkStatus());
}

TEST(XdsCertificateProviderTest, UnknownCertName) {
  XdsCertificateProvider provider;
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
