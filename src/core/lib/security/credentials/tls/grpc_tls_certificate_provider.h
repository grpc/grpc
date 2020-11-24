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

#include <grpc/grpc_security.h>
#include <string.h>

#include "absl/container/inlined_vector.h"

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

// A basic provider class that will get credentials from string during
// initialization.
class StaticDataCertificateProvider final
    : public grpc_tls_certificate_provider {
 public:
  StaticDataCertificateProvider(
      std::string root_certificate,
      grpc_core::PemKeyCertPairList pem_key_cert_pairs);

  ~StaticDataCertificateProvider() override;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

 private:
  struct WatcherInfo {
    bool root_being_watched = false;
    bool identity_being_watched = false;
  };
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  std::string root_certificate_;
  grpc_core::PemKeyCertPairList pem_key_cert_pairs_;
  // Guards members below.
  grpc_core::Mutex mu_;
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
  // Read the root certificates from files and update the distributor.
  absl::optional<PemKeyCertPairList> ReadIdentityKeyCertPairFromFiles(
      const std::string& private_key_path,
      const std::string& identity_certificate_path);

  // Information that is used by the refreshing thread.
  std::string private_key_path_;
  std::string identity_certificate_path_;
  std::string root_cert_path_;
  unsigned int refresh_interval_sec_ = 0;

  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  grpc_core::Thread refresh_thread_;
  gpr_event shutdown_event_;

  // Guards members below.
  grpc_core::Mutex mu_;
  // The most-recent credential data. It will be empty if the most recent read
  // attempt failed.
  std::string root_certificate_;
  grpc_core::PemKeyCertPairList pem_key_cert_pairs_;
  // Stores each cert_name we get from the distributor callback and its watcher
  // information.
  std::map<std::string, WatcherInfo> watcher_info_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
