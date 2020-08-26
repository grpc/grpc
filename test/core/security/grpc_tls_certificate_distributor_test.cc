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

// TlsCertificatesTestWatcher is a simple watcher implementation for testing
// purpose.
class TlsCertificatesTestWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
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

  TlsCertificatesTestWatcher() {}
  TlsCertificatesTestWatcher(std::deque<ErrorInfo>* err_queue)
      : err_queue_(err_queue) {}
  TlsCertificatesTestWatcher(bool* is_destroyed) : destroyed(is_destroyed) {}

  ~TlsCertificatesTestWatcher() {
    if (destroyed != nullptr) {
      *destroyed = true;
    }
  }

  void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) {
    root_certs_ = root_certs;
    key_cert_pairs_ = std::move(key_cert_pairs);
  }

  void OnError(grpc_error* root_cert_error, grpc_error* identity_cert_error) {
    if (err_queue_ != nullptr) {
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
        GRPC_ERROR_UNREF(root_cert_error);
      }
      if (identity_cert_error != GRPC_ERROR_NONE) {
        grpc_slice identity_error_slice;
        GPR_ASSERT(grpc_error_get_str(identity_cert_error,
                                      GRPC_ERROR_STR_DESCRIPTION,
                                      &identity_error_slice));
        identity_error_str =
            std::string(grpc_core::StringViewFromSlice(identity_error_slice));
        GRPC_ERROR_UNREF(identity_cert_error);
      }
      (*err_queue_)
          .emplace_back(std::move(root_error_str),
                        std::move(identity_error_str));
    } else {
      GRPC_ERROR_UNREF(root_cert_error);
      GRPC_ERROR_UNREF(identity_cert_error);
    }
  }

  const absl::optional<absl::string_view>& GetRootCerts() {
    return root_certs_;
  }

  const absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>&
  GetKeyCertPairs() {
    return key_cert_pairs_;
  }

 private:
  absl::optional<absl::string_view> root_certs_;
  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
      key_cert_pairs_;
  std::deque<ErrorInfo>* err_queue_ = nullptr;
  bool* destroyed = nullptr;
};

// CallbackStatus contains the parameters in  the watch_status_callback_ of
// the distributor. When a particular callback is invoked, we will push a
// CallbackStatus to a deque, and later check if the status updates are correct.
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

grpc_tls_certificate_distributor::PemKeyCertPairList MakeCertKeyPairs(
    const char* private_key, const char* certs) {
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(private_key);
  ssl_pair->cert_chain = gpr_strdup(certs);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(ssl_pair);
  return pem_key_cert_pairs;
}

TEST(GrpcTlsCertificateDistributorTest, BasicCredentialBehaviors) {
  grpc_tls_certificate_distributor distributor;
  EXPECT_FALSE(distributor.HasRootCerts(kRootCert1Name));
  EXPECT_FALSE(distributor.HasKeyCertPairs(kIdentityCert1Name));
  // After setting the certificates to the corresponding cert names, the
  // distributor should possess the corresponding certs.
  distributor.SetRootCerts(kRootCert1Name, kRootCert1Contents);
  EXPECT_TRUE(distributor.HasRootCerts(kRootCert1Name));
  distributor.SetKeyCertPairs(
      kIdentityCert1Name,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  EXPECT_TRUE(distributor.HasKeyCertPairs(kIdentityCert1Name));
  // Querying a non-existing cert name should return false.
  EXPECT_FALSE(distributor.HasRootCerts(kRootCert2Name));
  EXPECT_FALSE(distributor.HasKeyCertPairs(kIdentityCert2Name));
}

TEST(GrpcTlsCertificateDistributorTest, CredentialUpdatesWithoutCallbacks) {
  grpc_tls_certificate_distributor distributor;
  auto watcher = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_ptr = watcher.get();
  EXPECT_EQ(watcher->GetRootCerts(), absl::nullopt);
  EXPECT_EQ(watcher->GetKeyCertPairs(), absl::nullopt);
  distributor.WatchTlsCertificates(std::move(watcher), kCertName1, kCertName1);
  // SetKeyMaterials should trigger watcher's OnCertificatesChanged method.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  EXPECT_EQ(watcher_ptr->GetRootCerts(), kRootCert1Contents);
  EXPECT_THAT(
      *watcher_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  // SetRootCerts should trigger watcher's OnCertificatesChanged again.
  distributor.SetRootCerts(kCertName1, kRootCert2Contents);
  EXPECT_EQ(watcher_ptr->GetRootCerts(), kRootCert2Contents);
  EXPECT_THAT(
      *watcher_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  // SetKeyCertPairs should trigger watcher's OnCertificatesChanged again.
  distributor.SetKeyCertPairs(
      kCertName1,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  EXPECT_EQ(watcher_ptr->GetRootCerts(), kRootCert2Contents);
  EXPECT_TRUE(watcher_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert2PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert2Contents)))));
  distributor.CancelTlsCertificatesWatch(watcher_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, SameIdentityNameDiffRootName) {
  grpc_tls_certificate_distributor distributor;
  std::deque<CallbackStatus> cb_deque;
  distributor.SetWatchStatusCallback([&cb_deque](std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
    cb_deque.emplace_back(cert_name, root_being_watched,
                          identity_being_watched);
  });
  // Register watcher 1.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kRootCert1Name,
                                   kIdentityCert1Name);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kRootCert1Name, true, false},
                            {kIdentityCert1Name, false, true}}));
  cb_deque.clear();
  // Register watcher 2.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kRootCert2Name,
                                   kIdentityCert1Name);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kRootCert2Name, true, false}}));
  cb_deque.clear();
  // Push credential updates to kRootCert1Name and check if the status works as
  // expected.
  distributor.SetRootCerts(kRootCert1Name, kRootCert1Contents);
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts(), kRootCert1Contents);
  // Push credential updates to kRootCert2Name.
  distributor.SetRootCerts(kRootCert2Name, kRootCert2Contents);
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts(), kRootCert2Contents);
  // Push credential updates to kIdentityCert1Name and check if the status works
  // as expected.
  distributor.SetKeyCertPairs(
      kIdentityCert1Name,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1 and watcher 2.
  EXPECT_TRUE(watcher_1_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_1_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  EXPECT_TRUE(watcher_2_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_2_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kRootCert1Name, false, false}}));
  cb_deque.clear();
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kRootCert2Name, false, false},
                            {kIdentityCert1Name, false, false}}));
  cb_deque.clear();
}

TEST(GrpcTlsCertificateDistributorTest, SameRootNameDiffIdentityName) {
  grpc_tls_certificate_distributor distributor;
  std::deque<CallbackStatus> cb_deque;
  distributor.SetWatchStatusCallback([&cb_deque](std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
    cb_deque.emplace_back(cert_name, root_being_watched,
                          identity_being_watched);
  });
  // Register watcher 1.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kRootCert1Name,
                                   kIdentityCert1Name);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kRootCert1Name, true, false},
                            {kIdentityCert1Name, false, true}}));
  cb_deque.clear();
  // Register watcher 2.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kRootCert1Name,
                                   kIdentityCert2Name);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kIdentityCert2Name, false, true}}));
  cb_deque.clear();
  // Push credential updates to kRootCert1Name and check if the status works as
  // expected.
  distributor.SetRootCerts(kRootCert1Name, kRootCert1Contents);
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts(), kRootCert1Contents);
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts(), kRootCert1Contents);
  // Push credential updates to kIdentityCert1Name.
  distributor.SetKeyCertPairs(
      kIdentityCert1Name,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1 .
  EXPECT_TRUE(watcher_1_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_1_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  // Push credential updates to kIdentityCert2Name.
  distributor.SetKeyCertPairs(
      kIdentityCert2Name,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Check the updates are delivered to watcher 2.
  EXPECT_TRUE(watcher_2_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_2_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert2PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert2Contents)))));
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kIdentityCert1Name, false, false}}));
  cb_deque.clear();
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kRootCert1Name, false, false},
                            {kIdentityCert2Name, false, false}}));
  cb_deque.clear();
}

TEST(GrpcTlsCertificateDistributorTest,
     AddAndCancelFirstWatcherForSameRootAndIdentityCertName) {
  grpc_tls_certificate_distributor distributor;
  std::deque<CallbackStatus> cb_deque;
  distributor.SetWatchStatusCallback([&cb_deque](std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
    cb_deque.emplace_back(cert_name, root_being_watched,
                          identity_being_watched);
  });
  // Register watcher 1 watching kCertName1 for both root and identity certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, true}}));
  cb_deque.clear();
  // Push credential updates to kCertName1 and check if the status works as
  // expected.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts(), kRootCert1Contents);
  EXPECT_TRUE(watcher_1_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_1_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, false, false}}));
  cb_deque.clear();
}

TEST(GrpcTlsCertificateDistributorTest,
     AddAndCancelFirstWatcherForIdentityCertNameWithRootBeingWatched) {
  grpc_tls_certificate_distributor distributor;
  std::deque<CallbackStatus> cb_deque;
  distributor.SetWatchStatusCallback([&cb_deque](std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
    cb_deque.emplace_back(cert_name, root_being_watched,
                          identity_being_watched);
  });
  // Register watcher 1 watching kCertName1 for root certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   absl::nullopt);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, false}}));
  cb_deque.clear();
  // Register watcher 2 watching kCertName1 for identity certs.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), absl::nullopt,
                                   kCertName1);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, true}}));
  cb_deque.clear();
  // Push credential updates to kCertName1 and check if the status works as
  // expected.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts(), kRootCert1Contents);
  EXPECT_FALSE(watcher_1_ptr->GetKeyCertPairs().has_value());
  // Check the updates are delivered to watcher 2.
  EXPECT_FALSE(watcher_2_ptr->GetRootCerts().has_value());
  EXPECT_TRUE(watcher_2_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_2_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, false}}));
  cb_deque.clear();
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, false, false}}));
  cb_deque.clear();
}

TEST(GrpcTlsCertificateDistributorTest,
     AddAndCancelFirstWatcherForRootCertNameWithIdentityBeingWatched) {
  grpc_tls_certificate_distributor distributor;
  std::deque<CallbackStatus> cb_deque;
  distributor.SetWatchStatusCallback([&cb_deque](std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
    cb_deque.emplace_back(cert_name, root_being_watched,
                          identity_being_watched);
  });
  // Register watcher 1 watching kCertName1 for identity certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), absl::nullopt,
                                   kCertName1);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, false, true}}));
  cb_deque.clear();
  // Register watcher 2 watching kCertName1 for root certs.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kCertName1,
                                   absl::nullopt);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, true}}));
  cb_deque.clear();
  // Push credential updates to kCertName1 and check if the status works as
  // expected.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Check the updates are delivered to watcher 1.
  EXPECT_FALSE(watcher_1_ptr->GetRootCerts().has_value());
  EXPECT_TRUE(watcher_1_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_1_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts(), kRootCert1Contents);
  EXPECT_FALSE(watcher_2_ptr->GetKeyCertPairs().has_value());
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, false, true}}));
  cb_deque.clear();
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, false, false}}));
  cb_deque.clear();
}

TEST(GrpcTlsCertificateDistributorTest,
     RemoveAllWatchersForCertNameAndAddAgain) {
  grpc_tls_certificate_distributor distributor;
  std::deque<CallbackStatus> cb_deque;
  distributor.SetWatchStatusCallback([&cb_deque](std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
    cb_deque.emplace_back(cert_name, root_being_watched,
                          identity_being_watched);
  });
  // Register watcher 1 and watcher 2 watching kCertName1 for root and identity
  // certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, true}}));
  cb_deque.clear();
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kCertName1,
                                   kCertName1);
  EXPECT_THAT(cb_deque,
              testing::ElementsAreArray(std::vector<CallbackStatus>{}));
  cb_deque.clear();
  // Push credential updates to kCertName1.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  EXPECT_THAT(cb_deque,
              testing::ElementsAreArray(std::vector<CallbackStatus>{}));
  cb_deque.clear();
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, false, false}}));
  cb_deque.clear();
  // Register watcher 3 watching kCertName for root and identity certs.
  auto watcher_3 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), kCertName1,
                                   kCertName1);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, true}}));
  cb_deque.clear();
  // Push credential updates to kCertName1.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert2Contents,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Check the updates are delivered to watcher 3.
  EXPECT_EQ(watcher_3_ptr->GetRootCerts(), kRootCert2Contents);
  EXPECT_TRUE(watcher_3_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_3_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert2PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert2Contents)))));
  // Cancel watcher 3.
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, false, false}}));
  cb_deque.clear();
}

TEST(GrpcTlsCertificateDistributorTest, ResetCallbackToNull) {
  grpc_tls_certificate_distributor distributor;
  std::deque<CallbackStatus> cb_deque;
  distributor.SetWatchStatusCallback([&cb_deque](std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
    cb_deque.emplace_back(cert_name, root_being_watched,
                          identity_being_watched);
  });
  // Register watcher 1 watching kCertName1 for root and identity certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  EXPECT_THAT(cb_deque, testing::ElementsAreArray(std::vector<CallbackStatus>{
                            {kCertName1, true, true}}));
  cb_deque.clear();
  // Reset callback to nullptr.
  distributor.SetWatchStatusCallback(nullptr);
  // Cancel watcher 1 shouldn't trigger any callback.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  EXPECT_THAT(cb_deque,
              testing::ElementsAreArray(std::vector<CallbackStatus>{}));
  cb_deque.clear();
}

TEST(GrpcTlsCertificateDistributorTest, SetKeyMaterialsInCallback) {
  grpc_tls_certificate_distributor distributor;
  distributor.SetWatchStatusCallback(
      [&distributor](std::string cert_name, bool root_being_watched,
                     bool identity_being_watched) {
        distributor.SetKeyMaterials(
            cert_name, kRootCert1Contents,
            MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
      });
  auto verify_function = [&distributor](std::string cert_name) {
    auto watcher = absl::make_unique<TlsCertificatesTestWatcher>();
    TlsCertificatesTestWatcher* watcher_ptr = watcher.get();
    distributor.WatchTlsCertificates(std::move(watcher), cert_name, cert_name);
    // Check the updates are delivered to watcher 1.
    EXPECT_EQ(watcher_ptr->GetRootCerts(), kRootCert1Contents);
    EXPECT_TRUE(watcher_ptr->GetKeyCertPairs().has_value());
    EXPECT_THAT(
        *watcher_ptr->GetKeyCertPairs(),
        ::testing::ElementsAre(::testing::AllOf(
            ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                                ::testing::StrEq(kIdentityCert1PrivateKey)),
            ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                                ::testing::StrEq(kIdentityCert1Contents)))));
    distributor.CancelTlsCertificatesWatch(watcher_ptr);
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

TEST(GrpcTlsCertificateDistributorTest, WatchACertInfoWithValidCredentials) {
  grpc_tls_certificate_distributor distributor;
  // Push credential updates to kCertName1.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Push root credential updates to kCertName2.
  distributor.SetRootCerts(kRootCert2Name, kRootCert2Contents);
  // Push identity credential updates to kCertName2.
  distributor.SetKeyCertPairs(
      kIdentityCert2Name,
      MakeCertKeyPairs(kIdentityCert2PrivateKey, kIdentityCert2Contents));
  // Register watcher 1.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  // watcher 1 should receive the credentials right away.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts(), kRootCert1Contents);
  EXPECT_TRUE(watcher_1_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_1_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  // Register watcher 2.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kRootCert2Name,
                                   absl::nullopt);
  // watcher 2 should receive the root credentials right away.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts(), kRootCert2Contents);
  EXPECT_FALSE(watcher_2_ptr->GetKeyCertPairs().has_value());
  // Register watcher 3.
  auto watcher_3 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), absl::nullopt,
                                   kIdentityCert2Name);
  // watcher 3 should received the identity credentials right away.
  EXPECT_FALSE(watcher_3_ptr->GetRootCerts().has_value());
  EXPECT_TRUE(watcher_3_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_3_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert2PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert2Contents)))));
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, SetErrorForCertForBothRootAndIdentity) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  // Calling SetErrorForCert on both cert names should only call one OnError
  // on watcher 1.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  // Calling SetErrorForCert on both cert names again should call OnError
  // on watcher 1 again.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, SetErrorForCertForRootOrIdentity) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   absl::nullopt);
  // Calling SetErrorForCert on root name should only call one OnError
  // on watcher 1.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_NONE);
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, ""}}));
  watcher_1_err_deque.clear();
  // Calling SetErrorForCert on identity name should do nothing.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_NONE,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  // Register watcher 2.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_2_err_deque;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_deque);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), absl::nullopt,
                                   kCertName1);
  // Calling SetErrorForCert on identity name should only call one OnError
  // on watcher 2.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_NONE,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {"", kIdentityErrorMessage}}));
  watcher_2_err_deque.clear();
  // Calling SetErrorForCert on root name should do nothing.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_NONE);
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  watcher_2_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
}

TEST(GrpcTlsCertificateDistributorTest,
     SetErrorForCertForIdentityNameWithSameNameForRootErrored) {
  grpc_tls_certificate_distributor distributor;
  // SetErrorForCert for kCertName1.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  // Register watcher 1 for kCertName1 as root and kCertName2 as identity.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName2);
  // Should trigger OnError call right away since kCertName1 has error.
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, ""}}));
  watcher_1_err_deque.clear();
  //   Calling SetErrorForCert on kCertName2 should trigger OnError with both
  //   errors, because kCertName1 also has error.
  distributor.SetErrorForCert(
      kCertName2, GRPC_ERROR_NONE,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
}

TEST(GrpcTlsCertificateDistributorTest,
     SetErrorForCertForRootNameWithSameNameForIdentityErrored) {
  grpc_tls_certificate_distributor distributor;
  // SetErrorForCert for kCertName1.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  // Register watcher 1 for kCertName2 as root and kCertName1 as identity.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName2,
                                   kCertName1);
  // Should trigger OnError call right away since kCertName2 has error.
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {"", kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  // Calling SetErrorForCert on kCertName2 should trigger OnError with both
  // errors, because kCertName1 also has error.
  distributor.SetErrorForCert(
      kCertName2, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_NONE);
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
}

TEST(GrpcTlsCertificateDistributorTest,
     SetErrorForCertForIdentityNameWithSameNameForRootNoError) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1 for kCertName1 as root and kCertName2 as identity.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName2);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor.SetErrorForCert(
      kCertName2, GRPC_ERROR_NONE,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {"", kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  // Register watcher 2 for kCertName2 as identity and a non-existing name
  // kRootCert1Name as root.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_2_err_deque;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_deque);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kRootCert1Name,
                                   kCertName2);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor.SetErrorForCert(
      kCertName2, GRPC_ERROR_NONE,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {"", kIdentityErrorMessage}}));
  watcher_2_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
}

TEST(GrpcTlsCertificateDistributorTest,
     SetErrorForCertForRootNameWithSameNameForIdentityNoError) {
  grpc_tls_certificate_distributor distributor;
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName2,
                                   kCertName1);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor.SetErrorForCert(
      kCertName2, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_NONE);
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, ""}}));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  // Register watcher 2 for kCertName2 as root and a non-existing name
  // kIdentityCert1Name as identity.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_2_err_deque;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_deque);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kCertName2,
                                   kIdentityCert1Name);
  // Should not trigger OnError.
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  // Calling SetErrorForCert on kCertName2 should trigger OnError.
  distributor.SetErrorForCert(
      kCertName2, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_NONE);
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, ""}}));
  watcher_2_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
}

TEST(GrpcTlsCertificateDistributorTest,
     CancelTheLastWatcherOnAnErroredCertInfo) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  // Calling SetErrorForCert on both cert names should only call one OnError
  // on watcher 1.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  // When watcher 1 is removed, the cert info entry should be removed.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  // Register watcher 2 on the same cert name.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_2_err_deque;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_deque);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kCertName1,
                                   kCertName1);
  // Should not trigger OnError call on watcher 2 right away.
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  watcher_2_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
}

TEST(GrpcTlsCertificateDistributorTest,
     WatchErroredCertInfoWithValidCredentialData) {
  grpc_tls_certificate_distributor distributor;
  // Push credential updates to kCertName1.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Calling SetErrorForCert on both cert names.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  // Register watcher 1.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  // watcher 1 should receive both the old credentials and the error right away.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts(), kRootCert1Contents);
  EXPECT_TRUE(watcher_1_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_1_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kRootErrorMessage, kIdentityErrorMessage}}));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
}

TEST(GrpcTlsCertificateDistributorTest,
     SetErrorForCertThenSuccessfulCredentialUpdates) {
  grpc_tls_certificate_distributor distributor;
  // Calling SetErrorForCert on both cert names.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  // Push credential updates to kCertName1.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Register watcher 1.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  // watcher 1 should only receive credential updates without any error, because
  // the previous error is wiped out by a successful update.
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  EXPECT_EQ(watcher_1_ptr->GetRootCerts(), kRootCert1Contents);
  EXPECT_TRUE(watcher_1_ptr->GetKeyCertPairs().has_value());
  EXPECT_THAT(
      *watcher_1_ptr->GetKeyCertPairs(),
      ::testing::ElementsAre(::testing::AllOf(
          ::testing::Property(&grpc_core::PemKeyCertPair::private_key,
                              ::testing::StrEq(kIdentityCert1PrivateKey)),
          ::testing::Property(&grpc_core::PemKeyCertPair::cert_chain,
                              ::testing::StrEq(kIdentityCert1Contents)))));
  watcher_1_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, WatchCertInfoThenInvokeSetError) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  // Register watcher 2.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_2_err_deque;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_deque);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kRootCert1Name,
                                   absl::nullopt);
  // Register watcher 3.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_3_err_deque;
  auto watcher_3 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_3_err_deque);
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), absl::nullopt,
                                   kIdentityCert1Name);
  distributor.SetError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage));
  EXPECT_THAT(watcher_1_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kErrorMessage, kErrorMessage}}));
  watcher_1_err_deque.clear();
  EXPECT_THAT(watcher_2_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {kErrorMessage, ""}}));
  watcher_2_err_deque.clear();
  EXPECT_THAT(watcher_3_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{
                      {"", kErrorMessage}}));
  watcher_3_err_deque.clear();
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, WatchErroredCertInfoBySetError) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1 watching kCertName1 as root.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_1_err_deque;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_deque);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   absl::nullopt);
  // Register watcher 2 watching kCertName2 as identity.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_2_err_deque;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_deque);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), absl::nullopt,
                                   kCertName2);
  // Call SetError and then cancel all watchers.
  distributor.SetError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage));
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  // Register watcher 3 watching kCertName1 as root and kCertName2 as identity
  // should not get the error updates.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_3_err_deque;
  auto watcher_3 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_3_err_deque);
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), kCertName1,
                                   kCertName2);
  EXPECT_THAT(watcher_3_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
  // Register watcher 4 watching kCertName2 as root and kCertName1 as identity
  // should not get the error updates.
  std::deque<TlsCertificatesTestWatcher::ErrorInfo> watcher_4_err_deque;
  auto watcher_4 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_4_err_deque);
  TlsCertificatesTestWatcher* watcher_4_ptr = watcher_4.get();
  distributor.WatchTlsCertificates(std::move(watcher_4), kCertName2,
                                   kCertName1);
  EXPECT_THAT(watcher_4_err_deque,
              testing::ElementsAreArray(
                  std::vector<TlsCertificatesTestWatcher::ErrorInfo>{}));
  distributor.CancelTlsCertificatesWatch(watcher_4_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, WatcherDestroyedAfterCancelled) {
  grpc_tls_certificate_distributor distributor;
  // Push credential updates to kCertName1.
  distributor.SetKeyMaterials(
      kCertName1, kRootCert1Contents,
      MakeCertKeyPairs(kIdentityCert1PrivateKey, kIdentityCert1Contents));
  // Calling SetErrorForCert on both cert names.
  distributor.SetErrorForCert(
      kCertName1, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kRootErrorMessage),
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kIdentityErrorMessage));
  // Register watcher 1.
  bool destroyed = false;
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>(&destroyed);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName1,
                                   kCertName1);
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  // Cancel watcher 1 should make destroyed to true.
  EXPECT_TRUE(destroyed);
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
