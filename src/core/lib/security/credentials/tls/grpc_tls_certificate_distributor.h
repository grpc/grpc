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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_DISTRIBUTOR_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_DISTRIBUTOR_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "absl/container/inlined_vector.h"
#include "absl/types/optional.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

/* TLS certificate distributor. */
struct grpc_tls_certificate_distributor
    : public grpc_core::RefCounted<grpc_tls_certificate_distributor> {
 public:
  typedef absl::InlinedVector<grpc_core::PemKeyCertPair, 1> PemKeyCertPairList;

  // Interface for watching TLS certificates update.
  class TlsCertificatesWatcherInterface {
   public:
    virtual ~TlsCertificatesWatcherInterface() = default;
    // OnCertificatesChanged describes the actions the implementer will take
    // when the root or identity certificates are updated. Setting to
    // absl::nullopt indicates no corresponding updates for root_certs or
    // key_cert_pairs.
    virtual void OnCertificatesChanged(
        absl::optional<std::string> root_certs,
        absl::optional<PemKeyCertPairList> key_cert_pairs) = 0;
    // OnError describes the actions the implementer will take when an error
    // happens. The implementer owns the grpc_error.
    virtual void OnError(grpc_error* error) = 0;
  };

  ~grpc_tls_certificate_distributor();

  /**
   * Set the key materials based on their certificate names.
   *
   * @param root_cert_name The name of the root certificates being updated.
   * @param identity_cert_name The name of the identity certificates being
   * updated.
   * @param pem_root_certs The content of root certificates. If set to
   * absl::nullopt, the root certificates won't be updated and the
   * root_cert_name will be ignored.
   * @param pem_key_cert_pairs The content of identity key-cert pairs. If set to
   * absl::nullopt, the identity key-certs pairs won't be updated and the
   * identity_cert_name will be ignored.
   */
  void SetKeyMaterials(std::string root_cert_name,
                       absl::optional<absl::string_view> pem_root_certs,
                       std::string identity_cert_name,
                       absl::optional<PemKeyCertPairList> pem_key_cert_pairs);
  void SetRootCerts(std::string root_cert_name,
                    absl::string_view pem_root_certs);
  void SetKeyCertPairs(std::string identity_cert_name,
                       PemKeyCertPairList pem_key_cert_pairs);
  bool HasRootCerts(const std::string& root_cert_name);
  bool HasKeyCertPairs(const std::string& identity_cert_name);
  /**
   * Set the TLS certificate watch status callback function. The
   * grpc_tls_certificate_distributor will invoke this callback when a new
   * certificate name is watched by a newly registered watcher, or when a
   * certificate name is not watched by any watchers.
   *
   * @param callback The callback function being set by the caller, e.g the
   * Producer. Note that this callback will be invoked for each certificate
   * name. If the identity certificate and root certificate's status are updated
   * and they have the same cert name, this callback will be invoked twice.
   *
   * For the parameters in the callback function:
   * string_value The name of the certificates being watched.
   * bool_value_1 If the root certificates with the specific name are
   * still being watched.
   * bool_value_2 If the identity certificates with the specific name are
   * still being watched.
   */
  void SetWatchStatusCallback(
      std::function<void(std::string, bool, bool)> callback) {
    watch_status_callback_ = callback;
  };
  /**
   * Register a watcher. The ownership of the WatcherInfo will be transferred to
   * the watchers_ field of the distributor.
   *
   * @param watcher The watcher being registered.
   * @param root_cert_name The name of the root certificates that will be
   * watched. If set to absl::nullopt, the root certificates won't be watched.
   * @param identity_cert_name The name of the identity certificates that will
   * be watched. If set to absl::nullopt, the identity certificates won't be
   * watched.
   */
  void WatchTlsCertificates(
      std::unique_ptr<TlsCertificatesWatcherInterface> watcher,
      absl::optional<std::string> root_cert_name,
      absl::optional<std::string> identity_cert_name);
  /**
   * Cancel a watcher. The ownership of the watcher should be already
   * transferred to the distributor, so a raw pointer of the watcher is needed.
   *
   * @param watcher The watcher being canceled.
   */
  void CancelTlsCertificatesWatch(TlsCertificatesWatcherInterface* watcher);

 private:
  struct WatcherInfo {
    std::unique_ptr<TlsCertificatesWatcherInterface> watcher;
    absl::optional<std::string> root_cert_name;
    absl::optional<std::string> identity_cert_name;
  };

  struct CertificateStatus {
    int root_cert_watcher_cnt;
    int identity_cert_watcher_cnt;
  };

  // Whenever key materials change, CertificateUpdated will be invoked to send
  // updates to each watcher. root_cert_name and identity_cert_name are the
  // names of the certificates being updated.
  void CertificatesUpdated(
      const absl::optional<std::string>& root_cert_name,
      const absl::optional<std::string>& identity_cert_name);

  grpc_core::Mutex mu_;
  // The field watchers_ owns the ownership of the WatcherInfo.
  std::map<TlsCertificatesWatcherInterface*, WatcherInfo> watchers_;
  // The callback to notify the caller, e.g. the Producer, that the watch status
  // is changed.
  std::function<void(std::string, bool, bool)> watch_status_callback_;
  std::map<std::string, std::string> pem_root_certs_;
  std::map<std::string, PemKeyCertPairList> pem_key_cert_pair_;
  // The field watch_status_ stores the count of root certificates and identity
  // certificates being watched for each certificate name.
  std::map<std::string, CertificateStatus> watch_status_;
};

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_DISTRIBUTOR_H \
        */
