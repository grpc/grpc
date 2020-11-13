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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#include <gmock/gmock.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <deque>
#include <list>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define INVALID_PATH "invalid/path"

namespace grpc_core {

namespace testing {

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
    std::string root_certs;
    PemKeyCertPairList key_cert_pairs;
    CredentialInfo(std::string root, PemKeyCertPairList key_cert)
        : root_certs(std::move(root)), key_cert_pairs(std::move(key_cert)) {}
    bool operator==(const CredentialInfo& other) const {
      return root_certs == other.root_certs &&
             key_cert_pairs == other.key_cert_pairs;
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

    std::deque<CredentialInfo> GetCredentialQueue() {
      // We move the data member value so the data member will be re-initiated
      // with size 0, and ready for the next check.
      return std::move(cert_update_queue);
    }
    std::deque<ErrorInfo> GetErrorQueue() {
      // We move the data member value so the data member will be re-initiated
      // with size 0, and ready for the next check.
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
        absl::optional<absl::string_view> root_certs,
        absl::optional<PemKeyCertPairList> key_cert_pairs) override {
      std::string updated_root;
      if (root_certs.has_value()) {
        updated_root = std::string(*root_certs);
      }
      PemKeyCertPairList updated_identity;
      if (key_cert_pairs.has_value()) {
        updated_identity = std::move(*key_cert_pairs);
      }
      state_->cert_update_queue.emplace_back(std::move(updated_root),
                                             std::move(updated_identity));
    }

    void OnError(grpc_error* root_cert_error,
                 grpc_error* identity_cert_error) override {
      GPR_ASSERT(root_cert_error != GRPC_ERROR_NONE ||
                 identity_cert_error != GRPC_ERROR_NONE);
      std::string root_error_str;
      std::string identity_error_str;
      if (root_cert_error != GRPC_ERROR_NONE) {
        grpc_slice root_error_slice;
        GPR_ASSERT(grpc_error_get_str(
            root_cert_error, GRPC_ERROR_STR_DESCRIPTION, &root_error_slice));
        root_error_str = std::string(StringViewFromSlice(root_error_slice));
      }
      if (identity_cert_error != GRPC_ERROR_NONE) {
        grpc_slice identity_error_slice;
        GPR_ASSERT(grpc_error_get_str(identity_cert_error,
                                      GRPC_ERROR_STR_DESCRIPTION,
                                      &identity_error_slice));
        identity_error_str =
            std::string(StringViewFromSlice(identity_error_slice));
      }
      state_->error_queue.emplace_back(std::move(root_error_str),
                                       std::move(identity_error_str));
      GRPC_ERROR_UNREF(root_cert_error);
      GRPC_ERROR_UNREF(identity_cert_error);
    }

   private:
    WatcherState* state_;
  };

  void SetUp() override {
    LoadCredentialData(CA_CERT_PATH, 1, &root_cert_);
    LoadCredentialData(CA_CERT_PATH, 0, &root_cert_no_terminator_);
    LoadCredentialData(SERVER_CERT_PATH, 1, &cert_chain_);
    LoadCredentialData(SERVER_CERT_PATH, 0, &cert_chain_no_terminator_);
    LoadCredentialData(SERVER_KEY_PATH, 1, &private_key_);
    LoadCredentialData(SERVER_KEY_PATH, 0, &private_key_no_terminator_);
  }

  static PemKeyCertPairList MakeCertKeyPairs(const char* private_key,
                                             const char* certs) {
    if (strcmp(private_key, "") == 0 && strcmp(certs, "") == 0) {
      return {};
    }
    grpc_ssl_pem_key_cert_pair* ssl_pair =
        static_cast<grpc_ssl_pem_key_cert_pair*>(
            gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
    ssl_pair->private_key = gpr_strdup(private_key);
    ssl_pair->cert_chain = gpr_strdup(certs);
    PemKeyCertPairList pem_key_cert_pairs;
    pem_key_cert_pairs.emplace_back(ssl_pair);
    return pem_key_cert_pairs;
  }

  static void LoadCredentialData(const char* path, int add_null_terminator,
                                 std::string* credential) {
    grpc_slice slice = grpc_empty_slice();
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(path, add_null_terminator, &slice)));
    *credential = std::string(StringViewFromSlice(slice));
    grpc_slice_unref(slice);
  }

  WatcherState* MakeWatcher(
      RefCountedPtr<grpc_tls_certificate_distributor> distributor,
      absl::optional<std::string> root_cert_name,
      absl::optional<std::string> identity_cert_name) {
    MutexLock lock(&mu_);
    distributor_ = distributor;
    watchers_.emplace_back();
    // TlsCertificatesTestWatcher ctor takes a pointer to the WatcherState.
    // It sets WatcherState::watcher to point to itself.
    // The TlsCertificatesTestWatcher dtor will set WatcherState::watcher back
    // to nullptr to indicate that it's been destroyed.
    auto watcher =
        absl::make_unique<TlsCertificatesTestWatcher>(&watchers_.back());
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
  std::string root_cert_no_terminator_;
  std::string private_key_;
  std::string private_key_no_terminator_;
  std::string cert_chain_;
  std::string cert_chain_no_terminator_;
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
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  root_cert_, MakeCertKeyPairs(private_key_.c_str(),
                                               cert_chain_.c_str()))));
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(root_cert_, {})));
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), absl::nullopt, kCertName);
  EXPECT_THAT(
      watcher_state_3->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          "", MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodPaths) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          CA_CERT_PATH, 1);
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  root_cert_, MakeCertKeyPairs(private_key_.c_str(),
                                               cert_chain_.c_str()))));
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(root_cert_, {})));
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), absl::nullopt, kCertName);
  EXPECT_THAT(
      watcher_state_3->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          "", MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithBadPaths) {
  FileWatcherCertificateProvider provider(INVALID_PATH, INVALID_PATH,
                                          INVALID_PATH, 1);
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootError, kIdentityError)));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootError, "")));
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), absl::nullopt, kCertName);
  EXPECT_THAT(watcher_state_3->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo("", kIdentityError)));
  EXPECT_THAT(watcher_state_3->GetCredentialQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderOnCertificateRefreshed) {
  // Create a temporary file and copy root cert data into it.
  FILE* root_cert_tmp = nullptr;
  char* root_cert_tmp_name = nullptr;
  root_cert_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_root_cert",
                              &root_cert_tmp_name);
  GPR_ASSERT(root_cert_tmp_name != nullptr);
  GPR_ASSERT(root_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(root_cert_no_terminator_.c_str(), 1,
                    root_cert_no_terminator_.size(),
                    root_cert_tmp) == root_cert_no_terminator_.size());
  fclose(root_cert_tmp);
  // Create a temporary file and copy identity key data into it.
  FILE* identity_key_tmp = nullptr;
  char* identity_key_tmp_name = nullptr;
  identity_key_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_identity_key",
                                 &identity_key_tmp_name);
  GPR_ASSERT(identity_key_tmp_name != nullptr);
  GPR_ASSERT(identity_key_tmp != nullptr);
  GPR_ASSERT(fwrite(private_key_no_terminator_.c_str(), 1,
                    private_key_no_terminator_.size(),
                    identity_key_tmp) == private_key_no_terminator_.size());
  fclose(identity_key_tmp);
  // Create a temporary file and copy identity cert data into it.
  FILE* identity_cert_tmp = nullptr;
  char* identity_cert_tmp_name = nullptr;
  identity_cert_tmp = gpr_tmpfile(
      "GrpcTlsCertificateProviderTest_identity_cert", &identity_cert_tmp_name);
  GPR_ASSERT(identity_cert_tmp_name != nullptr);
  GPR_ASSERT(identity_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(cert_chain_no_terminator_.c_str(), 1,
                    cert_chain_no_terminator_.size(),
                    identity_cert_tmp) == cert_chain_no_terminator_.size());
  fclose(identity_cert_tmp);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      identity_key_tmp_name, identity_cert_tmp_name, root_cert_tmp_name, 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  root_cert_, MakeCertKeyPairs(private_key_.c_str(),
                                               cert_chain_.c_str()))));
  // Copy new data to files. We copy root cert data into identity key file,
  // identity key data into identity cert file, and identity cert data into root
  // cert file. Then check if the data is refreshed.
  root_cert_tmp = fopen(root_cert_tmp_name, "w");
  GPR_ASSERT(root_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(cert_chain_no_terminator_.c_str(), 1,
                    cert_chain_no_terminator_.size(),
                    root_cert_tmp) == cert_chain_no_terminator_.size());
  fclose(root_cert_tmp);
  identity_key_tmp = fopen(identity_key_tmp_name, "w");
  GPR_ASSERT(identity_key_tmp != nullptr);
  GPR_ASSERT(fwrite(root_cert_no_terminator_.c_str(), 1,
                    root_cert_no_terminator_.size(),
                    identity_key_tmp) == root_cert_no_terminator_.size());
  fclose(identity_key_tmp);
  identity_cert_tmp = fopen(identity_cert_tmp_name, "w");
  GPR_ASSERT(identity_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(private_key_no_terminator_.c_str(), 1,
                    private_key_no_terminator_.size(),
                    identity_cert_tmp) == private_key_no_terminator_.size());
  fclose(identity_cert_tmp);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see the new credential data is loaded.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  cert_chain_,
                  MakeCertKeyPairs(root_cert_.c_str(), private_key_.c_str()))));
  // Clean up.
  CancelWatch(watcher_state_1);
  gpr_free(identity_key_tmp_name);
  gpr_free(identity_cert_tmp_name);
  gpr_free(root_cert_tmp_name);
  remove(root_cert_tmp_name);
  remove(identity_key_tmp_name);
  remove(identity_cert_tmp_name);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodAtFirstThenDeletedFiles) {
  // Create a temporary file and copy root cert data into it.
  FILE* root_cert_tmp = nullptr;
  char* root_cert_tmp_name = nullptr;
  root_cert_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_root_cert",
                              &root_cert_tmp_name);
  GPR_ASSERT(root_cert_tmp_name != nullptr);
  GPR_ASSERT(root_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(root_cert_no_terminator_.c_str(), 1,
                    root_cert_no_terminator_.size(),
                    root_cert_tmp) == root_cert_no_terminator_.size());
  fclose(root_cert_tmp);
  // Create a temporary file and copy identity key data into it.
  FILE* identity_key_tmp = nullptr;
  char* identity_key_tmp_name = nullptr;
  identity_key_tmp = gpr_tmpfile("GrpcTlsCertificateProviderTest_identity_key",
                                 &identity_key_tmp_name);
  GPR_ASSERT(identity_key_tmp_name != nullptr);
  GPR_ASSERT(identity_key_tmp != nullptr);
  GPR_ASSERT(fwrite(private_key_no_terminator_.c_str(), 1,
                    private_key_no_terminator_.size(),
                    identity_key_tmp) == private_key_no_terminator_.size());
  fclose(identity_key_tmp);
  // Create a temporary file and copy identity cert data into it.
  FILE* identity_cert_tmp = nullptr;
  char* identity_cert_tmp_name = nullptr;
  identity_cert_tmp = gpr_tmpfile(
      "GrpcTlsCertificateProviderTest_identity_cert", &identity_cert_tmp_name);
  GPR_ASSERT(identity_cert_tmp_name != nullptr);
  GPR_ASSERT(identity_cert_tmp != nullptr);
  GPR_ASSERT(fwrite(cert_chain_no_terminator_.c_str(), 1,
                    cert_chain_no_terminator_.size(),
                    identity_cert_tmp) == cert_chain_no_terminator_.size());
  fclose(identity_cert_tmp);
  // Create FileWatcherCertificateProvider.
  FileWatcherCertificateProvider provider(
      identity_key_tmp_name, identity_cert_tmp_name, root_cert_tmp_name, 1);
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  // The initial data is all good, so we expect to have successful credential
  // updates.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  root_cert_, MakeCertKeyPairs(private_key_.c_str(),
                                               cert_chain_.c_str()))));
  // Remove all the files.
  remove(root_cert_tmp_name);
  remove(identity_key_tmp_name);
  remove(identity_cert_tmp_name);
  // Wait 2 seconds for the provider's refresh thread to read the deleted files.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(2, GPR_TIMESPAN)));
  // Expect to see errors sent to watchers, and no credential updates.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootError, kIdentityError)));
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Clean up.
  CancelWatch(watcher_state_1);
  gpr_free(identity_key_tmp_name);
  gpr_free(identity_cert_tmp_name);
  gpr_free(root_cert_tmp_name);
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
