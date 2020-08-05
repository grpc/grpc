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

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace testing {
// TlsCertificatesTestWatcher is a simple watcher implementation for testing
// purpose.
class TlsCertificatesTestWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  TlsCertificatesTestWatcher(std::queue<std::string>& err_queue)
      : err_queue_(err_queue) {}

  ~TlsCertificatesTestWatcher() {}

  void OnCertificatesChanged(
      absl::optional<absl::string_view> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) {
    this->root_certs_ = root_certs;
    this->key_cert_pairs_ = key_cert_pairs;
  }

  void OnError(grpc_error* error) {
    ASSERT_NE(error, GRPC_ERROR_NONE);
    grpc_slice str;
    GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &str));
    err_queue_.push(std::string(grpc_core::StringViewFromSlice(str)));
    GRPC_ERROR_UNREF(error);
  }

  const absl::optional<absl::string_view>& GetRootCerts() {
    return this->root_certs_;
  }

  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
  GetKeyCertPairs() {
    return this->key_cert_pairs_;
  }

 private:
  absl::optional<absl::string_view> root_certs_;
  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
      key_cert_pairs_;
  std::queue<std::string>& err_queue_;
};

// CallbackStatus contains the parameters in  the watch_status_callback_ of
// the distributor. When a particular callback is invoked, we will push a
// CallbackStatus to a queue, and later check if the status updates are correct.
struct CallbackStatus {
  std::string cert_name;
  bool root_being_watched = false;
  bool identity_being_watched = false;
  CallbackStatus(std::string name, bool root_watched, bool identity_watched)
      : cert_name(name),
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

TEST(GrpcTlsCertificateDistributorTest, BasicCredentialBehaviors) {
  grpc_tls_certificate_distributor distributor;
  // Setting absl::nullopt to both cert names shouldn't have any side effect.
  distributor.SetKeyMaterials("root_cert_name", absl::nullopt,
                              "identity_cert_name", absl::nullopt);
  EXPECT_EQ(distributor.HasRootCerts("root_cert_name"), false);
  EXPECT_EQ(distributor.HasKeyCertPairs("identity_cert_name"), false);

  // After setting the certificates to the corresponding cert names, the
  // distributor should possess the corresponding certs.
  distributor.SetRootCerts("root_cert_name", "root_certificate_contents");
  EXPECT_EQ(distributor.HasRootCerts("root_cert_name"), true);

  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup("identity_private_key_contents");
  ssl_pair->cert_chain = gpr_strdup("identity_certificate_contents");
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyCertPairs("identity_cert_name", pem_key_cert_pairs);
  EXPECT_EQ(distributor.HasKeyCertPairs("identity_cert_name"), true);
  // Querying a non-existing cert name should return false.
  EXPECT_EQ(distributor.HasRootCerts("other_root_cert_name"), false);
  EXPECT_EQ(distributor.HasKeyCertPairs("other_identity_cert_name"), false);
}

TEST(GrpcTlsCertificateDistributorTest, CredentialUpdates) {
  std::queue<std::string> err_queue;
  grpc_tls_certificate_distributor distributor;
  std::unique_ptr<TlsCertificatesTestWatcher> watcher =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_ptr = watcher.get();
  EXPECT_EQ(watcher->GetRootCerts(), absl::nullopt);
  EXPECT_EQ(watcher->GetKeyCertPairs(), absl::nullopt);
  EXPECT_EQ(err_queue.size(), 0);
  distributor.WatchTlsCertificates(std::move(watcher),
                                   absl::make_optional("root_cert_name"),
                                   absl::make_optional("identity_cert_name"));

  // SetKeyMaterials should trigger watcher's OnCertificatesChanged method.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup("identity_private_key_contents");
  ssl_pair->cert_chain = gpr_strdup("identity_certificate_contents");
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  distributor.SetKeyMaterials(
      "root_cert_name",
      absl::make_optional(absl::string_view("root_certificate_contents")),
      "identity_cert_name", absl::make_optional(pem_key_cert_pairs));
  EXPECT_EQ(watcher_ptr->GetRootCerts().has_value(), true);
  EXPECT_STREQ(std::string(*watcher_ptr->GetRootCerts()).c_str(),
               "root_certificate_contents");
  EXPECT_EQ(watcher_ptr->GetKeyCertPairs().has_value(), true);
  auto cert_pair_list = *watcher_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "identity_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), "identity_certificate_contents");

  // SetRootCerts should trigger watcher's OnCertificatesChanged again.
  distributor.SetRootCerts("root_cert_name",
                           "another_root_certificate_contents");
  EXPECT_EQ(watcher_ptr->GetRootCerts().has_value(), true);
  EXPECT_STREQ(std::string(*watcher_ptr->GetRootCerts()).c_str(),
               "another_root_certificate_contents");
  cert_pair_list = *watcher_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "identity_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(), "identity_certificate_contents");

  // SetKeyCertPairs should trigger watcher's OnCertificatesChanged again.
  grpc_ssl_pem_key_cert_pair* another_ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  another_ssl_pair->private_key =
      gpr_strdup("another_identity_private_key_contents");
  another_ssl_pair->cert_chain =
      gpr_strdup("another_identity_certificate_contents");
  grpc_tls_certificate_distributor::PemKeyCertPairList
      another_pem_key_cert_pairs;
  another_pem_key_cert_pairs.emplace_back(
      grpc_core::PemKeyCertPair(another_ssl_pair));
  distributor.SetKeyCertPairs("identity_cert_name", another_pem_key_cert_pairs);
  EXPECT_EQ(watcher_ptr->GetRootCerts().has_value(), true);
  EXPECT_STREQ(std::string(*watcher_ptr->GetRootCerts()).c_str(),
               "another_root_certificate_contents");
  cert_pair_list = *watcher_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "another_identity_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(),
               "another_identity_certificate_contents");

  distributor.CancelTlsCertificatesWatch(watcher_ptr);
}

// In this test, we create a scenario where we have 5 watchers and 3 credentials
// being watched, to test the credential updating and
// watching status changing. Details are:
// - watcher 1 watches the root cert of cert_1 and identity cert of cert_2
// - watcher 2 watches the root cert of cert_3 and identity cert of cert_1
// - watcher 3 watches the identity cert of cert_3
// - watcher 4 watches the root cert of cert_1
// - watcher 5 watches the root cert of cert_2 and identity cert of cert_2
// We will invoke events in the following sequence to see if they behave as
// expected:
// register watcher 1 -> register watcher 4 -> register watcher 2 ->
// update cert_1 ->register watcher 5 -> cancel watcher 5 -> cancel watcher 4
// -> update cert_2 -> cancel watcher 1 -> register watcher 3 -> update cert_3
// -> cancel watcher 2 -> cancel watcher 3
TEST(GrpcTlsCertificateDistributorTest, CredentialAndWatcherInterop) {
  std::queue<std::string> err_queue;
  grpc_tls_certificate_distributor distributor;
  std::queue<CallbackStatus> queue;
  distributor.SetWatchStatusCallback([&queue](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    queue.push(
        CallbackStatus(cert_name, root_being_watched, identity_being_watched));
  });

  // Register watcher 1.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_1 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1),
                                   absl::make_optional("cert_1"),
                                   absl::make_optional("cert_2"));
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_1", true, false},
                                         {"cert_2", false, true}});

  // Register watcher 4.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_4 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_4_ptr = watcher_4.get();
  distributor.WatchTlsCertificates(
      std::move(watcher_4), absl::make_optional("cert_1"), absl::nullopt);
  EXPECT_EQ(queue.size(), 0);

  // Register watcher 2.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_2 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_2_ptr = watcher_2.get();
  distributor.WatchTlsCertificates(std::move(watcher_2),
                                   absl::make_optional("cert_3"),
                                   absl::make_optional("cert_1"));
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", true, false},
                                         {"cert_1", true, true}});

  // Push credential updates to cert_1 and check if the status works as
  // expected.
  grpc_ssl_pem_key_cert_pair* ssl_pair_1 =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair_1->private_key = gpr_strdup("identity_1_private_key_contents");
  ssl_pair_1->cert_chain = gpr_strdup("identity_1_certificate_contents");
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs_1;
  pem_key_cert_pairs_1.emplace_back(grpc_core::PemKeyCertPair(ssl_pair_1));
  distributor.SetKeyMaterials(
      "cert_1",
      absl::make_optional(absl::string_view("root_1_certificate_contents")),
      "cert_1", absl::make_optional(pem_key_cert_pairs_1));
  // Check the updates are delivered to watcher 1.
  EXPECT_EQ(watcher_1_ptr->GetRootCerts().has_value(), true);
  EXPECT_STREQ(std::string(*watcher_1_ptr->GetRootCerts()).c_str(),
               "root_1_certificate_contents");
  EXPECT_EQ(watcher_1_ptr->GetKeyCertPairs().has_value(), true);
  EXPECT_EQ((*watcher_1_ptr->GetKeyCertPairs()).size(), 0);
  // Check the updates are delivered to watcher 4.
  EXPECT_EQ(watcher_4_ptr->GetRootCerts().has_value(), true);
  EXPECT_STREQ(std::string(*watcher_4_ptr->GetRootCerts()).c_str(),
               "root_1_certificate_contents");
  EXPECT_EQ(watcher_4_ptr->GetKeyCertPairs().has_value(), false);
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts().has_value(), true);
  EXPECT_EQ(*watcher_2_ptr->GetRootCerts(), "");
  auto cert_pair_list = *watcher_2_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "identity_1_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(),
               "identity_1_certificate_contents");

  // Register watcher 5.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_5 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_5_ptr = watcher_5.get();
  distributor.WatchTlsCertificates(std::move(watcher_5),
                                   absl::make_optional("cert_2"),
                                   absl::make_optional("cert_2"));
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_2", true, true}});

  // Cancel watcher 5.
  distributor.CancelTlsCertificatesWatch(watcher_5_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_2", false, true}});
  // Cancel watcher 4.
  distributor.CancelTlsCertificatesWatch(watcher_4_ptr);
  EXPECT_EQ(queue.size(), 0);

  // Push credential updates to cert_2, and check if the status works as
  // expected.
  grpc_ssl_pem_key_cert_pair* ssl_pair_2 =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair_2->private_key = gpr_strdup("identity_2_private_key_contents");
  ssl_pair_2->cert_chain = gpr_strdup("identity_2_certificate_contents");
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs_2;
  pem_key_cert_pairs_2.emplace_back(grpc_core::PemKeyCertPair(ssl_pair_2));
  distributor.SetKeyMaterials(
      "cert_2",
      absl::make_optional(absl::string_view("root_2_certificate_contents")),
      "cert_2", absl::make_optional(pem_key_cert_pairs_2));
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts().has_value(), true);
  EXPECT_STREQ(std::string(*watcher_2_ptr->GetRootCerts()).c_str(), "");
  EXPECT_EQ(watcher_2_ptr->GetKeyCertPairs().has_value(), true);
  cert_pair_list = *watcher_2_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "identity_1_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(),
               "identity_1_certificate_contents");

  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_1", false, true},
                                         {"cert_2", false, false}});

  // Register watcher 3.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_3 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), absl::nullopt,
                                   absl::make_optional("cert_3"));
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", true, true}});

  // Push credential updates to cert_3, and check if the status works as
  // expected.
  grpc_ssl_pem_key_cert_pair* ssl_pair_3 =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair_3->private_key = gpr_strdup("identity_3_private_key_contents");
  ssl_pair_3->cert_chain = gpr_strdup("identity_3_certificate_contents");
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs_3;
  pem_key_cert_pairs_3.emplace_back(grpc_core::PemKeyCertPair(ssl_pair_3));
  distributor.SetKeyMaterials(
      "cert_3",
      absl::make_optional(absl::string_view("root_3_certificate_contents")),
      "cert_3", absl::make_optional(pem_key_cert_pairs_3));
  // Check the updates are delivered to watcher 3.
  EXPECT_EQ(watcher_3_ptr->GetRootCerts().has_value(), false);
  EXPECT_EQ(watcher_3_ptr->GetKeyCertPairs().has_value(), true);
  cert_pair_list = *watcher_3_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "identity_3_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(),
               "identity_3_certificate_contents");

  // Make another push to cert_3, and see if the contents get updated.
  grpc_ssl_pem_key_cert_pair* another_ssl_pair_3 =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  another_ssl_pair_3->private_key =
      gpr_strdup("another_identity_3_private_key_contents");
  another_ssl_pair_3->cert_chain =
      gpr_strdup("another_identity_3_certificate_contents");
  grpc_tls_certificate_distributor::PemKeyCertPairList
      another_pem_key_cert_pairs_3;
  another_pem_key_cert_pairs_3.emplace_back(
      grpc_core::PemKeyCertPair(another_ssl_pair_3));
  distributor.SetKeyMaterials(
      "cert_3",
      absl::make_optional(
          absl::string_view("another_root_3_certificate_contents")),
      "cert_3", absl::make_optional(another_pem_key_cert_pairs_3));
  // Check the updates are delivered to watcher 3.
  EXPECT_EQ(watcher_3_ptr->GetRootCerts().has_value(), false);
  EXPECT_EQ(watcher_3_ptr->GetKeyCertPairs().has_value(), true);
  cert_pair_list = *watcher_3_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "another_identity_3_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(),
               "another_identity_3_certificate_contents");
  // Check the updates are delivered to watcher 2.
  EXPECT_EQ(watcher_2_ptr->GetRootCerts().has_value(), true);
  EXPECT_STREQ(std::string(*watcher_2_ptr->GetRootCerts()).c_str(),
               "another_root_3_certificate_contents");
  EXPECT_EQ(watcher_2_ptr->GetKeyCertPairs().has_value(), true);
  cert_pair_list = *watcher_2_ptr->GetKeyCertPairs();
  EXPECT_EQ(cert_pair_list.size(), 1);
  EXPECT_STREQ(cert_pair_list[0].private_key(),
               "identity_1_private_key_contents");
  EXPECT_STREQ(cert_pair_list[0].cert_chain(),
               "identity_1_certificate_contents");

  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", false, true},
                                         {"cert_1", false, false}});
  // Cancel watcher 3.
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
  VerifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", false, false}});
  // At this point, the watcher status map should be cleaned up.
  EXPECT_EQ(queue.size(), 0);
}

// Cancel an unregistered watcher should not make the program crash(while we
// will log the errors).
TEST(GrpcTlsCertificateDistributorTest, CancelUnregisteredWatcher) {
  std::queue<std::string> err_queue;
  grpc_tls_certificate_distributor distributor;
  std::unique_ptr<TlsCertificatesTestWatcher> watcher =
      std::unique_ptr<TlsCertificatesTestWatcher>(nullptr);
  distributor.WatchTlsCertificates(std::move(watcher),
                                   absl::make_optional("cert_1"),
                                   absl::make_optional("cert_1"));
  distributor.CancelTlsCertificatesWatch(watcher.get());
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
