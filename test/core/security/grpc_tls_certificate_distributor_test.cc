/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"

#include <gmock/gmock.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <queue>
#include <thread>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace testing {

constexpr const char* kCertName = "cert_name";
constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kRootCert = "root_certificate_contents";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kIdentityPrivateKey = "identity_private_key_contents";
constexpr const char* kIdentityCert = "identity_certificate_contents";
constexpr const char* kAnotherRootCertName = "another_root_cert_name";
constexpr const char* kAnotherRootCert = "another_root_certificate_contents";
constexpr const char* kAnotherIdentityCertName = "another_identity_cert_name";
constexpr const char* kAnotherIdentityPrivateKey =
    "another_identity_private_key_contents";
constexpr const char* kAnotherIdentityCert =
    "another_identity_certificate_contents";
constexpr const char* kErrorMessage = "error_message";

// TlsCertificatesTestWatcher is a simple watcher implementation for testing
// purpose.
class TlsCertificatesTestWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  TlsCertificatesTestWatcher() {}
  TlsCertificatesTestWatcher(std::queue<std::string>* err_queue)
      : err_queue_(err_queue) {}

  ~TlsCertificatesTestWatcher() {}

  void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) {
    root_certs_ = root_certs;
    key_cert_pairs_ = std::move(key_cert_pairs);
  }

  void OnError(grpc_error* error) {
    ASSERT_NE(error, GRPC_ERROR_NONE);
    GPR_ASSERT(err_queue_ != nullptr);
    grpc_slice str;
    GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &str));
    (*err_queue_).push(std::string(grpc_core::StringViewFromSlice(str)));
    GRPC_ERROR_UNREF(error);
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
  std::queue<std::string>* err_queue_;
};

// CallbackStatus contains the parameters in  the watch_status_callback_ of
// the distributor. When a particular callback is invoked, we will push a
// CallbackStatus to a queue, and later check if the status updates are correct.
struct CallbackStatus {
  std::string cert_name;
  bool root_being_watched;
  bool identity_being_watched;
  CallbackStatus(std::string name, bool root_watched, bool identity_watched)
      : cert_name(std::move(name)),
        root_being_watched(root_watched),
        identity_being_watched(identity_watched) {}
};

// A helper function to check if the watch_status_callback_ field of the
// distributor is invoked as expected.
void VerifyCallbackStatusQueue(
    std::queue<CallbackStatus>& queue,
    const std::vector<CallbackStatus>& expected_status_list) {
  EXPECT_EQ(queue.size(), expected_status_list.size());
  int index = 0;
  while (queue.size() > 0) {
    auto status = queue.front();
    EXPECT_STREQ(status.cert_name.c_str(),
                 expected_status_list.at(index).cert_name.c_str());
    EXPECT_EQ(status.root_being_watched,
              expected_status_list.at(index).root_being_watched);
    EXPECT_EQ(status.identity_being_watched,
              expected_status_list.at(index).identity_being_watched);
    queue.pop();
    index++;
  }
}

// A helper function to check if the SendErrorToWatchers function of the watcher
// is invoked as expected.
void VerifyErrorQueue(std::queue<std::string>& err_queue,
                      const std::vector<std::string>& expected_status_list) {
  EXPECT_EQ(err_queue.size(), expected_status_list.size());
  int index = 0;
  while (err_queue.size() > 0) {
    std::string& err_msg = err_queue.front();
    EXPECT_EQ(err_msg, kErrorMessage);
    err_queue.pop();
    index++;
  }
}

TEST(GrpcTlsCertificateDistributorTest, BasicCredentialBehaviors) {
  grpc_tls_certificate_distributor distributor;
  EXPECT_EQ(distributor.HasRootCerts(kRootCertName), false);
  EXPECT_EQ(distributor.HasKeyCertPairs(kIdentityCertName), false);
  // After setting the certificates to the corresponding cert names, the
  // distributor should possess the corresponding certs.
  distributor.SetRootCerts(kRootCertName, kRootCert);
  EXPECT_EQ(distributor.HasRootCerts(kRootCertName), true);
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyCertPairs(kIdentityCertName, pem_key_cert_pairs);
  EXPECT_EQ(distributor.HasKeyCertPairs(kIdentityCertName), true);
  // Querying a non-existing cert name should return false.
  EXPECT_EQ(distributor.HasRootCerts(kAnotherRootCertName), false);
  EXPECT_EQ(distributor.HasKeyCertPairs(kAnotherIdentityCertName), false);
}

TEST(GrpcTlsCertificateDistributorTest, CredentialUpdatesWithoutCallbacks) {
  grpc_tls_certificate_distributor distributor;
  auto watcher = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_ptr = watcher.get();
  EXPECT_EQ(watcher->GetRootCerts(), absl::nullopt);
  EXPECT_EQ(watcher->GetKeyCertPairs(), absl::nullopt);
  distributor.WatchTlsCertificates(std::move(watcher), kCertName, kCertName);
  // SetKeyMaterials should trigger watcher's OnCertificatesChanged method.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyMaterials(kCertName, absl::string_view(kRootCert),
                              pem_key_cert_pairs);
  EXPECT_EQ(watcher_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_ptr->GetRootCerts()), kRootCert);
  EXPECT_EQ(watcher_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // SetRootCerts should trigger watcher's OnCertificatesChanged again.
  distributor.SetRootCerts(kCertName, kAnotherRootCert);
  EXPECT_EQ(watcher_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_ptr->GetRootCerts()), kAnotherRootCert);
  cert_pair_list = *watcher_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // SetKeyCertPairs should trigger watcher's OnCertificatesChanged again.
  grpc_ssl_pem_key_cert_pair* another_ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  another_ssl_pair->private_key = gpr_strdup(kAnotherIdentityPrivateKey);
  another_ssl_pair->cert_chain = gpr_strdup(kAnotherIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList
      another_pem_key_cert_pairs;
  another_pem_key_cert_pairs.emplace_back(
      grpc_core::PemKeyCertPair(another_ssl_pair));
  distributor.SetKeyCertPairs(kCertName, another_pem_key_cert_pairs);
  EXPECT_EQ(watcher_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_ptr->GetRootCerts()), kAnotherRootCert);
  cert_pair_list = *watcher_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kAnotherIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kAnotherIdentityCert);
  distributor.CancelTlsCertificatesWatch(watcher_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, SameIdentityNameDiffRootName) {
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });
  // Register watcher 1.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kRootCertName,
                                   kIdentityCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kRootCertName, true, false},
                                         {kIdentityCertName, false, true}});
  // Register watcher 2.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kAnotherRootCertName,
                                   kIdentityCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kAnotherRootCertName, true, false}});
  // Push credential updates to kRootCertName and check if the status works as
  // expected.
  distributor.SetRootCerts(kRootCertName, absl::string_view(kRootCert));
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_1_ptr->GetRootCerts()), kRootCert);
  // Push credential updates to kAnotherRootCertName.
  distributor.SetRootCerts(kAnotherRootCertName,
                           absl::string_view(kAnotherRootCert));
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_2_ptr->GetRootCerts()), kAnotherRootCert);
  // Push credential updates to kIdentityCertName and check if the status works
  // as expected.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyCertPairs(kIdentityCertName, pem_key_cert_pairs);
  // Check the updates are delivered to watcher 1 and watcher 2.
  EXPECT_EQ(watcher_1_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_1_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  EXPECT_EQ(watcher_2_ptr->GetKeyCertPairs().has_value(), true);
  cert_pair_list = *watcher_2_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kRootCertName, false, false}});
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kAnotherRootCertName, false, false},
                                         {kIdentityCertName, false, false}});
}

TEST(GrpcTlsCertificateDistributorTest, SameRootNameDiffIdentityName) {
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });
  // Register watcher 1.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kRootCertName,
                                   kIdentityCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kRootCertName, true, false},
                                         {kIdentityCertName, false, true}});
  // Register watcher 2.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kRootCertName,
                                   kAnotherIdentityCertName);
  VerifyCallbackStatusQueue(
      queue,
      std::vector<CallbackStatus>{{kAnotherIdentityCertName, false, true}});
  // Push credential updates to kRootCertName and check if the status works as
  // expected.
  distributor.SetRootCerts(kRootCertName, absl::string_view(kRootCert));
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_1_ptr->GetRootCerts()), kRootCert);
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_2_ptr->GetRootCerts()), kRootCert);
  // Push credential updates to kIdentityCertName.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyCertPairs(kIdentityCertName, pem_key_cert_pairs);
  // Check the updates are delivered to watcher 1 .
  EXPECT_EQ(watcher_1_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_1_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // Push credential updates to kAnotherIdentityCertName.
  grpc_ssl_pem_key_cert_pair* another_ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  another_ssl_pair->private_key = gpr_strdup(kAnotherIdentityPrivateKey);
  another_ssl_pair->cert_chain = gpr_strdup(kAnotherIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList
      another_pem_key_cert_pairs;
  another_pem_key_cert_pairs.emplace_back(
      grpc_core::PemKeyCertPair(another_ssl_pair));
  distributor.SetKeyCertPairs(kAnotherIdentityCertName,
                              another_pem_key_cert_pairs);
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetKeyCertPairs().has_value(), true);
  cert_pair_list = *watcher_2_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kAnotherIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kAnotherIdentityCert);
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kIdentityCertName, false, false}});
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  VerifyCallbackStatusQueue(
      queue,
      std::vector<CallbackStatus>{{kRootCertName, false, false},
                                  {kAnotherIdentityCertName, false, false}});
}

TEST(GrpcTlsCertificateDistributorTest,
     AddAndCancelFirstWatcherForSameRootAndIdentityCertName) {
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });
  // Register watcher 1 watching kCertName for both root and identity certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName, kCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, true}});
  // Push credential updates to kCertName and check if the status works as
  // expected.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyMaterials(kCertName, kRootCert, pem_key_cert_pairs);
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_1_ptr->GetRootCerts()), kRootCert);
  EXPECT_EQ(watcher_1_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_1_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, false, false}});
}

TEST(GrpcTlsCertificateDistributorTest,
     AddAndCancelFirstWatcherForIdentityCertNameWithRootBeingWatched) {
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });
  // Register watcher 1 watching kCertName for root certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName,
                                   absl::nullopt);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, false}});
  // Register watcher 2 watching kCertName for identity certs.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), absl::nullopt,
                                   kCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, true}});
  // Push credential updates to kCertName and check if the status works as
  // expected.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyMaterials(kCertName, kRootCert, pem_key_cert_pairs);
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_1_ptr->GetRootCerts()), kRootCert);
  EXPECT_EQ(watcher_1_ptr->GetKeyCertPairs().has_value(), false);
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts().has_value(), false);
  EXPECT_EQ(watcher_2_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_2_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, false}});
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, false, false}});
}

TEST(
    GrpcTlsCertificateDistributorTest,
    AddAndCancelFirstWatcherForRootAndIdentityCertNameWithIdentityBeingWatched) {
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });
  // Register watcher 1 watching kCertName for identity certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), absl::nullopt,
                                   kCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, false, true}});
  // Register watcher 2 watching kCertName for root and identity certs.
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kCertName, kCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, true}});
  // Push credential updates to kCertName and check if the status works as
  // expected.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyMaterials(kCertName, kRootCert, pem_key_cert_pairs);
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts().has_value(), false);
  EXPECT_EQ(watcher_1_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_1_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_2_ptr->GetRootCerts()), kRootCert);
  EXPECT_EQ(watcher_2_ptr->GetKeyCertPairs().has_value(), true);
  cert_pair_list = *watcher_2_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, false, true}});
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, false, false}});
}

TEST(GrpcTlsCertificateDistributorTest,
     RemoveAllWatchersForCertNameAndAddAgain) {
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });
  // Register watcher 1 and watcher 2 watching kCertName for root and identity
  // certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName, kCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, true}});
  auto watcher_2 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kCertName, kCertName);
  VerifyCallbackStatusQueue(queue, std::vector<CallbackStatus>{});
  // Push credential updates to kCertName.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
  ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyMaterials(kCertName, kRootCert, pem_key_cert_pairs);
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  VerifyCallbackStatusQueue(queue, std::vector<CallbackStatus>{});
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, false, false}});
  // Register watcher 3 watching kCertName for root and identity certs.
  auto watcher_3 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), kCertName, kCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, true}});
  // Push credential updates to kCertName.
  grpc_ssl_pem_key_cert_pair* another_ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  another_ssl_pair->private_key = gpr_strdup(kAnotherIdentityPrivateKey);
  another_ssl_pair->cert_chain = gpr_strdup(kAnotherIdentityCert);
  grpc_tls_certificate_distributor::PemKeyCertPairList
      another_pem_key_cert_pairs;
  another_pem_key_cert_pairs.emplace_back(
      grpc_core::PemKeyCertPair(another_ssl_pair));
  distributor.SetKeyMaterials(kCertName, kAnotherRootCert,
                              another_pem_key_cert_pairs);
  // Check the updates are delivered to watcher 3.
  EXPECT_EQ(watcher_3_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(std::string(*watcher_3_ptr->GetRootCerts()), kAnotherRootCert);
  EXPECT_EQ(watcher_3_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_3_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(), kAnotherIdentityPrivateKey);
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), kAnotherIdentityCert);
  // Cancel watcher 3.
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, false, false}});
}

TEST(GrpcTlsCertificateDistributorTest, ResetCallbackToNull) {
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });
  // Register watcher 1 watching kCertName for root and identity certs.
  auto watcher_1 = absl::make_unique<TlsCertificatesTestWatcher>();
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName, kCertName);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{kCertName, true, true}});
  // Reset callback to nullptr.
  distributor.SetWatchStatusCallback(nullptr);
  // Cancel watcher 1 shouldn't trigger any callback.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(queue, std::vector<CallbackStatus>{});
}

TEST(GrpcTlsCertificateDistributorTest, SendErrorToWatchersForOneCertName) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1.
  std::queue<std::string> watcher_1_err_queue;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_queue);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName, kCertName);
  // Calling SendErrorToWatchers on both cert names should only call one OnError
  // on watcher 1.
  distributor.SendErrorToWatchers(
      kCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage), true,
      true);
  VerifyErrorQueue(watcher_1_err_queue,
                   std::vector<std::string>{kErrorMessage});
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  // Register watcher 2.
  std::queue<std::string> watcher_2_err_queue;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_queue);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kCertName,
                                   absl::nullopt);
  // Calling SendErrorToWatchers on root name should only call one OnError on
  // watcher 2.
  distributor.SendErrorToWatchers(
      kCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage), true,
      false);
  VerifyErrorQueue(watcher_2_err_queue,
                   std::vector<std::string>{kErrorMessage});
  // Calling SendErrorToWatchers on identity name shouldn't call OnError on
  // watcher 2.
  distributor.SendErrorToWatchers(
      kCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage), false,
      true);
  VerifyErrorQueue(watcher_2_err_queue, std::vector<std::string>{});
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  // Register watcher 3.
  std::queue<std::string> watcher_3_err_queue;
  auto watcher_3 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_3_err_queue);
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), absl::nullopt,
                                   kCertName);
  // Calling SendErrorToWatchers on root name should only call OnError on
  // watcher 3.
  distributor.SendErrorToWatchers(
      kCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage), true,
      false);
  VerifyErrorQueue(watcher_3_err_queue, std::vector<std::string>{});
  // Calling SendErrorToWatchers on identity name should only call one OnError
  // on watcher 3.
  distributor.SendErrorToWatchers(
      kCertName, GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage), false,
      true);
  VerifyErrorQueue(watcher_3_err_queue,
                   std::vector<std::string>{kErrorMessage});
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, SendErrorToAllWatchers) {
  grpc_tls_certificate_distributor distributor;
  // Register watcher 1.
  std::queue<std::string> watcher_1_err_queue;
  auto watcher_1 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_1_err_queue);
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1), kCertName, kCertName);
  // Register watcher 2.
  std::queue<std::string> watcher_2_err_queue;
  auto watcher_2 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_2_err_queue);
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2), kRootCertName,
                                   absl::nullopt);
  // Register watcher 3.
  std::queue<std::string> watcher_3_err_queue;
  auto watcher_3 =
      absl::make_unique<TlsCertificatesTestWatcher>(&watcher_3_err_queue);
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), absl::nullopt,
                                   kIdentityCertName);
  // Calling SendErrorToWatchers to all watchers.
  distributor.SendErrorToWatchers(
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(kErrorMessage));
  VerifyErrorQueue(watcher_1_err_queue,
                   std::vector<std::string>{kErrorMessage});
  VerifyErrorQueue(watcher_2_err_queue,
                   std::vector<std::string>{kErrorMessage});
  VerifyErrorQueue(watcher_3_err_queue,
                   std::vector<std::string>{kErrorMessage});
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
}

TEST(GrpcTlsCertificateDistributorTest, SetKeyMaterialsInCallback) {
  grpc_tls_certificate_distributor distributor;
  distributor.SetWatchStatusCallback(
      [&distributor](std::string cert_name, bool root_being_watched,
                     bool identity_being_watched) {
        grpc_ssl_pem_key_cert_pair* ssl_pair =
            static_cast<grpc_ssl_pem_key_cert_pair*>(
                gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
        ssl_pair->private_key = gpr_strdup(kIdentityPrivateKey);
        ssl_pair->cert_chain = gpr_strdup(kIdentityCert);
        grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
        pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
        distributor.SetKeyMaterials(cert_name, absl::string_view(kRootCert),
                                    pem_key_cert_pairs);
      });
  auto verify_function = [&distributor](std::string cert_name) {
    auto watcher = absl::make_unique<TlsCertificatesTestWatcher>();
    TlsCertificatesTestWatcher* watcher_ptr = watcher.get();
    distributor.WatchTlsCertificates(std::move(watcher), cert_name, cert_name);
    // Check the updates are delivered to watcher 1.
    EXPECT_EQ(watcher_ptr->GetRootCerts().has_value(), true);
    EXPECT_EQ(std::string(*watcher_ptr->GetRootCerts()), kRootCert);
    EXPECT_EQ(watcher_ptr->GetKeyCertPairs().has_value(), true);
    auto cert_pair_list = *watcher_ptr->GetKeyCertPairs();
    EXPECT_EQ(cert_pair_list.size(), 1);
    EXPECT_STREQ(cert_pair_list[0].private_key(), kIdentityPrivateKey);
    EXPECT_STREQ(cert_pair_list[0].cert_chain(), kIdentityCert);
    distributor.CancelTlsCertificatesWatch(watcher_ptr);
  };
  // Start 1000 threads that will register a watcher to a new cert name, verify
  // the key materials being set, and then cancel the watcher, to make sure the
  // lock mechanism in the distributor is safe.
  std::vector<std::thread> threads;
  threads.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    threads.emplace_back(std::thread(verify_function, std::to_string(i)));
  }
  for (auto& th : threads) {
    th.join();
  }
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
