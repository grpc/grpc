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
};

namespace grpc_core {

class DataWatcherCertificateProvider;

// A helper class which is used by the provider that needs to watch credential
// updates. It handles the logic to notify the distributor when seeing an
// update.
class CertificateProviderWatcherNotifier : public grpc_core::RefCounted<CertificateProviderWatcherNotifier> {
 public:
  CertificateProviderWatcherNotifier(DataWatcherCertificateProvider* provider);

  ~CertificateProviderWatcherNotifier();

  void SetRootCertificate(std::string root_certificate);
  void SetKeyCertificatePairs(PemKeyCertPairList pem_key_cert_pairs);
  void SetRootCertificateAndKeyCertificatePairs(
      std::string root_certificate, PemKeyCertPairList pem_key_cert_pairs);

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const {
    return distributor_;
  }

 private:
  struct WatcherInfo {
    bool root_being_watched = false;
    bool identity_being_watched = false;
  };

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;

  Mutex mu_;
  // Stores each cert_name we get from the distributor callback and its watcher
  // information.
  std::map<std::string, WatcherInfo> watcher_info_ ABSL_GUARDED_BY(mu_);

  DataWatcherCertificateProvider* provider_ = nullptr;
};

// A basic provider class that callers can set its credentials by explicitly
// through |SetRootCertificate| or |SetKeyCertificatePairs|.
class DataWatcherCertificateProvider final
    : public grpc_tls_certificate_provider {
 public:
  DataWatcherCertificateProvider();

  // Sets the root_certificate and updates the distributor.
  absl::Status SetRootCertificate(std::string root_certificate);

  // Sets the key-cert pair list and updates the distributor.
  absl::Status SetKeyCertificatePairs(PemKeyCertPairList pem_key_cert_pairs);

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_notifier_->distributor();
  }

  std::string root_certificate();

  PemKeyCertPairList pem_key_cert_pairs();

 private:
  Mutex mu_;
  // The most-recent credential data. It will be empty if the most recent read
  // attempt failed.
  std::string root_certificate_ ABSL_GUARDED_BY(mu_);
  PemKeyCertPairList pem_key_cert_pairs_ ABSL_GUARDED_BY(mu_);

  RefCountedPtr<CertificateProviderWatcherNotifier> distributor_notifier_;
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

 private:
  struct WatcherInfo {
    bool root_being_watched = false;
    bool identity_being_watched = false;
  };
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

//  Checks if the private key and the certificate chain for all pairs in the
//  list match. Returns an OK status if matched, or |pair_list| is empty.
//  Otherwise, an error status is returned.
absl::StatusOr<bool> PrivateKeyAndCertificateMatch(
    const PemKeyCertPairList& pair_list);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
