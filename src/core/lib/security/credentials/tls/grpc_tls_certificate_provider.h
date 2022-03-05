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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H

#include <grpc/support/port_platform.h>

#include <string.h>

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"

#include <grpc/grpc_security.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

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
  virtual grpc_pollset_set* interested_parties() const { return nullptr; }

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
    GPR_ASSERT(other != nullptr);
    // Intentionally uses grpc_core::QsortCompare instead of strcmp as a safety
    // against different grpc_tls_certificate_provider types using the same
    // name.
    int r = grpc_core::QsortCompare(type(), other->type());
    if (r != 0) return r;
    return CompareImpl(other);
  }

  // The pointer value \a type is used to uniquely identify a creds
  // implementation for down-casting purposes. Every provider implementation
  // should use a unique string instance, which should be returned by all
  // instances of that provider implementation.
  virtual const char* type() const = 0;

 private:
  // Implementation for `Compare` method intended to be overridden by
  // subclasses. Only invoked if `type()` and `other->type()` point to the same
  // string.
  virtual int CompareImpl(const grpc_tls_certificate_provider* other) const = 0;
};

namespace grpc_core {

// A basic provider class that will get credentials from string during
// initialization.
class StaticDataCertificateProvider final
    : public grpc_tls_certificate_provider {
 public:
  StaticDataCertificateProvider(std::string root_certificate,
                                PemKeyCertPairList pem_key_cert_pairs);

  ~StaticDataCertificateProvider() override;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

  const char* type() const override { return "StaticData"; }

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

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  std::string root_certificate_;
  PemKeyCertPairList pem_key_cert_pairs_;
  // Guards members below.
  Mutex mu_;
  // Stores each cert_name we get from the distributor callback and its watcher
  // information.
  std::map<std::string, WatcherInfo> watcher_info_;
};

// A provider class that will watch the credential changes on the file system.
class FileWatcherCertificateProvider final
    : public grpc_tls_certificate_provider {
 public:
  FileWatcherCertificateProvider(std::string private_key_path,
                                 std::string identity_certificate_path,
                                 std::string root_cert_path,
                                 unsigned int refresh_interval_sec);

  ~FileWatcherCertificateProvider() override;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

  const char* type() const override { return "FileWatcher"; }

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
  absl::optional<std::string> ReadRootCertificatesFromFile(
      const std::string& root_cert_full_path);
  // Read the private key and the certificate chain from files and update the
  // distributor.
  absl::optional<PemKeyCertPairList> ReadIdentityKeyCertPairFromFiles(
      const std::string& private_key_path,
      const std::string& identity_certificate_path);

  // Information that is used by the refreshing thread.
  std::string private_key_path_;
  std::string identity_certificate_path_;
  std::string root_cert_path_;
  unsigned int refresh_interval_sec_ = 0;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  Thread refresh_thread_;
  gpr_event shutdown_event_;

  // Guards members below.
  Mutex mu_;
  // The most-recent credential data. It will be empty if the most recent read
  // attempt failed.
  std::string root_certificate_ ABSL_GUARDED_BY(mu_);
  PemKeyCertPairList pem_key_cert_pairs_ ABSL_GUARDED_BY(mu_);
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

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
