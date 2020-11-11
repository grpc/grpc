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

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define INVALID_PATH "invalid/path"

namespace testing {

constexpr const char* kCertName = "cert_name";
constexpr const char* kRootError = "Unable to get latest root certificates.";
constexpr const char* kIdentityError =
    "Unable to get latest identity certificates.";

class GrpcTlsCertificateProviderTest : public ::testing::Test {
 protected:
  // Forward declaration.
  class TlsCertificatesTestWatcher;

  static grpc_core::PemKeyCertPairList MakeCertKeyPairs(const char* private_key,
                                                        const char* certs) {
    if (strcmp(private_key, "") == 0 && strcmp(certs, "") == 0) {
      return {};
    }
    grpc_ssl_pem_key_cert_pair* ssl_pair =
        static_cast<grpc_ssl_pem_key_cert_pair*>(
            gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
    ssl_pair->private_key = gpr_strdup(private_key);
    ssl_pair->cert_chain = gpr_strdup(certs);
    grpc_core::PemKeyCertPairList pem_key_cert_pairs;
    pem_key_cert_pairs.emplace_back(ssl_pair);
    return pem_key_cert_pairs;
  }

  // CredentialInfo contains the parameters when calling OnCertificatesChanged
  // of a watcher. When OnCertificatesChanged is invoked, we will push a
  // CredentialInfo to the cert_update_queue of state_, and check in each test
  // if the status updates are correct.
  struct CredentialInfo {
    std::string root_certs;
    grpc_core::PemKeyCertPairList key_cert_pairs;
    CredentialInfo(std::string root, grpc_core::PemKeyCertPairList key_cert)
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
        absl::optional<grpc_core::PemKeyCertPairList> key_cert_pairs) override {
      std::string updated_root;
      if (root_certs.has_value()) {
        updated_root = std::string(*root_certs);
      }
      grpc_core::PemKeyCertPairList updated_identity;
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
        root_error_str =
            std::string(grpc_core::StringViewFromSlice(root_error_slice));
      }
      if (identity_cert_error != GRPC_ERROR_NONE) {
        grpc_slice identity_error_slice;
        GPR_ASSERT(grpc_error_get_str(identity_cert_error,
                                      GRPC_ERROR_STR_DESCRIPTION,
                                      &identity_error_slice));
        identity_error_str =
            std::string(grpc_core::StringViewFromSlice(identity_error_slice));
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
    grpc_slice ca_slice, cert_slice, key_slice;
    GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                 grpc_load_file(CA_CERT_PATH, 1, &ca_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
    root_cert_ = std::string(grpc_core::StringViewFromSlice(ca_slice));
    private_key_ = std::string(grpc_core::StringViewFromSlice(key_slice));
    cert_chain_ = std::string(grpc_core::StringViewFromSlice(cert_slice));
    grpc_slice_unref(ca_slice);
    grpc_slice_unref(key_slice);
    grpc_slice_unref(cert_slice);
  }

  WatcherState* MakeWatcher(
      grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor,
      absl::optional<std::string> root_cert_name,
      absl::optional<std::string> identity_cert_name) {
    grpc_core::MutexLock lock(&mu_);
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
    grpc_core::MutexLock lock(&mu_);
    distributor_->CancelTlsCertificatesWatch(state->watcher);
    EXPECT_EQ(state->watcher, nullptr);
  }

  std::string root_cert_;
  std::string private_key_;
  std::string cert_chain_;
  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  // Use a std::list<> here to avoid the address invalidation caused by internal
  // reallocation of std::vector<>.
  std::list<WatcherState> watchers_;
  // This is to make watchers_ thread-safe.
  grpc_core::Mutex mu_;
};

TEST_F(GrpcTlsCertificateProviderTest, StaticDataCertificateProviderCreation) {
  grpc_core::StaticDataCertificateProvider provider(
      root_cert_, MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()));
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              testing::ElementsAre(CredentialInfo(
                  root_cert_, MakeCertKeyPairs(private_key_.c_str(),
                                               cert_chain_.c_str()))));
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              testing::ElementsAre(CredentialInfo(root_cert_, {})));
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), absl::nullopt, kCertName);
  EXPECT_THAT(
      watcher_state_3->GetCredentialQueue(),
      testing::ElementsAre(CredentialInfo(
          "", MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithGoodPaths) {
  grpc_core::FileWatcherCertificateProvider provider(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              testing::ElementsAre(CredentialInfo(
                  root_cert_, MakeCertKeyPairs(private_key_.c_str(),
                                               cert_chain_.c_str()))));
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              testing::ElementsAre(CredentialInfo(root_cert_, {})));
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), absl::nullopt, kCertName);
  EXPECT_THAT(
      watcher_state_3->GetCredentialQueue(),
      testing::ElementsAre(CredentialInfo(
          "", MakeCertKeyPairs(private_key_.c_str(), cert_chain_.c_str()))));
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateProviderTest,
       FileWatcherCertificateProviderWithBadPaths) {
  grpc_core::FileWatcherCertificateProvider provider(INVALID_PATH, INVALID_PATH,
                                                     INVALID_PATH, 1);
  // Watcher watching both root and identity certs.
  WatcherState* watcher_state_1 =
      MakeWatcher(provider.distributor(), kCertName, kCertName);
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              testing::ElementsAre(ErrorInfo(kRootError, kIdentityError)));
  EXPECT_EQ(watcher_state_1->GetCredentialQueue().size(), 0);
  CancelWatch(watcher_state_1);
  // Watcher watching only root certs.
  WatcherState* watcher_state_2 =
      MakeWatcher(provider.distributor(), kCertName, absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetErrorQueue(),
              testing::ElementsAre(ErrorInfo(kRootError, "")));
  EXPECT_EQ(watcher_state_2->GetCredentialQueue().size(), 0);
  CancelWatch(watcher_state_2);
  // Watcher watching only identity certs.
  WatcherState* watcher_state_3 =
      MakeWatcher(provider.distributor(), absl::nullopt, kCertName);
  EXPECT_THAT(watcher_state_3->GetErrorQueue(),
              testing::ElementsAre(ErrorInfo("", kIdentityError)));
  EXPECT_EQ(watcher_state_3->GetCredentialQueue().size(), 0);
  CancelWatch(watcher_state_3);
}

}  // namespace testing

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
