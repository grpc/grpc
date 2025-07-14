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

#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <deque>
#include <list>

#include "absl/base/no_destructor.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/crash.h"
#include "src/core/util/match.h"
#include "src/core/util/tmpfile.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define CA_CERT_PATH_2 "src/core/tsi/test_creds/multi-domain.pem"
#define SERVER_CERT_PATH_2 "src/core/tsi/test_creds/server0.pem"
#define SERVER_KEY_PATH_2 "src/core/tsi/test_creds/server0.key"
#define INVALID_PATH "invalid/path"
#define MALFORMED_CERT_PATH "src/core/tsi/test_creds/malformed-cert.pem"
#define MALFORMED_KEY_PATH "src/core/tsi/test_creds/malformed-key.pem"

namespace grpc_core {

namespace testing {

namespace {
const char* kGoodSpiffeBundleMapPath =
    "test/core/credentials/transport/tls/test_data/spiffe/"
    "client_spiffebundle.json";

const char* kGoodSpiffeBundleMapPath2 =
    "test/core/credentials/transport/tls/test_data/spiffe/"
    "test_bundles/spiffebundle2.json";

const char* kMalformedSpiffeBundleMapPath =
    "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
    "spiffebundle_malformed.json";

SpiffeBundleMap GetGoodSpiffeBundleMap() {
  static const absl::NoDestructor<SpiffeBundleMap> kSpiffeBundleMap([] {
    auto spiffe_bundle_map =
        SpiffeBundleMap::FromFile(kGoodSpiffeBundleMapPath);
    EXPECT_TRUE(spiffe_bundle_map.ok());
    return *spiffe_bundle_map;
  }());
  return *kSpiffeBundleMap;
}

SpiffeBundleMap GetGoodSpiffeBundleMap2() {
  static const absl::NoDestructor<SpiffeBundleMap> kSpiffeBundleMap2([] {
    auto spiffe_bundle_map =
        SpiffeBundleMap::FromFile(kGoodSpiffeBundleMapPath2);
    EXPECT_TRUE(spiffe_bundle_map.ok());
    return *spiffe_bundle_map;
  }());
  return *kSpiffeBundleMap2;
}

MATCHER_P2(MatchesCredentialInfo, root_matcher, identity_matcher, "") {
  bool ok = true;
  ok &= ::testing::ExplainMatchResult(root_matcher, arg.root_cert_info,
                                      result_listener);
  ok &= ::testing::ExplainMatchResult(identity_matcher, arg.key_cert_pairs,
                                      result_listener);
  return ok;
}

MATCHER_P(EqRootCert, cert, "") {
  return ::testing::ExplainMatchResult(
      ::testing::Pointee(::testing::VariantWith<std::string>(cert)), arg,
      result_listener);
}

MATCHER_P(EqSpiffeBundleMap, map, "") {
  return ::testing::ExplainMatchResult(
      ::testing::Pointee(::testing::VariantWith<SpiffeBundleMap>(map)), arg,
      result_listener);
}

}  // namespace

constexpr const char* kCertName = "cert_name";
constexpr const char* kRootError = "Unable to get latest root certificates.";
constexpr const char* kIdentityError =
    "Unable to get latest identity certificates.";

class GrpcTlsCertificateProviderTest : public ::testing::Test {
 protected:
  // Forward declaration.
  class TlsCertificatesTestWatcher;

  // CredentialInfo contains the parameters when calling OnCertificatesChanged
  // of a watcher. When OnCertificatesChanged is invoked, we will push a
  // CredentialInfo to the cert_update_queue of state_, and check in each test
  // if the status updates are correct.
  struct CredentialInfo {
    PemKeyCertPairList key_cert_pairs;
    std::shared_ptr<RootCertInfo> root_cert_info;
    CredentialInfo(const RootCertInfo& roots, PemKeyCertPairList key_cert)
        : key_cert_pairs(std::move(key_cert)),
          root_cert_info(std::make_shared<RootCertInfo>(roots)) {}
    bool operator==(const CredentialInfo& other) const {
      return key_cert_pairs == other.key_cert_pairs &&
             root_cert_info == other.root_cert_info;
    }
  };

  // ErrorInfo contains the parameters when calling OnError of a watcher. When
  // OnError is invoked, we will push a ErrorInfo to the error_queue of state_,
  // and check in each test if the status updates are correct.
  struct ErrorInfo {
    std::string root_cert_str;
    std::string identity_cert_str;
    ErrorInfo(std::string root, std::string identity)
        : root_cert_str(std::move(root)),
          identity_cert_str(std::move(identity)) {}
    bool operator==(const ErrorInfo& other) const {
      return root_cert_str == other.root_cert_str &&
             identity_cert_str == other.identity_cert_str;
    }
  };

  struct WatcherState {
    TlsCertificatesTestWatcher* watcher = nullptr;
    std::deque<CredentialInfo> cert_update_queue;
    std::deque<ErrorInfo> error_queue;
    Mutex mu;

    std::deque<CredentialInfo> GetCredentialQueue() {
      // We move the data member value so the data member will be re-initiated
      // with size 0, and ready for the next check.
      MutexLock lock(&mu);
      return std::move(cert_update_queue);
    }
    std::deque<ErrorInfo> GetErrorQueue() {
      // We move the data member value so the data member will be re-initiated
      // with size 0, and ready for the next check.
      MutexLock lock(&mu);
      return std::move(error_queue);
    }
  };

  class TlsCertificatesTestWatcher : public grpc_tls_certificate_distributor::
                                         TlsCertificatesWatcherInterface {
   public:
    // ctor sets state->watcher to this.
    explicit TlsCertificatesTestWatcher(WatcherState* state) : state_(state) {
      state_->watcher = this;
    }

    // dtor sets state->watcher to nullptr.
    ~TlsCertificatesTestWatcher() override { state_->watcher = nullptr; }

    void OnCertificatesChanged(
        std::shared_ptr<RootCertInfo> roots,
        std::optional<PemKeyCertPairList> key_cert_pairs) override {
      MutexLock lock(&state_->mu);
      RootCertInfo updated_root;
      if (roots != nullptr) {
        updated_root = *roots;
      }
      PemKeyCertPairList updated_identity;
      if (key_cert_pairs.has_value()) {
        updated_identity = std::move(*key_cert_pairs);
      }
      state_->cert_update_queue.emplace_back(updated_root,
                                             std::move(updated_identity));
    }

    void OnError(grpc_error_handle root_cert_error,
                 grpc_error_handle identity_cert_error) override {
      MutexLock lock(&state_->mu);
      CHECK(!root_cert_error.ok() || !identity_cert_error.ok());
      std::string root_error_str;
      if (!root_cert_error.ok()) {
        root_error_str = std::string(root_cert_error.message());
      }
      std::string identity_error_str;
      if (!identity_cert_error.ok()) {
        identity_error_str = std::string(identity_cert_error.message());
      }
      state_->error_queue.emplace_back(std::move(root_error_str),
                                       std::move(identity_error_str));
    }

   private:
    WatcherState* state_;
  };

  void SetUp() override {
    root_cert_ = GetFileContents(CA_CERT_PATH);
    cert_chain_ = GetFileContents(SERVER_CERT_PATH);
    private_key_ = GetFileContents(SERVER_KEY_PATH);
    root_cert_2_ = GetFileContents(CA_CERT_PATH_2);
    cert_chain_2_ = GetFileContents(SERVER_CERT_PATH_2);
    private_key_2_ = GetFileContents(SERVER_KEY_PATH_2);
    malformed_cert_ = GetFileContents(MALFORMED_CERT_PATH);
    malformed_key_ = GetFileContents(MALFORMED_KEY_PATH);
    spiffe_bundle_contents_ = GetFileContents(kGoodSpiffeBundleMapPath);
    spiffe_bundle_contents_2_ = GetFileContents(kGoodSpiffeBundleMapPath2);
    malformed_spiffe_bundle_contents_ =
        GetFileContents(kMalformedSpiffeBundleMapPath);
  }

  WatcherState* MakeWatcher(
      RefCountedPtr<grpc_tls_certificate_distributor> distributor,
      std::optional<std::string> root_cert_name,
      std::optional<std::string> identity_cert_name) {
    MutexLock lock(&mu_);
    distributor_ = distributor;
    watchers_.emplace_back();
    // TlsCertificatesTestWatcher ctor takes a pointer to the WatcherState.
    // It sets WatcherState::watcher to point to itself.
    // The TlsCertificatesTestWatcher dtor will set WatcherState::watcher back
    // to nullptr to indicate that it's been destroyed.
    auto watcher =
        std::make_unique<TlsCertificatesTestWatcher>(&watchers_.back());
    distributor_->WatchTlsCertificates(std::move(watcher),
                                       std::move(root_cert_name),
                                       std::move(identity_cert_name));
    return &watchers_.back();
  }

  void CancelWatch(WatcherState* state) {
    MutexLock lock(&mu_);
    distributor_->CancelTlsCertificatesWatch(state->watcher);
    EXPECT_EQ(state->watcher, nullptr);
  }

  std::string root_cert_;
  std::string private_key_;
  std::string cert_chain_;
  std::string root_cert_2_;
  std::string private_key_2_;
  std::string cert_chain_2_;
  std::string malformed_cert_;
  std::string malformed_key_;
  std::string spiffe_bundle_contents_;
  std::string spiffe_bundle_contents_2_;
  std::string malformed_spiffe_bundle_contents_;
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  // Use a std::list<> here to avoid the address invalidation caused by internal
  // reallocation of std::vector<>.
  std::list<WatcherState> watchers_;
  // This is to make watchers_ thread-safe.
  Mutex mu_;
};

TEST_F(GrpcTlsCertificateProviderTest, StaticDataCertificateProviderCreation) {
  StaticDataCertificateProvider provider(
      root_cert_, MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, std::nullopt);
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(MatchesCredentialInfo(
                  EqRootCert(root_cert_), PemKeyCertPairList())));
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), std::nullopt, kCertName);
  EXPECT_THAT(watcher_state_3->GetCredentialQueue(),
              ::testing::ElementsAre(MatchesCredentialInfo(
                  EqRootCert(""), MakeCertKeyPairs(private_key_.c_str(),
                                                   cert_chain_.c_str()))));
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateProviderTest,
       StaticDataCertificateProviderWithGoodPathsAndCredentialValidation) {
  StaticDataCertificateProvider provider(
      root_cert_, MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  EXPECT_EQ(provider.ValidateCredentials(), absl::OkStatus());
}

TEST_F(GrpcTlsCertificateProviderTest,
       StaticDataCertificateProviderWithMalformedRootCertificate) {
  StaticDataCertificateProvider provider(
      malformed_cert_,
      MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::FailedPreconditionError(
                "Failed to parse root certificates as PEM: Invalid PEM."));
}

TEST_F(GrpcTlsCertificateProviderTest,
       StaticDataCertificateProviderWithMalformedIdentityCertificate) {
  StaticDataCertificateProvider provider(
      root_cert_,
      MakeCertKeyPairs(private_key_.c_str(), malformed_cert_.c_str()));
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::FailedPreconditionError(
                "Failed to parse certificate chain as PEM: Invalid PEM."));
}

TEST_F(GrpcTlsCertificateProviderTest,
       StaticDataCertificateProviderWithMalformedIdentityKey) {
  StaticDataCertificateProvider provider(
      root_cert_,
      MakeCertKeyPairs(malformed_key_.c_str(), cert_chain_.c_str()));
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::NotFoundError(
                "Failed to parse private key as PEM: No private key found."));
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodPaths) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          CA_CERT_PATH,
                                          /*spiffe_bundle_map_path=*/"", 1);
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, std::nullopt);
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(MatchesCredentialInfo(
                  EqRootCert(root_cert_), PemKeyCertPairList())));
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), std::nullopt, kCertName);
  EXPECT_THAT(watcher_state_3->GetCredentialQueue(),
              ::testing::ElementsAre(MatchesCredentialInfo(
                  EqRootCert(""), MakeCertKeyPairs(private_key_.c_str(),
                                                   cert_chain_.c_str()))));
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodPathsAndCredentialValidation) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          CA_CERT_PATH,
                                          /*spiffe_bundle_map_path=*/"", 1);
  EXPECT_EQ(provider.ValidateCredentials(), absl::OkStatus());
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithMalformedRootCertificate) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH_2, SERVER_CERT_PATH_2,
                                          MALFORMED_CERT_PATH,
                                          /*spiffe_bundle_map_path=*/"", 1);
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::FailedPreconditionError(
                "Failed to parse root certificates as PEM: Invalid PEM."));
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithMalformedIdentityCertificate) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH_2,
                                          MALFORMED_CERT_PATH, CA_CERT_PATH_2,
                                          /*spiffe_bundle_map_path=*/"", 1);
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::FailedPreconditionError(
                "Failed to parse certificate chain as PEM: Invalid PEM."));
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithMalformedIdentityKey) {
  FileWatcherCertificateProvider provider(MALFORMED_KEY_PATH,
                                          SERVER_CERT_PATH_2, CA_CERT_PATH_2,
                                          /*spiffe_bundle_map_path=*/"", 1);
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::NotFoundError(
                "Failed to parse private key as PEM: No private key found."));
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithBadPaths) {
  FileWatcherCertificateProvider provider(INVALID_PATH, INVALID_PATH,
                                          INVALID_PATH,
                                          /*spiffe_bundle_map_path=*/"", 1);
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootError, kIdentityError)));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, std::nullopt);
  EXPECT_THAT(watcher_state_2->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootError, "")));
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), std::nullopt, kCertName);
  EXPECT_THAT(watcher_state_3->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo("", kIdentityError)));
  EXPECT_THAT(watcher_state_3->GetCredentialQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_3);
}

// The following tests write credential data to temporary files to test the
// transition behavior of the provider.
TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderOnBothCertsRefreshed) {
  // Create temporary files and copy cert data into them.
  TmpFile tmp_root_cert(root_cert_);
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key.name(), tmp_identity_cert.name(), tmp_root_cert.name(),
      /*spiffe_bundle_map_path=*/"", 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // Expect to see the credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Copy new data to files.
  // TODO(ZhenLian): right now it is not completely atomic. Use the real atomic
  // update when the directory renaming is added in gpr.
  tmp_root_cert.RewriteFile(root_cert_2_);
  tmp_identity_key.RewriteFile(private_key_2_);
  tmp_identity_cert.RewriteFile(cert_chain_2_);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see the new credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_2_),
          MakeCertKeyPairs(private_key_2_.c_str(), cert_chain_2_.c_str()))));
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderOnRootCertsRefreshed) {
  // Create temporary files and copy cert data into them.
  TmpFile tmp_root_cert(root_cert_);
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key.name(), tmp_identity_cert.name(), tmp_root_cert.name(),
      /*spiffe_bundle_map_path=*/"", 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // Expect to see the credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Copy new data to files.
  // TODO(ZhenLian): right now it is not completely atomic. Use the real atomic
  // update when the directory renaming is added in gpr.
  tmp_root_cert.RewriteFile(root_cert_2_);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see the new credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_2_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderOnIdentityCertsRefreshed) {
  // Create temporary files and copy cert data into them.
  TmpFile tmp_root_cert(root_cert_);
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key.name(), tmp_identity_cert.name(), tmp_root_cert.name(),
      /*spiffe_bundle_map_path=*/"", 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // Expect to see the credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Copy new data to files.
  // TODO(ZhenLian): right now it is not completely atomic. Use the real atomic
  // update when the directory renaming is added in gpr.
  tmp_identity_key.RewriteFile(private_key_2_);
  tmp_identity_cert.RewriteFile(cert_chain_2_);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see the new credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_2_.c_str(), cert_chain_2_.c_str()))));
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodAtFirstThenDeletedBothCerts) {
  // Create temporary files and copy cert data into it.
  auto tmp_root_cert = std::make_unique<TmpFile>(root_cert_);
  auto tmp_identity_key = std::make_unique<TmpFile>(private_key_);
  auto tmp_identity_cert = std::make_unique<TmpFile>(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key->name(), tmp_identity_cert->name(),
      tmp_root_cert->name(), /*spiffe_bundle_map_path=*/"", 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Delete TmpFile objects, which will remove the corresponding files.
  tmp_root_cert.reset();
  tmp_identity_key.reset();
  tmp_identity_cert.reset();
  // Wait 2 seconds for the provider's refresh thread to read the deleted files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see errors sent to watchers, and no credential updates.
  // We have no ideas on how many errors we will receive, so we only check once.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::Contains(ErrorInfo(kRootError, kIdentityError)));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodAtFirstThenDeletedRootCerts) {
  // Create temporary files and copy cert data into it.
  auto tmp_root_cert = std::make_unique<TmpFile>(root_cert_);
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key.name(), tmp_identity_cert.name(), tmp_root_cert->name(),
      /*spiffe_bundle_map_path=*/"", 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Delete root TmpFile object, which will remove the corresponding file.
  tmp_root_cert.reset();
  // Wait 2 seconds for the provider's refresh thread to read the deleted files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see errors sent to watchers, and no credential updates.
  // We have no ideas on how many errors we will receive, so we only check once.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::Contains(ErrorInfo(kRootError, "")));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodAtFirstThenDeletedIdentityCerts) {
  // Create temporary files and copy cert data into it.
  TmpFile tmp_root_cert(root_cert_);
  auto tmp_identity_key = std::make_unique<TmpFile>(private_key_);
  auto tmp_identity_cert = std::make_unique<TmpFile>(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key->name(), tmp_identity_cert->name(), tmp_root_cert.name(),
      /*spiffe_bundle_map_path=*/"", 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqRootCert(root_cert_),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Delete identity TmpFile objects, which will remove the corresponding files.
  tmp_identity_key.reset();
  tmp_identity_cert.reset();
  // Wait 2 seconds for the provider's refresh thread to read the deleted files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see errors sent to watchers, and no credential updates.
  // We have no ideas on how many errors we will receive, so we only check once.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::Contains(ErrorInfo("", kIdentityError)));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderTooShortRefreshIntervalIsOverwritten) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          CA_CERT_PATH,
                                          /*spiffe_bundle_map_path=*/"", 0);
  ASSERT_THAT(provider.TestOnlyGetRefreshIntervalSecond(), 1);
}

TEST_F(GrpcTlsCertificateProviderTest, FailedKeyCertMatchOnEmptyPrivateKey) {
  absl::StatusOr<bool> status =
      PrivateKeyAndCertificateMatch(/*private_key=*/"", cert_chain_);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.status().message(), "Private key string is empty.");
}

TEST_F(GrpcTlsCertificateProviderTest, FailedKeyCertMatchOnEmptyCertificate) {
  absl::StatusOr<bool> status =
      PrivateKeyAndCertificateMatch(private_key_2_, /*cert_chain=*/"");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.status().message(), "Certificate string is empty.");
}

TEST_F(GrpcTlsCertificateProviderTest, FailedKeyCertMatchOnInvalidCertFormat) {
  absl::StatusOr<bool> status =
      PrivateKeyAndCertificateMatch(private_key_2_, "invalid_certificate");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.status().message(),
            "Conversion from PEM string to X509 failed.");
}

TEST_F(GrpcTlsCertificateProviderTest,
       FailedKeyCertMatchOnInvalidPrivateKeyFormat) {
  absl::StatusOr<bool> status =
      PrivateKeyAndCertificateMatch("invalid_private_key", cert_chain_2_);
  EXPECT_EQ(status.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.status().message(),
            "Conversion from PEM string to EVP_PKEY failed.");
}

TEST_F(GrpcTlsCertificateProviderTest, SuccessfulKeyCertMatch) {
  absl::StatusOr<bool> status =
      PrivateKeyAndCertificateMatch(private_key_2_, cert_chain_2_);
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(*status);
}

TEST_F(GrpcTlsCertificateProviderTest, FailedKeyCertMatchOnInvalidPair) {
  absl::StatusOr<bool> status =
      PrivateKeyAndCertificateMatch(private_key_2_, cert_chain_);
  EXPECT_TRUE(status.ok());
  EXPECT_FALSE(*status);
}

TEST_F(GrpcTlsCertificateProviderTest,
       SpiffeFileWatcherCertificateProviderWithGoodPaths) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          CA_CERT_PATH,
                                          kGoodSpiffeBundleMapPath, 1);
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap()),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, std::nullopt);
  EXPECT_THAT(
      watcher_state_2->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap()), PemKeyCertPairList())));
  CancelWatch(watcher_state_2);
}

TEST_F(
    GrpcTlsCertificateProviderTest,
    SpiffeFileWatcherCertificateProviderWithGoodPathsAndCredentialValidation) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          CA_CERT_PATH,
                                          kGoodSpiffeBundleMapPath, 1);
  EXPECT_EQ(provider.ValidateCredentials(), absl::OkStatus());
}

TEST_F(GrpcTlsCertificateProviderTest,
       SpiffeFileWatcherCertificateProviderWithMissingSpiffeBundlePath) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH_2, SERVER_CERT_PATH_2,
                                          CA_CERT_PATH, INVALID_PATH, 1);
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::InvalidArgumentError(
                "spiffe bundle map file invalid/path failed to load: INTERNAL: "
                "Failed to load file: invalid/path due to error(fdopen): No "
                "such file or directory"));
}

TEST_F(GrpcTlsCertificateProviderTest,
       SpiffeFileWatcherCertificateProviderWithMalformedSpiffeBundlePath) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH_2, SERVER_CERT_PATH_2,
                                          CA_CERT_PATH,
                                          kMalformedSpiffeBundleMapPath, 1);
  EXPECT_EQ(
      provider.ValidateCredentials(),
      absl::InvalidArgumentError(
          "spiffe bundle map file "
          "test/core/credentials/transport/tls/test_data/spiffe/test_bundles/"
          "spiffebundle_malformed.json failed to load: INVALID_ARGUMENT: "
          "errors validating JSON: [field: error:is not an object]"));
}

// The following tests write credential data to temporary files to test the
// transition behavior of the provider.
TEST_F(GrpcTlsCertificateProviderTest,
       SpiffeFileWatcherCertificateProviderOnBothRefreshed) {
  // Create temporary files and copy cert data into them.
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  TmpFile tmp_spiffe_bundle_map(spiffe_bundle_contents_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key.name(), tmp_identity_cert.name(),
      /*root_cert_path=*/"", tmp_spiffe_bundle_map.name(),
      /*refresh_interval_sec=*/1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // Expect to see the credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap()),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Copy new data to files.
  // TODO(ZhenLian): right now it is not completely atomic. Use the real atomic
  // update when the directory renaming is added in gpr.
  tmp_identity_key.RewriteFile(private_key_2_);
  tmp_identity_cert.RewriteFile(cert_chain_2_);
  tmp_spiffe_bundle_map.RewriteFile(spiffe_bundle_contents_2_);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see the new credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap2()),
          MakeCertKeyPairs(private_key_2_.c_str(), cert_chain_2_.c_str()))));
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateProviderTest,
       SpiffeFileWatcherCertificateProviderOnSpiffeBundleMapRefreshed) {
  // Create temporary files and copy cert data into them.
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  TmpFile tmp_spiffe_bundle_map(spiffe_bundle_contents_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key.name(), tmp_identity_cert.name(),
      /*root_cert_path=*/"", tmp_spiffe_bundle_map.name(),
      /*refresh_interval_sec=*/1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // Expect to see the credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap()),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Copy new data to files.
  // TODO(ZhenLian): right now it is not completely atomic. Use the real
  // atomic update when the directory renaming is added in gpr.
  tmp_spiffe_bundle_map.RewriteFile(spiffe_bundle_contents_2_);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see the new credential data.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap2()),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(
    GrpcTlsCertificateProviderTest,
    SpiffeFileWatcherCertificateProviderWithGoodAtFirstThenDeletedSpiffeBundleMap) {
  // Create temporary files and copy cert data into it.
  auto tmp_spiffe_bundle_map =
      std::make_unique<TmpFile>(spiffe_bundle_contents_);
  TmpFile tmp_identity_key(private_key_);
  TmpFile tmp_identity_cert(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key.name(), tmp_identity_cert.name(),
      /*root_cert_path=*/"", tmp_spiffe_bundle_map->name(),
      /*refresh_interval_sec=*/1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap()),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Delete root TmpFile object, which will remove the corresponding file.
  tmp_spiffe_bundle_map.reset();
  // Wait 2 seconds for the provider's refresh thread to read the deleted files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see errors sent to watchers, and no credential updates.
  // We have no ideas on how many errors we will receive, so we only check once.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::Contains(ErrorInfo(kRootError, "")));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Clean up.
  CancelWatch(watcher_state_1);
}

TEST_F(
    GrpcTlsCertificateProviderTest,
    SpiffeFileWatcherCertificateProviderWithGoodAtFirstThenDeletedBothCertsAndSpiffe) {
  // Create temporary files and copy cert data into it.
  auto tmp_spiffe_bundle_map =
      std::make_unique<TmpFile>(spiffe_bundle_contents_);
  auto tmp_identity_key = std::make_unique<TmpFile>(private_key_);
  auto tmp_identity_cert = std::make_unique<TmpFile>(cert_chain_);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      tmp_identity_key->name(), tmp_identity_cert->name(),
      /*root_cert_path=*/"", tmp_spiffe_bundle_map->name(),
      /*refresh_interval_sec=*/1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(MatchesCredentialInfo(
          EqSpiffeBundleMap(GetGoodSpiffeBundleMap()),
          MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  // Delete TmpFile objects, which will remove the corresponding files.
  tmp_spiffe_bundle_map.reset();
  tmp_identity_key.reset();
  tmp_identity_cert.reset();
  // Wait 2 seconds for the provider's refresh thread to read the deleted files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see errors sent to watchers, and no credential updates.
  // We have no ideas on how many errors we will receive, so we only check once.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::Contains(ErrorInfo(kRootError, kIdentityError)));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Clean up.
  CancelWatch(watcher_state_1);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
