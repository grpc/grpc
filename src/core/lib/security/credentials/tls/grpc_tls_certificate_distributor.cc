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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <stdlib.h>
#include <string.h>

void grpc_tls_certificate_distributor::SetKeyMaterials(
    const std::string& cert_name, absl::string_view pem_root_certs,
    PemKeyCertPairList pem_key_cert_pairs) {
  grpc_core::MutexLock lock(&mu_);
  auto& cert_info = certificate_info_map_[cert_name];
  cert_info.pem_root_certs = pem_root_certs;
  cert_info.pem_key_cert_pairs = std::move(pem_key_cert_pairs);
  CertificatesUpdatedLocked(cert_name, true, true, cert_info);
}

void grpc_tls_certificate_distributor::SetRootCerts(
    const std::string& root_cert_name, absl::string_view pem_root_certs) {
  grpc_core::MutexLock lock(&mu_);
  auto& cert_info = certificate_info_map_[root_cert_name];
  cert_info.pem_root_certs = pem_root_certs;
  CertificatesUpdatedLocked(root_cert_name, true, false, cert_info);
};

void grpc_tls_certificate_distributor::SetKeyCertPairs(
    const std::string& identity_cert_name,
    PemKeyCertPairList pem_key_cert_pairs) {
  grpc_core::MutexLock lock(&mu_);
  auto& cert_info = certificate_info_map_[identity_cert_name];
  cert_info.pem_key_cert_pairs = std::move(pem_key_cert_pairs);
  CertificatesUpdatedLocked(identity_cert_name, false, true, cert_info);
};

bool grpc_tls_certificate_distributor::HasRootCerts(
    const std::string& root_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  const auto it = certificate_info_map_.find(root_cert_name);
  return it != certificate_info_map_.end() &&
         !it->second.pem_root_certs.empty();
};

bool grpc_tls_certificate_distributor::HasKeyCertPairs(
    const std::string& identity_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  const auto it = certificate_info_map_.find(identity_cert_name);
  return it != certificate_info_map_.end() &&
         !it->second.pem_key_cert_pairs.empty();
};

void grpc_tls_certificate_distributor::SetErrorForCert(
    const std::string& cert_name, grpc_error* root_cert_error,
    grpc_error* identity_cert_error) {
  GPR_ASSERT(root_cert_error != GRPC_ERROR_NONE ||
             identity_cert_error != GRPC_ERROR_NONE);
  grpc_core::MutexLock lock(&mu_);
  CertificateInfo& cert_info = certificate_info_map_[cert_name];
  if (root_cert_error != GRPC_ERROR_NONE) {
    for (auto* watcher_ptr : cert_info.root_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      // identity_cert_error_to_report is the error of the identity cert this
      // watcher is watching, if there is any.
      grpc_error* identity_cert_error_to_report = GRPC_ERROR_NONE;
      if (identity_cert_error != GRPC_ERROR_NONE &&
          watcher_it->second.identity_cert_name == cert_name) {
        identity_cert_error_to_report = identity_cert_error;
      } else if (watcher_it->second.identity_cert_name.has_value()) {
        auto& identity_cert_info =
            certificate_info_map_[*watcher_it->second.identity_cert_name];
        identity_cert_error_to_report = identity_cert_info.identity_cert_error;
      }
      watcher_ptr->OnError(GRPC_ERROR_REF(root_cert_error),
                           GRPC_ERROR_REF(identity_cert_error_to_report));
    }
  }
  if (identity_cert_error != GRPC_ERROR_NONE) {
    for (auto* watcher_ptr : cert_info.identity_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      // root_cert_error_to_report is the error of the root cert this watcher is
      // watching, if there is any.
      grpc_error* root_cert_error_to_report = GRPC_ERROR_NONE;
      if (root_cert_error != GRPC_ERROR_NONE &&
          watcher_it->second.root_cert_name == cert_name) {
        continue;
      } else if (watcher_it->second.root_cert_name.has_value()) {
        auto& root_cert_info =
            certificate_info_map_[*watcher_it->second.root_cert_name];
        root_cert_error_to_report = root_cert_info.root_cert_error;
      }
      watcher_ptr->OnError(GRPC_ERROR_REF(root_cert_error_to_report),
                           GRPC_ERROR_REF(identity_cert_error));
    }
  }
  cert_info.SetRootError(root_cert_error);
  cert_info.SetIdentityError(identity_cert_error);
};

void grpc_tls_certificate_distributor::SetError(grpc_error* error) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  grpc_core::MutexLock lock(&mu_);
  for (const auto& watcher : watchers_) {
    const auto watcher_ptr = watcher.first;
    GPR_ASSERT(watcher_ptr != nullptr);
    const auto& watcher_info = watcher.second;
    watcher_ptr->OnError(
        watcher_info.root_cert_name.has_value() ? GRPC_ERROR_REF(error)
                                                : GRPC_ERROR_NONE,
        watcher_info.identity_cert_name.has_value() ? GRPC_ERROR_REF(error)
                                                    : GRPC_ERROR_NONE);
  }
  for (auto& cert_info_entry : certificate_info_map_) {
    auto& cert_info = cert_info_entry.second;
    cert_info.SetRootError(GRPC_ERROR_REF(error));
    cert_info.SetIdentityError(GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
};

void grpc_tls_certificate_distributor::WatchTlsCertificates(
    std::unique_ptr<TlsCertificatesWatcherInterface> watcher,
    absl::optional<std::string> root_cert_name,
    absl::optional<std::string> identity_cert_name) {
  bool start_watching_root_cert = false;
  bool already_watching_identity_for_root_cert = false;
  bool start_watching_identity_cert = false;
  bool already_watching_root_for_identity_cert = false;
  GPR_ASSERT(root_cert_name.has_value() || identity_cert_name.has_value());
  TlsCertificatesWatcherInterface* watcher_ptr = watcher.get();
  GPR_ASSERT(watcher_ptr != nullptr);
  // Update watchers_ and certificate_info_map_.
  {
    grpc_core::MutexLock lock(&mu_);
    const auto watcher_it = watchers_.find(watcher_ptr);
    // The caller needs to cancel the watcher first if it wants to re-register
    // the watcher.
    GPR_ASSERT(watcher_it == watchers_.end());
    watchers_[watcher_ptr] = {std::move(watcher), root_cert_name,
                              identity_cert_name};
    absl::optional<absl::string_view> updated_root_certs;
    absl::optional<PemKeyCertPairList> updated_identity_pairs;
    grpc_error* root_error = GRPC_ERROR_NONE;
    grpc_error* identity_error = GRPC_ERROR_NONE;
    if (root_cert_name.has_value()) {
      CertificateInfo& cert_info = certificate_info_map_[*root_cert_name];
      start_watching_root_cert = cert_info.root_cert_watchers.empty();
      already_watching_identity_for_root_cert =
          !cert_info.identity_cert_watchers.empty();
      cert_info.root_cert_watchers.insert(watcher_ptr);
      root_error = GRPC_ERROR_REF(cert_info.root_cert_error);
      updated_root_certs = cert_info.pem_root_certs;
    }
    if (identity_cert_name.has_value()) {
      CertificateInfo& cert_info = certificate_info_map_[*identity_cert_name];
      start_watching_identity_cert = cert_info.identity_cert_watchers.empty();
      already_watching_root_for_identity_cert =
          !cert_info.root_cert_watchers.empty();
      cert_info.identity_cert_watchers.insert(watcher_ptr);
      identity_error = GRPC_ERROR_REF(cert_info.identity_cert_error);
      updated_identity_pairs = cert_info.pem_key_cert_pairs;
    }
    // Notify this watcher if the certs it is watching already had some
    // contents. Note that an *_cert_error in cert_info only indicates error
    // occurred while trying to fetch the latest cert, but the updated_*_certs
    // should always be valid. So we will send the updates regardless of
    // *_cert_error.
    watcher_ptr->OnCertificatesChanged(updated_root_certs,
                                       std::move(updated_identity_pairs));
    // Notify this watcher if the certs it is watching already had some errors.
    if (root_error != GRPC_ERROR_NONE || identity_error != GRPC_ERROR_NONE) {
      watcher_ptr->OnError(GRPC_ERROR_REF(root_error),
                           GRPC_ERROR_REF(identity_error));
    }
    GRPC_ERROR_UNREF(root_error);
    GRPC_ERROR_UNREF(identity_error);
  }
  // Invoke watch status callback if needed.
  {
    grpc_core::MutexLock lock(&callback_mu_);
    if (watch_status_callback_ != nullptr) {
      if (root_cert_name == identity_cert_name &&
          (start_watching_root_cert || start_watching_identity_cert)) {
        watch_status_callback_(*root_cert_name, start_watching_root_cert,
                               start_watching_identity_cert);
      } else {
        if (start_watching_root_cert) {
          watch_status_callback_(*root_cert_name, true,
                                 already_watching_identity_for_root_cert);
        }
        if (start_watching_identity_cert) {
          watch_status_callback_(*identity_cert_name,
                                 already_watching_root_for_identity_cert, true);
        }
      }
    }
  }
};

void grpc_tls_certificate_distributor::CancelTlsCertificatesWatch(
    TlsCertificatesWatcherInterface* watcher) {
  absl::optional<std::string> root_cert_name;
  absl::optional<std::string> identity_cert_name;
  bool stop_watching_root_cert = false;
  bool already_watching_identity_for_root_cert = false;
  bool stop_watching_identity_cert = false;
  bool already_watching_root_for_identity_cert = false;
  // Update watchers_ and certificate_info_map_.
  {
    grpc_core::MutexLock lock(&mu_);
    auto it = watchers_.find(watcher);
    if (it == watchers_.end()) return;
    WatcherInfo& watcher_info = it->second;
    root_cert_name = std::move(watcher_info.root_cert_name);
    identity_cert_name = std::move(watcher_info.identity_cert_name);
    watchers_.erase(it);
    if (root_cert_name.has_value()) {
      auto it = certificate_info_map_.find(*root_cert_name);
      GPR_ASSERT(it != certificate_info_map_.end());
      CertificateInfo& cert_info = it->second;
      cert_info.root_cert_watchers.erase(watcher);
      stop_watching_root_cert = cert_info.root_cert_watchers.empty();
      already_watching_identity_for_root_cert =
          !cert_info.identity_cert_watchers.empty();
      if (stop_watching_root_cert && !already_watching_identity_for_root_cert) {
        certificate_info_map_.erase(it);
      }
    }
    if (identity_cert_name.has_value()) {
      auto it = certificate_info_map_.find(*identity_cert_name);
      GPR_ASSERT(it != certificate_info_map_.end());
      CertificateInfo& cert_info = it->second;
      cert_info.identity_cert_watchers.erase(watcher);
      stop_watching_identity_cert = cert_info.identity_cert_watchers.empty();
      already_watching_root_for_identity_cert =
          !cert_info.root_cert_watchers.empty();
      if (stop_watching_identity_cert &&
          !already_watching_root_for_identity_cert) {
        certificate_info_map_.erase(it);
      }
    }
  }
  // Invoke watch status callback if needed.
  {
    grpc_core::MutexLock lock(&callback_mu_);
    if (watch_status_callback_ != nullptr) {
      if (root_cert_name == identity_cert_name &&
          (stop_watching_root_cert || stop_watching_identity_cert)) {
        watch_status_callback_(*root_cert_name, !stop_watching_root_cert,
                               !stop_watching_identity_cert);
      } else {
        if (stop_watching_root_cert) {
          watch_status_callback_(*root_cert_name, false,
                                 already_watching_identity_for_root_cert);
        }
        if (stop_watching_identity_cert) {
          watch_status_callback_(*identity_cert_name,
                                 already_watching_root_for_identity_cert,
                                 false);
        }
      }
    }
  }
};

void grpc_tls_certificate_distributor::CertificatesUpdatedLocked(
    const std::string& cert_name, bool root_cert_changed,
    bool identity_cert_changed, CertificateInfo& cert_info) {
  // Go through each affected watchers and invoke OnCertificatesChanged.
  if (root_cert_changed) {
    cert_info.SetRootError(GRPC_ERROR_NONE);
    for (const auto watcher_ptr : cert_info.root_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      GPR_ASSERT(watcher_it->second.root_cert_name.has_value());
      absl::optional<PemKeyCertPairList> pem_key_cert_pairs;
      if (identity_cert_changed &&
          watcher_it->second.identity_cert_name == cert_name) {
        pem_key_cert_pairs = cert_info.pem_key_cert_pairs;
      } else if (watcher_it->second.identity_cert_name.has_value()) {
        auto& identity_cert_info =
            certificate_info_map_[*watcher_it->second.identity_cert_name];
        pem_key_cert_pairs = identity_cert_info.pem_key_cert_pairs;
      }
      watcher_ptr->OnCertificatesChanged(cert_info.pem_root_certs,
                                         std::move(pem_key_cert_pairs));
    }
  }
  if (identity_cert_changed) {
    cert_info.SetIdentityError(GRPC_ERROR_NONE);
    for (const auto watcher_ptr : cert_info.identity_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      GPR_ASSERT(watcher_it->second.identity_cert_name.has_value());
      absl::optional<absl::string_view> pem_root_certs;
      if (root_cert_changed && watcher_it->second.root_cert_name == cert_name) {
        continue;
      } else if (watcher_it->second.root_cert_name.has_value()) {
        auto& root_cert_info =
            certificate_info_map_[*watcher_it->second.root_cert_name];
        pem_root_certs = root_cert_info.pem_root_certs;
      }
      watcher_ptr->OnCertificatesChanged(pem_root_certs,
                                         cert_info.pem_key_cert_pairs);
    }
  }
};
