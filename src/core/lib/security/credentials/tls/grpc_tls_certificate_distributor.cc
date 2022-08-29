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

#include <algorithm>
#include <vector>

#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

void grpc_tls_certificate_distributor::SetKeyMaterials(
    const std::string& cert_name, absl::optional<std::string> pem_root_certs,
    absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs) {
  GPR_ASSERT(pem_root_certs.has_value() || pem_key_cert_pairs.has_value());
  grpc_core::MutexLock lock(&mu_);
  auto& cert_info = certificate_info_map_[cert_name];
  if (pem_root_certs.has_value()) {
    // Successful credential updates will clear any pre-existing error.
    cert_info.SetRootError(GRPC_ERROR_NONE);
    for (auto* watcher_ptr : cert_info.root_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      GPR_ASSERT(watcher_it->second.root_cert_name.has_value());
      absl::optional<grpc_core::PemKeyCertPairList>
          pem_key_cert_pairs_to_report;
      if (pem_key_cert_pairs.has_value() &&
          watcher_it->second.identity_cert_name == cert_name) {
        pem_key_cert_pairs_to_report = pem_key_cert_pairs;
      } else if (watcher_it->second.identity_cert_name.has_value()) {
        auto& identity_cert_info =
            certificate_info_map_[*watcher_it->second.identity_cert_name];
        if (!identity_cert_info.pem_key_cert_pairs.empty()) {
          pem_key_cert_pairs_to_report = identity_cert_info.pem_key_cert_pairs;
        }
      }
      watcher_ptr->OnCertificatesChanged(
          pem_root_certs, std::move(pem_key_cert_pairs_to_report));
    }
    cert_info.pem_root_certs = std::move(*pem_root_certs);
  }
  if (pem_key_cert_pairs.has_value()) {
    // Successful credential updates will clear any pre-existing error.
    cert_info.SetIdentityError(GRPC_ERROR_NONE);
    for (const auto watcher_ptr : cert_info.identity_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      GPR_ASSERT(watcher_it->second.identity_cert_name.has_value());
      absl::optional<absl::string_view> pem_root_certs_to_report;
      if (pem_root_certs.has_value() &&
          watcher_it->second.root_cert_name == cert_name) {
        // In this case, We've already sent the credential updates at the time
        // when checking pem_root_certs, so we will skip here.
        continue;
      } else if (watcher_it->second.root_cert_name.has_value()) {
        auto& root_cert_info =
            certificate_info_map_[*watcher_it->second.root_cert_name];
        if (!root_cert_info.pem_root_certs.empty()) {
          pem_root_certs_to_report = root_cert_info.pem_root_certs;
        }
      }
      watcher_ptr->OnCertificatesChanged(pem_root_certs_to_report,
                                         pem_key_cert_pairs);
    }
    cert_info.pem_key_cert_pairs = std::move(*pem_key_cert_pairs);
  }
}

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
    const std::string& cert_name,
    absl::optional<grpc_error_handle> root_cert_error,
    absl::optional<grpc_error_handle> identity_cert_error) {
  GPR_ASSERT(root_cert_error.has_value() || identity_cert_error.has_value());
  grpc_core::MutexLock lock(&mu_);
  CertificateInfo& cert_info = certificate_info_map_[cert_name];
  if (root_cert_error.has_value()) {
    for (auto* watcher_ptr : cert_info.root_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      // identity_cert_error_to_report is the error of the identity cert this
      // watcher is watching, if there is any.
      grpc_error_handle identity_cert_error_to_report = GRPC_ERROR_NONE;
      if (identity_cert_error.has_value() &&
          watcher_it->second.identity_cert_name == cert_name) {
        identity_cert_error_to_report = *identity_cert_error;
      } else if (watcher_it->second.identity_cert_name.has_value()) {
        auto& identity_cert_info =
            certificate_info_map_[*watcher_it->second.identity_cert_name];
        identity_cert_error_to_report = identity_cert_info.identity_cert_error;
      }
      watcher_ptr->OnError(GRPC_ERROR_REF(*root_cert_error),
                           GRPC_ERROR_REF(identity_cert_error_to_report));
    }
    cert_info.SetRootError(*root_cert_error);
  }
  if (identity_cert_error.has_value()) {
    for (auto* watcher_ptr : cert_info.identity_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers_.end());
      // root_cert_error_to_report is the error of the root cert this watcher is
      // watching, if there is any.
      grpc_error_handle root_cert_error_to_report = GRPC_ERROR_NONE;
      if (root_cert_error.has_value() &&
          watcher_it->second.root_cert_name == cert_name) {
        // In this case, We've already sent the error updates at the time when
        // checking root_cert_error, so we will skip here.
        continue;
      } else if (watcher_it->second.root_cert_name.has_value()) {
        auto& root_cert_info =
            certificate_info_map_[*watcher_it->second.root_cert_name];
        root_cert_error_to_report = root_cert_info.root_cert_error;
      }
      watcher_ptr->OnError(GRPC_ERROR_REF(root_cert_error_to_report),
                           GRPC_ERROR_REF(*identity_cert_error));
    }
    cert_info.SetIdentityError(*identity_cert_error);
  }
};

void grpc_tls_certificate_distributor::SetError(grpc_error_handle error) {
  GPR_ASSERT(!GRPC_ERROR_IS_NONE(error));
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
    absl::optional<grpc_core::PemKeyCertPairList> updated_identity_pairs;
    grpc_error_handle root_error = GRPC_ERROR_NONE;
    grpc_error_handle identity_error = GRPC_ERROR_NONE;
    if (root_cert_name.has_value()) {
      CertificateInfo& cert_info = certificate_info_map_[*root_cert_name];
      start_watching_root_cert = cert_info.root_cert_watchers.empty();
      already_watching_identity_for_root_cert =
          !cert_info.identity_cert_watchers.empty();
      cert_info.root_cert_watchers.insert(watcher_ptr);
      root_error = GRPC_ERROR_REF(cert_info.root_cert_error);
      // Empty credentials will be treated as no updates.
      if (!cert_info.pem_root_certs.empty()) {
        updated_root_certs = cert_info.pem_root_certs;
      }
    }
    if (identity_cert_name.has_value()) {
      CertificateInfo& cert_info = certificate_info_map_[*identity_cert_name];
      start_watching_identity_cert = cert_info.identity_cert_watchers.empty();
      already_watching_root_for_identity_cert =
          !cert_info.root_cert_watchers.empty();
      cert_info.identity_cert_watchers.insert(watcher_ptr);
      identity_error = GRPC_ERROR_REF(cert_info.identity_cert_error);
      // Empty credentials will be treated as no updates.
      if (!cert_info.pem_key_cert_pairs.empty()) {
        updated_identity_pairs = cert_info.pem_key_cert_pairs;
      }
    }
    // Notify this watcher if the certs it is watching already had some
    // contents. Note that an *_cert_error in cert_info only indicates error
    // occurred while trying to fetch the latest cert, but the updated_*_certs
    // should always be valid. So we will send the updates regardless of
    // *_cert_error.
    if (updated_root_certs.has_value() || updated_identity_pairs.has_value()) {
      watcher_ptr->OnCertificatesChanged(updated_root_certs,
                                         std::move(updated_identity_pairs));
    }
    // Notify this watcher if the certs it is watching already had some errors.
    if (!GRPC_ERROR_IS_NONE(root_error) ||
        !GRPC_ERROR_IS_NONE(identity_error)) {
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

/** -- Wrapper APIs declared in grpc_security.h -- **/

grpc_tls_identity_pairs* grpc_tls_identity_pairs_create() {
  return new grpc_tls_identity_pairs();
}

void grpc_tls_identity_pairs_add_pair(grpc_tls_identity_pairs* pairs,
                                      const char* private_key,
                                      const char* cert_chain) {
  GPR_ASSERT(pairs != nullptr);
  GPR_ASSERT(private_key != nullptr);
  GPR_ASSERT(cert_chain != nullptr);
  pairs->pem_key_cert_pairs.emplace_back(private_key, cert_chain);
}

void grpc_tls_identity_pairs_destroy(grpc_tls_identity_pairs* pairs) {
  GPR_ASSERT(pairs != nullptr);
  delete pairs;
}
