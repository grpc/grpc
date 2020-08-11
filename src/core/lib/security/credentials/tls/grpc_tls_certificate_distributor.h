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

// TLS certificate distributor.
struct grpc_tls_certificate_distributor
    : public grpc_core::RefCounted<grpc_tls_certificate_distributor> {
 public:
  typedef absl::InlinedVector<grpc_core::PemKeyCertPair, 1> PemKeyCertPairList;

  // Interface for watching TLS certificates update.
  class TlsCertificatesWatcherInterface {
   public:
    virtual ~TlsCertificatesWatcherInterface() = default;

    // Handles the delivery of the updated root and identity certificates.
    // Setting to absl::nullopt indicates no corresponding contents for
    // root_certs or key_cert_pairs. Note that we will send updates of the
    // latest contents on both root and identity certificates, even when only
    // one side of it got updated.
    //
    // @param root_certs the contents of the reloaded root certs.
    // @param key_cert_pairs the contents of the reloaded identity key-cert
    // pairs.
    virtual void OnCertificatesChanged(
        absl::optional<absl::string_view> root_certs,
        absl::optional<PemKeyCertPairList> key_cert_pairs) = 0;

    // Handles an error that occurs while attempting to fetch certificate data.
    //
    // @param error the error occurred while reloading.
    virtual void OnError(grpc_error* error) = 0;
  };

  // Set the key materials based on their certificate name. Note that we are
  // not doing any copies for pem_root_certs and pem_key_cert_pairs. For
  // pem_root_certs, the original string contents need to outlive the
  // distributor; for pem_key_cert_pairs, internally it is taking two
  // unique_ptr(s) to the credential string, so the ownership is actually
  // transferred.
  //
  // @param cert_name The name of the certificates being updated.
  // @param pem_root_certs The content of root certificates.
  // @param pem_key_cert_pairs The content of identity key-cert pairs.
  void SetKeyMaterials(const std::string& cert_name,
                       absl::string_view pem_root_certs,
                       PemKeyCertPairList pem_key_cert_pairs);

  void SetRootCerts(const std::string& root_cert_name,
                    absl::string_view pem_root_certs);

  void SetKeyCertPairs(const std::string& identity_cert_name,
                       PemKeyCertPairList pem_key_cert_pairs);

  bool HasRootCerts(const std::string& root_cert_name);

  bool HasKeyCertPairs(const std::string& identity_cert_name);

  // Set the TLS certificate watch status callback function. The
  // grpc_tls_certificate_distributor will invoke this callback when a new
  // certificate name is watched by a newly registered watcher, or when a
  // certificate name is not watched by any watchers.
  //
  // @param callback The callback function being set by the caller, e.g the
  // Producer. Note that this callback will be invoked for each certificate
  // name. If the identity certificate and root certificate's name are same and
  // they both got updated for the first time, this callback will only be
  // invoked once.
  //
  // For the parameters in the callback function:
  // string_value The name of the certificates being watched.
  // bool_value_1 If the root certificates with the specific name are
  // still being watched.
  // bool_value_2 If the identity certificates with the specific name are
  // still being watched.
  void SetWatchStatusCallback(
      std::function<void(std::string, bool, bool)> callback) {
    grpc_core::MutexLock lock(&mu_);
    watch_status_callback_ = callback;
  };

  // Register a watcher. The ownership of the WatcherInfo will be transferred to
  // the watchers_ field of the distributor, but we still need a raw pointer to
  // cancel the watcher. So callers need to store the raw pointer somewhere.
  //
  // @param watcher The watcher being registered.
  // @param root_cert_name The name of the root certificates that will be
  // watched. If set to absl::nullopt, the root certificates won't be watched.
  // @param identity_cert_name The name of the identity certificates that will
  // be watched. If set to absl::nullopt, the identity certificates won't be
  // watched.
  void WatchTlsCertificates(
      std::unique_ptr<TlsCertificatesWatcherInterface> watcher,
      absl::optional<std::string> root_cert_name,
      absl::optional<std::string> identity_cert_name);

  // Cancel a watcher.
  //
  // @param watcher The watcher being canceled.
  void CancelTlsCertificatesWatch(TlsCertificatesWatcherInterface* watcher);

  // Propagate the error that the caller (e.g. Producer) encounters to all the
  // watchers watching a particular certificate name.
  //
  // @param cert_name The watching cert name of the watchers that the caller
  // wants to notify when encountering error.
  // @param error The error that the caller encounters.
  // @param root_cert_error If propagating errors to watchers watching cert_name
  // as root certificate name.
  // @param identity_cert_error If propagating errors to watchers watching
  // cert_name as identity certificate name.
  void SendErrorToWatchers(const std::string& cert_name, grpc_error* error,
                           bool root_cert_error, bool identity_cert_error);

  // Propagate the error that the caller (e.g. Producer) encounters to all
  // watchers.
  //
  // @param error The error that the caller encounters.
  void SendErrorToWatchers(grpc_error* error);

 private:
  // Contains the information about each watcher.
  struct WatcherInfo {
    std::unique_ptr<TlsCertificatesWatcherInterface> watcher;
    absl::optional<std::string> root_cert_name;
    absl::optional<std::string> identity_cert_name;
  };
  // CertificateInfo contains the credential contents and some additional
  // watcher information.
  struct CertificateInfo {
    // CertificateUpdated will be invoked to send updates to each of the
    // watchers in root_cert_watchers and identity_cert_watchers whenever
    // pem_root_certs or pem_key_cert_pair changes.
    //
    // @param cert_name The name of the certificates being changed.
    // @param root_cert_changed If the root cert with name "cert_name" changed.
    // @param identity_cert_changed If the identity cert with name "cert_name"
    // changed.
    // @param watchers A reference to watchers_.
    // @param certificate_info_map A reference to certificate_info_map_.
    void CertificatesUpdated(
        const std::string& cert_name, bool root_cert_changed,
        bool identity_cert_changed,
        const std::map<TlsCertificatesWatcherInterface*, WatcherInfo>& watchers,
        const std::map<std::string, CertificateInfo>& certificate_info_map);

    // The contents of the root certificates.
    absl::string_view pem_root_certs;
    // The contents of the identity key-certificate pairs.
    PemKeyCertPairList pem_key_cert_pairs;
    // The count of watchers watching root certificates(of that particular
    // name).
    int root_cert_watcher_count = 0;
    // The count of watchers watching identity certificates(of that particular
    // name).
    int identity_cert_watcher_count = 0;
    // The set of watchers watching root certificates(of that particular name).
    // This is mainly used for quickly looking up the affected watchers while
    // performing a credential reloading.
    std::set<TlsCertificatesWatcherInterface*> root_cert_watchers;
    // The set of watchers watching identity certificates(of that particular
    // name). This is mainly used for quickly looking up the affected watchers
    // while performing a credential reloading.
    std::set<TlsCertificatesWatcherInterface*> identity_cert_watchers;
  };

  grpc_core::Mutex mu_;
  // We need a dedicated mutex for watch_status_callback_ for allowing
  // callers(e.g. Producer) to directly set key materials in the callback
  // functions.
  grpc_core::Mutex callback_mu_;
  // Stores information about each watcher.
  std::map<TlsCertificatesWatcherInterface*, WatcherInfo> watchers_;
  // The callback to notify the caller, e.g. the Producer, that the watch status
  // is changed.
  std::function<void(std::string, bool, bool)> watch_status_callback_;
  // Stores the names of each certificate, and their corresponding credential
  // contents as well as some additional watcher information.
  std::map<std::string, CertificateInfo> certificate_info_map_;
};

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_DISTRIBUTOR_H
