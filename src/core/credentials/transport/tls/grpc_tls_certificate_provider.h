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

#ifndef GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
#define GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H

#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_distributor.h"
#include "src/core/credentials/transport/tls/spiffe_utils.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/thd.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/useful.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

// Interface for a grpc_tls_certificate_provider that handles the process to
// fetch credentials and validation contexts. Implementations are free to rely
// on local or remote sources to fetch the latest secrets, and free to share any
// state among different instances as they deem fit.
//
// On creation, grpc_tls_certificate_provider creates a
// grpc_tls_certificate_distributor object. When the credentials and validation
// contexts become valid or changed, a grpc_tls_certificate_provider should
// notify its distributor so as to propagate the update to the watchers.
struct grpc_tls_certificate_provider
    : public grpc_core::RefCounted<grpc_tls_certificate_provider> {
 public:
  virtual grpc_core::RefCountedPtr<grpc_tls_certificate_distributor>
  distributor() const = 0;

  // Compares this grpc_tls_certificate_provider object with \a other.
  // If this method returns 0, it means that gRPC can treat the two certificate
  // providers as effectively the same. This method is used to compare
  // `grpc_tls_certificate_provider` objects when they are present in
  // channel_args. One important usage of this is when channel args are used in
  // SubchannelKey, which leads to a useful property that allows subchannels to
  // be reused when two different `grpc_tls_certificate_provider` objects are
  // used but they compare as equal (assuming other channel args match).
  int Compare(const grpc_tls_certificate_provider* other) const {
    GRPC_CHECK_NE(other, nullptr);
    int r = type().Compare(other->type());
    if (r != 0) return r;
    return CompareImpl(other);
  }

  // The pointer value \a type is used to uniquely identify a creds
  // implementation for down-casting purposes. Every provider implementation
  // should use a unique string instance, which should be returned by all
  // instances of that provider implementation.
  virtual grpc_core::UniqueTypeName type() const = 0;

  static absl::string_view ChannelArgName();
  static int ChannelArgsCompare(const grpc_tls_certificate_provider* a,
                                const grpc_tls_certificate_provider* b) {
    return a->Compare(b);
  }

 private:
  // Implementation for `Compare` method intended to be overridden by
  // subclasses. Only invoked if `type()` and `other->type()` point to the same
  // string.
  virtual int CompareImpl(const grpc_tls_certificate_provider* other) const = 0;
};

namespace grpc_core {

// A provider class that will watch the credential changes on the file system.
class FileWatcherCertificateProvider final
    : public grpc_tls_certificate_provider {
 public:
  FileWatcherCertificateProvider(std::string private_key_path,
                                 std::string identity_certificate_path,
                                 std::string root_cert_path,
                                 std::string spiffe_bundle_map_path,
                                 int64_t refresh_interval_sec);

  ~FileWatcherCertificateProvider() override;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

  UniqueTypeName type() const override;

  absl::Status ValidateCredentials() const;

  int64_t TestOnlyGetRefreshIntervalSecond() const;

 private:
  struct WatcherInfo {
    bool root_being_watched = false;
    bool identity_being_watched = false;
  };

  int CompareImpl(const grpc_tls_certificate_provider* other) const override {
    // TODO(yashykt): Maybe do something better here.
    return QsortCompare(static_cast<const grpc_tls_certificate_provider*>(this),
                        other);
  }

  // Force an update from the file system regardless of the interval.
  void ForceUpdate();
  // Read the root certificates from files and update the distributor.
  std::optional<std::string> ReadRootCertificatesFromFile(
      const std::string& root_cert_full_path);
  // Read the private key and the certificate chain from files and update the
  // distributor.
  std::optional<PemKeyCertPairList> ReadIdentityKeyCertPairFromFiles(
      const std::string& private_key_path,
      const std::string& identity_certificate_path);

  // Information that is used by the refreshing thread.
  std::string private_key_path_;
  std::string identity_certificate_path_;
  std::string root_cert_path_;
  std::string spiffe_bundle_map_path_;
  int64_t refresh_interval_sec_ = 0;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  Thread refresh_thread_;
  gpr_event shutdown_event_;

  // Guards members below.
  mutable Mutex mu_;
  // The most-recent credential data. It will be empty if the most recent read
  // attempt failed.
  PemKeyCertPairList pem_key_cert_pairs_ ABSL_GUARDED_BY(mu_);
  // The most-recent root data.
  // - If unset, the status will be OK and the value will be nullptr
  // - If a SPIFFE Bundle Map is configured and fails to read, the status will
  // be not-Ok
  // - If a string root cert is configured and fails to read, the status will be
  // OK with a nullptr
  // - Otherwise, holds either a SpiffeBundleMap or a string root cert
  // TODO(gtcooke94) - refactor the handling for string root cert files such
  // that their failure is a non-ok status rather than a nullptr
  absl::StatusOr<std::shared_ptr<RootCertInfo>> root_cert_info_
      ABSL_GUARDED_BY(mu_) = nullptr;
  // Stores each cert_name we get from the distributor callback and its watcher
  // information.
  std::map<std::string, WatcherInfo> watcher_info_ ABSL_GUARDED_BY(mu_);
};

// Implements a provider that uses in-memory data that can be modified in a
// thread-safe manner.
class InMemoryCertificateProvider final : public grpc_tls_certificate_provider {
 public:
  InMemoryCertificateProvider();
  InMemoryCertificateProvider(const InMemoryCertificateProvider&) = delete;
  InMemoryCertificateProvider(InMemoryCertificateProvider&&) = delete;
  InMemoryCertificateProvider& operator=(const InMemoryCertificateProvider&) =
      delete;
  InMemoryCertificateProvider& operator=(InMemoryCertificateProvider&&) =
      delete;

  // TODO(anasalazar): Expose APIs so that callers can update in the root
  // certificate.
  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

  UniqueTypeName type() const override;

  absl::Status ValidateCredentials() const;

  void UpdateRoot(std::shared_ptr<RootCertInfo> root_certificates);
  void UpdateIdentity(const PemKeyCertPairList& pem_key_cert_pairs);

  static RefCountedPtr<grpc_tls_certificate_provider>
  CreateTestingCertificateProvider(
      std::string root_cert_info, const PemKeyCertPairList& pem_key_cert_pairs);

 private:
  struct WatcherInfo {
    bool root_being_watched = false;
    bool identity_being_watched = false;
  };

  int CompareImpl(const grpc_tls_certificate_provider* other) const override {
    return QsortCompare(static_cast<const grpc_tls_certificate_provider*>(this),
                        other);
  }
  void ForceUpdate(std::optional<std::shared_ptr<RootCertInfo>> root_cert_info,
                   std::optional<const PemKeyCertPairList> pem_key_cert_pairs);

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;

  // Guards pem_key_cert_pairs_, root_certificates_ and watcher_info_.
  mutable Mutex mu_;
  // The most-recent credential data. It will be empty if the most recent read
  // attempt failed.
  PemKeyCertPairList pem_key_cert_pairs_ ABSL_GUARDED_BY(mu_);
  absl::StatusOr<std::shared_ptr<RootCertInfo>> root_certificates_
      ABSL_GUARDED_BY(mu_);
  // Stores each cert_name we get from the distributor callback and its watcher
  // information.
  std::map<std::string, WatcherInfo> watcher_info_ ABSL_GUARDED_BY(mu_);
};

//  Checks if the private key matches the certificate's public key.
//  Returns a not-OK status on failure, or a bool indicating
//  whether the key/cert pair matches.
absl::StatusOr<bool> PrivateKeyAndCertificateMatch(
    absl::string_view private_key, absl::string_view cert_chain);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
