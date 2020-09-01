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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"

#include <gmock/gmock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <deque>
#include <thread>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace testing {

constexpr const char* kCertName1 = "cert_1_name";
constexpr const char* kCertName2 = "cert_2_name";
constexpr const char* kRootCert1Name = "root_cert_1_name";
constexpr const char* kRootCert1Contents = "root_cert_1_contents";
constexpr const char* kRootCert2Name = "root_cert_2_name";
constexpr const char* kRootCert2Contents = "root_cert_2_contents";
constexpr const char* kIdentityCert1Name = "identity_cert_1_name";
constexpr const char* kIdentityCert1PrivateKey = "identity_private_key_1";
constexpr const char* kIdentityCert1Contents = "identity_cert_1_contents";
constexpr const char* kIdentityCert2Name = "identity_cert_2_name";
constexpr const char* kIdentityCert2PrivateKey = "identity_private_key_2";
constexpr const char* kIdentityCert2Contents = "identity_cert_2_contents";
constexpr const char* kErrorMessage = "error_message";
constexpr const char* kRootErrorMessage = "root_error_message";
constexpr const char* kIdentityErrorMessage = "identity_error_message";

class GrpcTlsCertificateDistributorTest : public ::testing::Test {
 protected:
  // Forward declaration.
  class TlsCertificatesTestWatcher;

   ~GrpcTlsCertificateDistributorTest() {
      gpr_log(GPR_ERROR, "%s", "test's destructor is called");
    }

  struct CredentialInfo {
    absl::string_view root_certs;
    grpc_tls_certificate_distributor::PemKeyCertPairList key_cert_pairs;
    CredentialInfo(absl::string_view root, grpc_tls_certificate_distributor::PemKeyCertPairList key_cert)
        : root_certs(root),
          key_cert_pairs(std::move(key_cert)) {}
    bool operator==(const CredentialInfo& other) const {
      return root_certs == other.root_certs &&
             key_cert_pairs == other.key_cert_pairs;
    }
    ~CredentialInfo() {
      gpr_log(GPR_ERROR, "%s", "CredentialInfo's destructor is called");
    }
  };

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
    ~ErrorInfo() {
      gpr_log(GPR_ERROR, "%s", "ErrorInfo's destructor is called");
    }
  };

  struct WatcherState {
    TlsCertificatesTestWatcher* watcher = nullptr;
    std::deque<CredentialInfo> cert_update_queue;
    std::deque<ErrorInfo> error_queue;
  };

  class TlsCertificatesTestWatcher
      : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
   public:
    // ctor sets state->watcher to this.
    explicit TlsCertificatesTestWatcher(WatcherState* state)
        : state_(state) {
        state_->watcher = this;
    }

    // dtor sets state->watcher to nullptr.
    ~TlsCertificatesTestWatcher() {
      gpr_log(GPR_ERROR, "%s", "watcher's destructor is called");
      state_->watcher = nullptr;
    }

    void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) {
      gpr_log(GPR_ERROR, "%s", "OnCertificatesChanged is called");

    }

    void OnError(grpc_error* root_cert_error, grpc_error* identity_cert_error) {
      gpr_log(GPR_ERROR, "%s", "OnError is called");
      GRPC_ERROR_UNREF(root_cert_error);
      GRPC_ERROR_UNREF(identity_cert_error);
    }
   private:
    WatcherState* state_;
  };

  WatcherState* MakeWatcher(
      std::string root_cert_name, std::string identity_cert_name) {
    watchers_.emplace_back();
    // TlsCertificatesTestWatcher ctor takes a pointer to the WatcherState.
    // It sets WatcherState::watcher to point to itself.
    // The TlsCertificatesTestWatcher dtor will set WatcherState::watcher back to nullptr
    // to indicate that it's been destroyed.
    auto watcher =
        absl::make_unique<TlsCertificatesTestWatcher>(&watchers_.back());
    distributor_.WatchTlsCertificates(std::move(watcher), root_cert_name, identity_cert_name);
    return &watchers_.back();
  }

  grpc_tls_certificate_distributor distributor_;
  std::vector<WatcherState> watchers_;
};

TEST_F(GrpcTlsCertificateDistributorTest, GoodTest) {
  WatcherState* watcher_state_1 = MakeWatcher(kRootCert1Name, kIdentityCert1Name);
  distributor_.CancelTlsCertificatesWatch(watcher_state_1->watcher);
}


TEST_F(GrpcTlsCertificateDistributorTest, WeirdTest) {
  WatcherState* watcher_state_1 = MakeWatcher(kRootCert1Name, kIdentityCert1Name);
  gpr_log(GPR_ERROR, "---------------watchers_ size is %zu-----------", watchers_.size());
  for (auto& state : watchers_) {
    gpr_log(GPR_ERROR, "cert_update_queue size is %zu", state.cert_update_queue.size());
    gpr_log(GPR_ERROR, "error_queue size is %zu", state.error_queue.size());
  }
  WatcherState* watcher_state_2 = MakeWatcher(kRootCert2Name, kIdentityCert1Name);
  gpr_log(GPR_ERROR, "---------------watchers_ size is %zu-----------", watchers_.size());
  for (auto& state : watchers_) {
    gpr_log(GPR_ERROR, "cert_update_queue size is %zu", state.cert_update_queue.size());
    gpr_log(GPR_ERROR, "error_queue size is %zu", state.error_queue.size());
  }
  gpr_log(GPR_ERROR, "%s", "access watcher_state_1->cert_update_queue.size()");
  // Calling watcher_state_1->cert_update_queue.size() would break.
  gpr_log(GPR_ERROR, "%d", watcher_state_1->cert_update_queue.size());
  gpr_log(GPR_ERROR, "%s", "This log won't be reached");
  distributor_.CancelTlsCertificatesWatch(watcher_state_1->watcher);
  distributor_.CancelTlsCertificatesWatch(watcher_state_2->watcher);
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
