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

namespace testing {
// TlsCertificatesTestWatcher is a simple watcher implementation for testing
// purpose.
class TlsCertificatesTestWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  TlsCertificatesTestWatcher(std::queue<std::string>& err_queue)
      : grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface(),
        root_certs_(absl::nullopt),
        key_cert_pairs_(absl::nullopt),
        err_queue_(err_queue) {}
  void OnCertificatesChanged(
      absl::optional<std::string> root_certs,
      absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
          key_cert_pairs) {
    this->root_certs_ = root_certs;
    this->key_cert_pairs_ = key_cert_pairs;
  }
  void OnError(grpc_error* error) {
    if (error != nullptr) {
      grpc_slice str;
      GPR_ASSERT(grpc_error_get_str(error, GRPC_ERROR_STR_DESCRIPTION, &str));
      err_queue_.push(std::string((char*)GRPC_SLICE_START_PTR(str)));
      GRPC_ERROR_UNREF(error);
    }
  }
  ~TlsCertificatesTestWatcher() {}
  absl::optional<std::string> GetRootCerts() { return this->root_certs_; }
  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
  GetKeyCertPairs() {
    return this->key_cert_pairs_;
  }

 private:
  absl::optional<std::string> root_certs_;
  absl::optional<grpc_tls_certificate_distributor::PemKeyCertPairList>
      key_cert_pairs_;
  std::queue<std::string>& err_queue_;
};

// CallbackStatus contains the parameters in  the watch_status_callback_ of
// the distributor. When a particular callback is invoked, we will push a
// CallbackStatus to a queue, and later check if the status updates are correct.
struct CallbackStatus {
  std::string cert_name;
  bool root_being_watched;
  bool identity_being_watched;
};

// A helper function to set key materials in the distributor.
void setKeyMaterialsIntoDistributor(
    grpc_tls_certificate_distributor& distributor, bool has_root,
    const std::string& root_cert_name, const std::string& root_cert_contents,
    bool has_identity, const std::string& identity_cert_name,
    const std::string& identity_key_contents,
    const std::string& identity_cert_contents) {
  if (!has_root && !has_identity) {
    // We deliberately call SetKeyMaterials with two absl::nullopt certificate
    // contents to test its behaviors.
    distributor.SetKeyMaterials(root_cert_name, absl::nullopt,
                                identity_cert_name, absl::nullopt);
    return;
  }
  if (has_root && !has_identity) {
    distributor.SetRootCerts(root_cert_name, root_cert_contents);
    return;
  }
  // At this point, we assume identity_key_contents and identity_cert_contents
  // must not be absl::nullopt.
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      (grpc_ssl_pem_key_cert_pair*)gpr_malloc(
          sizeof(grpc_ssl_pem_key_cert_pair));
  ssl_pair->private_key = gpr_strdup(identity_key_contents.c_str());
  ssl_pair->cert_chain = gpr_strdup(identity_cert_contents.c_str());
  grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  pem_key_cert_pairs.emplace_back(grpc_core::PemKeyCertPair(ssl_pair));
  if (!has_root) {
    distributor.SetKeyCertPairs(identity_cert_name, pem_key_cert_pairs);
    return;
  }
  distributor.SetKeyMaterials(root_cert_name, root_cert_contents,
                              identity_cert_name, pem_key_cert_pairs);
}

// A helper function to check if the credentials are successfully updated to the
// watchers.
void verifyCredentialUpdatesInWatcher(
    TlsCertificatesTestWatcher* watcher_ptr, bool check_root,
    const std::string& root_cert_contents, bool check_identity,
    const std::string& identity_key_contents,
    const std::string& identity_cert_contents) {
  if (!check_root) {
    EXPECT_EQ(watcher_ptr->GetRootCerts(), absl::nullopt);
  } else {
    EXPECT_STREQ((*watcher_ptr->GetRootCerts()).c_str(),
                 root_cert_contents.c_str());
  }
  if (!check_identity) {
    EXPECT_EQ(watcher_ptr->GetKeyCertPairs(), absl::nullopt);
  } else {
    // If need to check identity key materials, we assume the list length would
    // always be 1 across this test.
    auto cert_pair_list = *watcher_ptr->GetKeyCertPairs();
    EXPECT_EQ(cert_pair_list.size(), 1);
    EXPECT_STREQ(cert_pair_list[0].private_key(),
                 identity_key_contents.c_str());
    EXPECT_STREQ(cert_pair_list[0].cert_chain(),
                 identity_cert_contents.c_str());
  }
}

// A helper function to check if the watch_status_callback_ field of the
// distributor is invoked as expected.
void verifyCallbackStatusQueue(
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
  setKeyMaterialsIntoDistributor(distributor, false, "root_cert_name", "",
                                 false, "identity_cert_name", "", "");
  EXPECT_EQ(distributor.HasRootCerts("root_cert_name"), false);
  EXPECT_EQ(distributor.HasKeyCertPairs("identity_cert_name"), false);
  // After setting the certificates to the corresponding cert names, the
  // distributor should possess the corresponding certs.
  setKeyMaterialsIntoDistributor(distributor, true, "root_cert_name",
                                 "root_certificate_contents", false,
                                 "identity_cert_name", "", "");
  EXPECT_EQ(distributor.HasRootCerts("root_cert_name"), true);
  setKeyMaterialsIntoDistributor(
      distributor, false, "", "", true, "identity_cert_name",
      "identity_private_key_contents", "identity_certificate_contents");
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
  setKeyMaterialsIntoDistributor(
      distributor, true, "root_cert_name", "root_certificate_contents", true,
      "identity_cert_name", "identity_private_key_contents",
      "identity_certificate_contents");
  verifyCredentialUpdatesInWatcher(
      watcher_ptr, true, "root_certificate_contents", true,
      "identity_private_key_contents", "identity_certificate_contents");
  EXPECT_EQ(err_queue.size(), 0);
  // SetRootCerts should trigger watcher's OnCertificatesChanged again.
  setKeyMaterialsIntoDistributor(distributor, true, "root_cert_name",
                                 "another_root_certificate_contents", false, "",
                                 "", "");
  verifyCredentialUpdatesInWatcher(
      watcher_ptr, true, "another_root_certificate_contents", true,
      "identity_private_key_contents", "identity_certificate_contents");
  EXPECT_EQ(err_queue.size(), 0);
  // SetKeyCertPairs should trigger watcher's OnCertificatesChanged again.
  setKeyMaterialsIntoDistributor(distributor, false, "", "", true,
                                 "identity_cert_name",
                                 "another_identity_private_key_contents",
                                 "another_identity_certificate_contents");
  verifyCredentialUpdatesInWatcher(watcher_ptr, true,
                                   "another_root_certificate_contents", true,
                                   "another_identity_private_key_contents",
                                   "another_identity_certificate_contents");
  EXPECT_EQ(err_queue.size(), 0);
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
        CallbackStatus{cert_name, root_being_watched, identity_being_watched});
  });
  // Register watcher 1.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_1 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_1_ptr = watcher_1.get();
  distributor.WatchTlsCertificates(std::move(watcher_1),
                                   absl::make_optional("cert_1"),
                                   absl::make_optional("cert_2"));
  verifyCallbackStatusQueue(
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
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", true, false},
                                         {"cert_1", true, true}});
  // Push credential updates to cert_1, and check if the status works as
  // expected.
  setKeyMaterialsIntoDistributor(distributor, true, "cert_1",
                                 "root_1_certificate_contents", true, "cert_1",
                                 "identity_1_private_key_contents",
                                 "identity_1_certificate_contents");
  verifyCredentialUpdatesInWatcher(
      watcher_1_ptr, true, "root_1_certificate_contents", false, "", "");
  verifyCredentialUpdatesInWatcher(
      watcher_4_ptr, true, "root_1_certificate_contents", false, "", "");
  verifyCredentialUpdatesInWatcher(watcher_2_ptr, false, "", true,
                                   "identity_1_private_key_contents",
                                   "identity_1_certificate_contents");
  // Register watcher 5.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_5 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_5_ptr = watcher_5.get();
  distributor.WatchTlsCertificates(std::move(watcher_5),
                                   absl::make_optional("cert_2"),
                                   absl::make_optional("cert_2"));
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_2", true, true}});
  // Cancel watcher 5.
  distributor.CancelTlsCertificatesWatch(watcher_5_ptr);
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_2", false, true}});
  // Cancel watcher 4.
  distributor.CancelTlsCertificatesWatch(watcher_4_ptr);
  EXPECT_EQ(queue.size(), 0);
  // Push credential updates to cert_2, and check if the status works as
  // expected.
  setKeyMaterialsIntoDistributor(distributor, true, "cert_2",
                                 "root_2_certificate_contents", true, "cert_2",
                                 "identity_2_private_key_contents",
                                 "identity_2_certificate_contents");
  verifyCredentialUpdatesInWatcher(
      watcher_1_ptr, true, "root_1_certificate_contents", true,
      "identity_2_private_key_contents", "identity_2_certificate_contents");
  verifyCredentialUpdatesInWatcher(watcher_2_ptr, false, "", true,
                                   "identity_1_private_key_contents",
                                   "identity_1_certificate_contents");
  // Cancel watcher 1.
  distributor.CancelTlsCertificatesWatch(watcher_1_ptr);
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_1", false, true},
                                         {"cert_2", false, false}});
  // Register watcher 3.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_3 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  TlsCertificatesTestWatcher* watcher_3_ptr = watcher_3.get();
  distributor.WatchTlsCertificates(std::move(watcher_3), absl::nullopt,
                                   absl::make_optional("cert_3"));
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", true, true}});
  // Push credential updates to cert_3, and check if the status works as
  // expected.
  setKeyMaterialsIntoDistributor(distributor, true, "cert_3",
                                 "root_3_certificate_contents", true, "cert_3",
                                 "identity_3_private_key_contents",
                                 "identity_3_certificate_contents");
  verifyCredentialUpdatesInWatcher(watcher_3_ptr, false, "", true,
                                   "identity_3_private_key_contents",
                                   "identity_3_certificate_contents");
  verifyCredentialUpdatesInWatcher(
      watcher_2_ptr, true, "root_3_certificate_contents", true,
      "identity_1_private_key_contents", "identity_1_certificate_contents");
  // Make another push to cert_3, and see if the contents get updated.
  setKeyMaterialsIntoDistributor(
      distributor, true, "cert_3", "another_root_3_certificate_contents", true,
      "cert_3", "another_identity_3_private_key_contents",
      "another_identity_3_certificate_contents");
  verifyCredentialUpdatesInWatcher(watcher_3_ptr, false, "", true,
                                   "another_identity_3_private_key_contents",
                                   "another_identity_3_certificate_contents");
  verifyCredentialUpdatesInWatcher(
      watcher_2_ptr, true, "another_root_3_certificate_contents", true,
      "identity_1_private_key_contents", "identity_1_certificate_contents");
  // Cancel watcher 2.
  distributor.CancelTlsCertificatesWatch(watcher_2_ptr);
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", false, true},
                                         {"cert_1", false, false}});
  // Cancel watcher 3.
  distributor.CancelTlsCertificatesWatch(watcher_3_ptr);
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_3", false, false}});
  // At this point, the watcher status map should be cleaned up.
  EXPECT_EQ(queue.size(), 0);
}

// Test a case when the distributor is destructed with some watchers still
// watching, its dtor will invoke the proper callbacks and OnError of each
// existing watcher.
TEST(GrpcTlsCertificateDistributorTest, DestructorCleanUp) {
  std::queue<std::string> err_queue;
  std::unique_ptr<grpc_tls_certificate_distributor> distributor_ptr =
      std::unique_ptr<grpc_tls_certificate_distributor>(
          new grpc_tls_certificate_distributor);
  std::queue<CallbackStatus> queue;
  distributor_ptr->SetWatchStatusCallback(
      [&queue](std::string cert_name, bool root_being_watched,
               bool identity_being_watched) {
        queue.push(CallbackStatus{cert_name, root_being_watched,
                                  identity_being_watched});
      });
  // Register watcher 1.
  std::unique_ptr<TlsCertificatesTestWatcher> watcher_1 =
      std::unique_ptr<TlsCertificatesTestWatcher>(
          new TlsCertificatesTestWatcher(err_queue));
  distributor_ptr->WatchTlsCertificates(std::move(watcher_1),
                                        absl::make_optional("cert_1"),
                                        absl::make_optional("cert_1"));
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_1", true, false},
                                         {"cert_1", true, true}});
  // Reset the distributor without canceling watcher 1.
  distributor_ptr.reset();
  // The error should be populated into err_queue.
  EXPECT_EQ(err_queue.size(), 1);
  std::string err_msg = err_queue.front();
  EXPECT_STREQ(err_msg.c_str(),
               "The grpc_tls_certificate_distributor is destructed but the "
               "watcher may still be used.");
  verifyCallbackStatusQueue(
      queue, std::vector<CallbackStatus>{{"cert_1", false, false}});
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
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
