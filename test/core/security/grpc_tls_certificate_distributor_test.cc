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

#include <deque>
#include <list>
#include <string>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc_core {

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

    void OnError(grpc_error_handle root_cert_error,
                 grpc_error_handle identity_cert_error) override {
      GPR_ASSERT(!root_cert_error.ok() || !identity_cert_error.ok());
      std::string root_error_str;
      std::string identity_error_str;
      if (!root_cert_error.ok()) {
        GPR_ASSERT(grpc_error_get_str(
            root_cert_error, StatusStrProperty::kDescription, &root_error_str));
      }
      if (!identity_cert_error.ok()) {
        GPR_ASSERT(grpc_error_get_str(identity_cert_error,
                                      StatusStrProperty::kDescription,
                                      &identity_error_str));
      }
      state_->error_queue.emplace_back(std::move(root_error_str),
                                       std::move(identity_error_str));
    }

   private:
    WatcherState* state_;
  };

  // CallbackStatus contains the parameters when calling watch_status_callback_
  // of the distributor. When a particular callback is invoked, we will push a
  // CallbackStatus to a callback_queue_, and check in each test if the status
  // updates are correct.
  struct CallbackStatus {
    std::string cert_name;
    bool root_being_watched;
    bool identity_being_watched;
    CallbackStatus(std::string name, bool root_watched, bool identity_watched)
        : cert_name(std::move(name)),
          root_being_watched(root_watched),
          identity_being_watched(identity_watched) {}
    bool operator==(const CallbackStatus& other) const {
      return cert_name == other.cert_name &&
             root_being_watched == other.root_being_watched &&
             identity_being_watched == other.identity_being_watched;
    }
  };

  void SetUp() override {
    distributor_.SetWatchStatusCallback([this](std::string cert_name,
                                               bool root_being_watched,
                                               bool identity_being_watched) {
      callback_queue_.emplace_back(std::move(cert_name), root_being_watched,
                                   identity_being_watched);
    });
  }

  WatcherState* MakeWatcher(absl::optional<std::string> root_cert_name,
                            absl::optional<std::string> identity_cert_name) {
    MutexLock lock(&mu_);
    watchers_.emplace_back();
    // TlsCertificatesTestWatcher ctor takes a pointer to the WatcherState.
    // It sets WatcherState::watcher to point to itself.
    // The TlsCertificatesTestWatcher dtor will set WatcherState::watcher back
    // to nullptr to indicate that it's been destroyed.
    auto watcher =
        std::make_unique<TlsCertificatesTestWatcher>(&watchers_.back());
    distributor_.WatchTlsCertificates(std::move(watcher),
                                      std::move(root_cert_name),
                                      std::move(identity_cert_name));
    return &watchers_.back();
  }

  void CancelWatch(WatcherState* state) {
    MutexLock lock(&mu_);
    distributor_.CancelTlsCertificatesWatch(state->watcher);
    EXPECT_EQ(state->watcher, nullptr);
  }

  std::deque<CallbackStatus> GetCallbackQueue() {
    // We move the data member value so the data member will be re-initiated
    // with size 0, and ready for the next check.
    return std::move(callback_queue_);
  }

  grpc_tls_certificate_distributor distributor_;
  // Use a std::list<> here to avoid the address invalidation caused by internal
  // reallocation of std::vector<>.
  std::list<WatcherState> watchers_;
  std::deque<CallbackStatus> callback_queue_;
  // This is to make watchers_ and callback_queue_ thread-safe.
  Mutex mu_;
};

TEST_F(GrpcTlsCertificateDistributorTest, BasicCredentialBehaviors) {
  EXPECT_FALSE(distributor_.HasRootCerts(kRootCert1Name));
  EXPECT_FALSE(distributor_.HasKeyCertPairs(kIdentityCert1Name));
  // After setting the certificates to the corresponding cert names, the
  // distributor should possess the corresponding certs.
  distributor_.SetKeyMaterials(kRootCert1Name, kRootCert1Contents,
                               absl::nullopt);
  EXPECT_TRUE(distributor_.HasRootCerts(kRootCert1Name));
  distributor_.SetKeyMaterials(
      kIdentityCert1Name, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  EXPECT_TRUE(distributor_.HasKeyCertPairs(kIdentityCert1Name));
  // Querying a non-existing cert name should return false.
  EXPECT_FALSE(distributor_.HasRootCerts(kRootCert2Name));
  EXPECT_FALSE(distributor_.HasKeyCertPairs(kIdentityCert2Name));
}

TEST_F(GrpcTlsCertificateDistributorTest, UpdateCredentialsOnAnySide) {
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, true)));
  // SetKeyMaterials should trigger watcher's OnCertificatesChanged method.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  // Set root certs should trigger watcher's OnCertificatesChanged again.
  distributor_.SetKeyMaterials(kCertName1, kRootCert2Contents, absl::nullopt);
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert2Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  // Set identity certs should trigger watcher's OnCertificatesChanged again.
  distributor_.SetKeyMaterials(
      kCertName1, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert2Contents,
          MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents))));
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateDistributorTest, SameIdentityNameDiffRootName) {
  // Register watcher 1.
  WatcherState* watcher_state_1 =
      MakeWatcher(kRootCert1Name, kIdentityCert1Name);
  EXPECT_THAT(
      GetCallbackQueue(),
      ::testing::ElementsAre(CallbackStatus(kRootCert1Name, true, false),
                             CallbackStatus(kIdentityCert1Name, false, true)));
  // Register watcher 2.
  WatcherState* watcher_state_2 =
      MakeWatcher(kRootCert2Name, kIdentityCert1Name);
  EXPECT_THAT(GetCallbackQueue(), ::testing::ElementsAre(CallbackStatus(
                                      kRootCert2Name, true, false)));
  // Push credential updates to kRootCert1Name and check if the status works as
  // expected.
  distributor_.SetKeyMaterials(kRootCert1Name, kRootCert1Contents,
                               absl::nullopt);
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert1Contents, {})));
  // Push credential updates to kRootCert2Name.
  distributor_.SetKeyMaterials(kRootCert2Name, kRootCert2Contents,
                               absl::nullopt);
  // Check the updates are delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert2Contents, {})));
  // Push credential updates to kIdentityCert1Name and check if the status works
  // as expected.
  distributor_.SetKeyMaterials(
      kIdentityCert1Name, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1 and watcher 2.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  EXPECT_THAT(
      watcher_state_2->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert2Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  // Cancel watcher 1.
  CancelWatch(watcher_state_1);
  EXPECT_THAT(GetCallbackQueue(), ::testing::ElementsAre(CallbackStatus(
                                      kRootCert1Name, false, false)));
  // Cancel watcher 2.
  CancelWatch(watcher_state_2);
  EXPECT_THAT(
      GetCallbackQueue(),
      ::testing::ElementsAre(CallbackStatus(kRootCert2Name, false, false),
                             CallbackStatus(kIdentityCert1Name, false, false)));
}

TEST_F(GrpcTlsCertificateDistributorTest, SameRootNameDiffIdentityName) {
  // Register watcher 1.
  WatcherState* watcher_state_1 =
      MakeWatcher(kRootCert1Name, kIdentityCert1Name);
  EXPECT_THAT(
      GetCallbackQueue(),
      ::testing::ElementsAre(CallbackStatus(kRootCert1Name, true, false),
                             CallbackStatus(kIdentityCert1Name, false, true)));
  // Register watcher 2.
  WatcherState* watcher_state_2 =
      MakeWatcher(kRootCert1Name, kIdentityCert2Name);
  EXPECT_THAT(GetCallbackQueue(), ::testing::ElementsAre(CallbackStatus(
                                      kIdentityCert2Name, false, true)));
  // Push credential updates to kRootCert1Name and check if the status works as
  // expected.
  distributor_.SetKeyMaterials(kRootCert1Name, kRootCert1Contents,
                               absl::nullopt);
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert1Contents, {})));
  // Check the updates are delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert1Contents, {})));
  // Push credential updates to SetKeyMaterials.
  distributor_.SetKeyMaterials(
      kIdentityCert1Name, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  // Push credential updates to kIdentityCert2Name.
  distributor_.SetKeyMaterials(
      kIdentityCert2Name, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Check the updates are delivered to watcher 2.
  EXPECT_THAT(
      watcher_state_2->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents))));
  // Cancel watcher 1.
  CancelWatch(watcher_state_1);
  EXPECT_THAT(GetCallbackQueue(), ::testing::ElementsAre(CallbackStatus(
                                      kIdentityCert1Name, false, false)));
  // Cancel watcher 2.
  CancelWatch(watcher_state_2);
  EXPECT_THAT(
      GetCallbackQueue(),
      ::testing::ElementsAre(CallbackStatus(kRootCert1Name, false, false),
                             CallbackStatus(kIdentityCert2Name, false, false)));
}

TEST_F(GrpcTlsCertificateDistributorTest,
       AddAndCancelFirstWatcherForSameRootAndIdentityCertName) {
  // Register watcher 1 watching kCertName1 for both root and identity certs.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, true)));
  // Push credential updates to kCertName1 and check if the status works as
  // expected.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  // Cancel watcher 1.
  CancelWatch(watcher_state_1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, false, false)));
}

TEST_F(GrpcTlsCertificateDistributorTest,
       AddAndCancelFirstWatcherForIdentityCertNameWithRootBeingWatched) {
  // Register watcher 1 watching kCertName1 for root certs.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, absl::nullopt);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, false)));
  // Register watcher 2 watching kCertName1 for identity certs.
  WatcherState* watcher_state_2 = MakeWatcher(absl::nullopt, kCertName1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, true)));
  // Push credential updates to kCertName1 and check if the status works as
  // expected.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert1Contents, {})));
  // Check the updates are delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  "", MakeCertKeyPairs(kIdentityCert1PrivateKey,
                                       kIdentityCert1Contents))));
  // Push root cert updates to kCertName1.
  distributor_.SetKeyMaterials(kCertName1, kRootCert2Contents, absl::nullopt);
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert2Contents, {})));
  // Check the updates are not delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(), ::testing::ElementsAre());
  // Push identity cert updates to kCertName1.
  distributor_.SetKeyMaterials(
      kCertName1, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Check the updates are not delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Check the updates are delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  "", MakeCertKeyPairs(kIdentityCert2PrivateKey,
                                       kIdentityCert2Contents))));
  watcher_state_2->cert_update_queue.clear();
  // Cancel watcher 2.
  CancelWatch(watcher_state_2);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, false)));
  // Cancel watcher 1.
  CancelWatch(watcher_state_1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, false, false)));
}

TEST_F(GrpcTlsCertificateDistributorTest,
       AddAndCancelFirstWatcherForRootCertNameWithIdentityBeingWatched) {
  // Register watcher 1 watching kCertName1 for identity certs.
  WatcherState* watcher_state_1 = MakeWatcher(absl::nullopt, kCertName1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, false, true)));
  // Register watcher 2 watching kCertName1 for root certs.
  WatcherState* watcher_state_2 = MakeWatcher(kCertName1, absl::nullopt);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, true)));
  // Push credential updates to kCertName1 and check if the status works as
  // expected.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  "", MakeCertKeyPairs(kIdentityCert1PrivateKey,
                                       kIdentityCert1Contents))));
  // Check the updates are delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert1Contents, {})));
  // Push root cert updates to kCertName1.
  distributor_.SetKeyMaterials(kCertName1, kRootCert2Contents, absl::nullopt);
  // Check the updates are delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert2Contents, {})));
  // Check the updates are not delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(), ::testing::ElementsAre());
  // Push identity cert updates to kCertName1.
  distributor_.SetKeyMaterials(
      kCertName1, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Check the updates are not delivered to watcher 2.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(), ::testing::ElementsAre());
  // Check the updates are delivered to watcher 1.
  EXPECT_THAT(watcher_state_1->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  "", MakeCertKeyPairs(kIdentityCert2PrivateKey,
                                       kIdentityCert2Contents))));
  // Cancel watcher 2.
  CancelWatch(watcher_state_2);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, false, true)));
  // Cancel watcher 1.
  CancelWatch(watcher_state_1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, false, false)));
}

TEST_F(GrpcTlsCertificateDistributorTest,
       RemoveAllWatchersForCertNameAndAddAgain) {
  // Register watcher 1 and watcher 2 watching kCertName1 for root and identity
  // certs.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, true)));
  WatcherState* watcher_state_2 = MakeWatcher(kCertName1, kCertName1);
  EXPECT_THAT(GetCallbackQueue(), ::testing::ElementsAre());
  // Push credential updates to kCertName1.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Cancel watcher 2.
  CancelWatch(watcher_state_2);
  EXPECT_THAT(GetCallbackQueue(), ::testing::ElementsAre());
  // Cancel watcher 1.
  CancelWatch(watcher_state_1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, false, false)));
  // Register watcher 3 watching kCertName for root and identity certs.
  WatcherState* watcher_state_3 = MakeWatcher(kCertName1, kCertName1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, true)));
  // Push credential updates to kCertName1.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert2Contents,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Check the updates are delivered to watcher 3.
  EXPECT_THAT(
      watcher_state_3->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert2Contents,
          MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents))));
  // Cancel watcher 3.
  CancelWatch(watcher_state_3);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, false, false)));
}

TEST_F(GrpcTlsCertificateDistributorTest, ResetCallbackToNull) {
  // Register watcher 1 watching kCertName1 for root and identity certs.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  EXPECT_THAT(GetCallbackQueue(),
              ::testing::ElementsAre(CallbackStatus(kCertName1, true, true)));
  // Reset callback to nullptr.
  distributor_.SetWatchStatusCallback(nullptr);
  // Cancel watcher 1 shouldn't trigger any callback.
  CancelWatch(watcher_state_1);
  EXPECT_THAT(GetCallbackQueue(), ::testing::ElementsAre());
}

TEST_F(GrpcTlsCertificateDistributorTest, SetKeyMaterialsInCallback) {
  distributor_.SetWatchStatusCallback([this](std::string cert_name,
                                             bool /*root_being_watched*/,
                                             bool /*identity_being_watched*/) {
    distributor_.SetKeyMaterials(
        cert_name, kRootCert1Contents,
        MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  });
  auto verify_function = [this](std::string cert_name) {
    WatcherState* watcher_state_1 = MakeWatcher(cert_name, cert_name);
    // Check the updates are delivered to watcher 1.
    EXPECT_THAT(
        watcher_state_1->GetCredentialQueue(),
        ::testing::ElementsAre(CredentialInfo(
            kRootCert1Contents, MakeCertKeyPairs(kIdentityCert1PrivateKey,
                                                 kIdentityCert1Contents))));
    CancelWatch(watcher_state_1);
  };
  // Start 10 threads that will register a watcher to a new cert name, verify
  // the key materials being set, and then cancel the watcher, to make sure the
  // lock mechanism in the distributor is safe.
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(verify_function, std::to_string(i));
  }
  for (auto& th : threads) {
    th.join();
  }
}

TEST_F(GrpcTlsCertificateDistributorTest, WatchACertInfoWithValidCredentials) {
  // Push credential updates to kCertName1.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Push root credential updates to kCertName2.
  distributor_.SetKeyMaterials(kRootCert2Name, kRootCert2Contents,
                               absl::nullopt);
  // Push identity credential updates to kCertName2.
  distributor_.SetKeyMaterials(
      kIdentityCert2Name, absl::nullopt,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Register watcher 1.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  // watcher 1 should receive the credentials right away.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  CancelWatch(watcher_state_1);
  // Register watcher 2.
  WatcherState* watcher_state_2 = MakeWatcher(kRootCert2Name, absl::nullopt);
  // watcher 2 should receive the root credentials right away.
  EXPECT_THAT(watcher_state_2->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(kRootCert2Contents, {})));
  // Register watcher 3.
  WatcherState* watcher_state_3 =
      MakeWatcher(absl::nullopt, kIdentityCert2Name);
  // watcher 3 should received the identity credentials right away.
  EXPECT_THAT(watcher_state_3->GetCredentialQueue(),
              ::testing::ElementsAre(CredentialInfo(
                  "", MakeCertKeyPairs(kIdentityCert2PrivateKey,
                                       kIdentityCert2Contents))));
  CancelWatch(watcher_state_2);
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       SetErrorForCertForBothRootAndIdentity) {
  // Register watcher 1.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  // Calling SetErrorForCert on both cert names should only call one OnError
  // on watcher 1.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(
                  ErrorInfo(kRootErrorMessage, kIdentityErrorMessage)));
  // Calling SetErrorForCert on root cert name should call OnError
  // on watcher 1 again.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kErrorMessage),
                               absl::nullopt);
  EXPECT_THAT(
      watcher_state_1->GetErrorQueue(),
      ::testing::ElementsAre(ErrorInfo(kErrorMessage, kIdentityErrorMessage)));
  // Calling SetErrorForCert on identity cert name should call OnError
  // on watcher 1 again.
  distributor_.SetErrorForCert(kCertName1, absl::nullopt,
                               GRPC_ERROR_CREATE(kErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kErrorMessage, kErrorMessage)));
  distributor_.CancelTlsCertificatesWatch(watcher_state_1->watcher);
  EXPECT_EQ(watcher_state_1->watcher, nullptr);
}

TEST_F(GrpcTlsCertificateDistributorTest, SetErrorForCertForRootOrIdentity) {
  // Register watcher 1.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, absl::nullopt);
  // Calling SetErrorForCert on root name should only call one OnError
  // on watcher 1.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               absl::nullopt);
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootErrorMessage, "")));
  // Calling SetErrorForCert on identity name should do nothing.
  distributor_.SetErrorForCert(kCertName1, absl::nullopt,
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(), ::testing::ElementsAre());
  // Calling SetErrorForCert on both names should still get one OnError call.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootErrorMessage, "")));
  CancelWatch(watcher_state_1);
  // Register watcher 2.
  WatcherState* watcher_state_2 = MakeWatcher(absl::nullopt, kCertName1);
  // Calling SetErrorForCert on identity name should only call one OnError
  // on watcher 2.
  distributor_.SetErrorForCert(kCertName1, absl::nullopt,
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_2->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo("", kIdentityErrorMessage)));
  // Calling SetErrorForCert on root name should do nothing.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetErrorQueue(), ::testing::ElementsAre());
  // Calling SetErrorForCert on both names should still get one OnError call.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_2->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo("", kIdentityErrorMessage)));
  CancelWatch(watcher_state_2);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       SetErrorForIdentityNameWithPreexistingErrorForRootName) {
  // SetErrorForCert for kCertName1.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  // Register watcher 1 for kCertName1 as root and kCertName2 as identity.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName2);
  // Should trigger OnError call right away since kCertName1 has error.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootErrorMessage, "")));
  // Calling SetErrorForCert on kCertName2 should trigger OnError with both
  // errors, because kCertName1 also has error.
  distributor_.SetErrorForCert(kCertName2, absl::nullopt,
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(
                  ErrorInfo(kRootErrorMessage, kIdentityErrorMessage)));
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       SetErrorForCertForRootNameWithSameNameForIdentityErrored) {
  // SetErrorForCert for kCertName1.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  // Register watcher 1 for kCertName2 as root and kCertName1 as identity.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName2, kCertName1);
  // Should trigger OnError call right away since kCertName2 has error.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo("", kIdentityErrorMessage)));
  // Calling SetErrorForCert on kCertName2 should trigger OnError with both
  // errors, because kCertName1 also has error.
  distributor_.SetErrorForCert(kCertName2, GRPC_ERROR_CREATE(kRootErrorMessage),
                               absl::nullopt);
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(
                  ErrorInfo(kRootErrorMessage, kIdentityErrorMessage)));
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       SetErrorForIdentityNameWithoutErrorForRootName) {
  // Register watcher 1 for kCertName1 as root and kCertName2 as identity.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName2);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(), ::testing::ElementsAre());
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor_.SetErrorForCert(kCertName2, absl::nullopt,
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo("", kIdentityErrorMessage)));
  CancelWatch(watcher_state_1);
  // Register watcher 2 for kCertName2 as identity and a non-existing name
  // kRootCert1Name as root.
  WatcherState* watcher_state_2 = MakeWatcher(kRootCert1Name, kCertName2);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_state_2->GetErrorQueue(), ::testing::ElementsAre());
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor_.SetErrorForCert(kCertName2, absl::nullopt,
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_2->error_queue,
              ::testing::ElementsAre(ErrorInfo("", kIdentityErrorMessage)));
  CancelWatch(watcher_state_2);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       SetErrorForRootNameWithPreexistingErrorForIdentityName) {
  WatcherState* watcher_state_1 = MakeWatcher(kCertName2, kCertName1);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_state_1->GetErrorQueue(), ::testing::ElementsAre());
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor_.SetErrorForCert(kCertName2, GRPC_ERROR_CREATE(kRootErrorMessage),
                               absl::nullopt);
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootErrorMessage, "")));
  CancelWatch(watcher_state_1);
  // Register watcher 2 for kCertName2 as root and a non-existing name
  // kIdentityCert1Name as identity.
  WatcherState* watcher_state_2 = MakeWatcher(kCertName2, kIdentityCert1Name);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_state_2->GetErrorQueue(), ::testing::ElementsAre());
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor_.SetErrorForCert(kCertName2, GRPC_ERROR_CREATE(kRootErrorMessage),
                               absl::nullopt);
  EXPECT_THAT(watcher_state_2->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kRootErrorMessage, "")));
  CancelWatch(watcher_state_2);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       CancelTheLastWatcherOnAnErroredCertInfo) {
  // Register watcher 1.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  // Calling SetErrorForCert on both cert names should only call one OnError
  // on watcher 1.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(
                  ErrorInfo(kRootErrorMessage, kIdentityErrorMessage)));
  // When watcher 1 is removed, the cert info entry should be removed.
  CancelWatch(watcher_state_1);
  // Register watcher 2 on the same cert name.
  WatcherState* watcher_state_2 = MakeWatcher(kCertName1, kCertName1);
  // Should not trigger OnError call on watcher 2 right away.
  EXPECT_THAT(watcher_state_2->GetErrorQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_2);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       WatchErroredCertInfoWithValidCredentialData) {
  // Push credential updates to kCertName1.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Calling SetErrorForCert on both cert names.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  // Register watcher 1.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  // watcher 1 should receive both the old credentials and the error right away.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(
                  ErrorInfo(kRootErrorMessage, kIdentityErrorMessage)));
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateDistributorTest,
       SetErrorForCertThenSuccessfulCredentialUpdates) {
  // Calling SetErrorForCert on both cert names.
  distributor_.SetErrorForCert(kCertName1, GRPC_ERROR_CREATE(kRootErrorMessage),
                               GRPC_ERROR_CREATE(kIdentityErrorMessage));
  // Push credential updates to kCertName1.
  distributor_.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Register watcher 1.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  // watcher 1 should only receive credential updates without any error, because
  // the previous error is wiped out by a successful update.
  EXPECT_THAT(
      watcher_state_1->GetCredentialQueue(),
      ::testing::ElementsAre(CredentialInfo(
          kRootCert1Contents,
          MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents))));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_1);
}

TEST_F(GrpcTlsCertificateDistributorTest, WatchCertInfoThenInvokeSetError) {
  // Register watcher 1.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, kCertName1);
  // Register watcher 2.
  WatcherState* watcher_state_2 = MakeWatcher(kRootCert1Name, absl::nullopt);
  // Register watcher 3.
  WatcherState* watcher_state_3 =
      MakeWatcher(absl::nullopt, kIdentityCert1Name);
  distributor_.SetError(GRPC_ERROR_CREATE(kErrorMessage));
  EXPECT_THAT(watcher_state_1->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kErrorMessage, kErrorMessage)));
  EXPECT_THAT(watcher_state_2->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo(kErrorMessage, "")));
  EXPECT_THAT(watcher_state_3->GetErrorQueue(),
              ::testing::ElementsAre(ErrorInfo("", kErrorMessage)));
  CancelWatch(watcher_state_1);
  CancelWatch(watcher_state_2);
  CancelWatch(watcher_state_3);
}

TEST_F(GrpcTlsCertificateDistributorTest, WatchErroredCertInfoBySetError) {
  // Register watcher 1 watching kCertName1 as root.
  WatcherState* watcher_state_1 = MakeWatcher(kCertName1, absl::nullopt);
  // Register watcher 2 watching kCertName2 as identity.
  WatcherState* watcher_state_2 = MakeWatcher(absl::nullopt, kCertName2);
  // Call SetError and then cancel all watchers.
  distributor_.SetError(GRPC_ERROR_CREATE(kErrorMessage));
  CancelWatch(watcher_state_1);
  CancelWatch(watcher_state_2);
  // Register watcher 3 watching kCertName1 as root and kCertName2 as identity
  // should not get the error updates.
  WatcherState* watcher_state_3 = MakeWatcher(kCertName1, kCertName2);
  EXPECT_THAT(watcher_state_3->GetErrorQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_3);
  // Register watcher 4 watching kCertName2 as root and kCertName1 as identity
  // should not get the error updates.
  WatcherState* watcher_state_4 = MakeWatcher(kCertName2, kCertName1);
  EXPECT_THAT(watcher_state_4->GetErrorQueue(), ::testing::ElementsAre());
  CancelWatch(watcher_state_4);
}

TEST_F(GrpcTlsCertificateDistributorTest, SetErrorForCertInCallback) {
  distributor_.SetWatchStatusCallback([this](std::string cert_name,
                                             bool /*root_being_watched*/,
                                             bool /*identity_being_watched*/) {
    this->distributor_.SetErrorForCert(
        cert_name, GRPC_ERROR_CREATE(kRootErrorMessage),
        GRPC_ERROR_CREATE(kIdentityErrorMessage));
  });
  auto verify_function = [this](std::string cert_name) {
    WatcherState* watcher_state_1 = MakeWatcher(cert_name, cert_name);
    // Check the errors are delivered to watcher 1.
    EXPECT_THAT(watcher_state_1->GetErrorQueue(),
                ::testing::ElementsAre(
                    ErrorInfo(kRootErrorMessage, kIdentityErrorMessage)));
    CancelWatch(watcher_state_1);
  };
  // Start 1000 threads that will register a watcher to a new cert name, verify
  // the key materials being set, and then cancel the watcher, to make sure the
  // lock mechanism in the distributor is safe.
  std::vector<std::thread> threads;
  threads.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    threads.emplace_back(verify_function, std::to_string(i));
  }
  for (auto& th : threads) {
    th.join();
  }
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
