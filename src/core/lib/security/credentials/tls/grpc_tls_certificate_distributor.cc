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
  cert_info.CertificatesUpdated(cert_name, true, true, watchers_,
                                certificate_info_map_);
}

void grpc_tls_certificate_distributor::SetRootCerts(
    const std::string& root_cert_name, absl::string_view pem_root_certs) {
  grpc_core::MutexLock lock(&mu_);
  auto& cert_info = certificate_info_map_[root_cert_name];
  cert_info.pem_root_certs = pem_root_certs;
  cert_info.CertificatesUpdated(root_cert_name, true, false, watchers_,
                                certificate_info_map_);
};

void grpc_tls_certificate_distributor::SetKeyCertPairs(
    const std::string& identity_cert_name,
    PemKeyCertPairList pem_key_cert_pairs) {
  grpc_core::MutexLock lock(&mu_);
  auto& cert_info = certificate_info_map_[identity_cert_name];
  cert_info.pem_key_cert_pairs = std::move(pem_key_cert_pairs);
  cert_info.CertificatesUpdated(identity_cert_name, false, true, watchers_,
                                certificate_info_map_);
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
         it->second.pem_key_cert_pairs.size() != 0;
};

void grpc_tls_certificate_distributor::WatchTlsCertificates(
    std::unique_ptr<TlsCertificatesWatcherInterface> watcher,
    absl::optional<std::string> root_cert_name,
    absl::optional<std::string> identity_cert_name) {
  grpc_core::ReleasableMutexLock lock(&mu_);
  TlsCertificatesWatcherInterface* watcher_ptr = watcher.get();
  GPR_ASSERT(watcher_ptr != nullptr);
  GPR_ASSERT(root_cert_name.has_value() || identity_cert_name.has_value());
  watchers_[watcher_ptr] = {std::move(watcher), root_cert_name,
                            identity_cert_name};
  if (root_cert_name.has_value() && identity_cert_name.has_value() &&
      *root_cert_name == *identity_cert_name) {
    const std::string& cert_name = *root_cert_name;
    CertificateInfo& cert_info = certificate_info_map_[cert_name];
    cert_info.root_cert_watchers.insert(watcher_ptr);
    cert_info.identity_cert_watchers.insert(watcher_ptr);
    ++cert_info.root_cert_watcher_count;
    ++cert_info.identity_cert_watcher_count;
    // Only invoke the callback when any of the root cert and identity cert is
    // updated for the first time.
    if (cert_info.root_cert_watcher_count == 1 ||
        cert_info.identity_cert_watcher_count == 1) {
      lock.Unlock();
      grpc_core::ReleasableMutexLock callback_lock(&callback_mu_);
      if (watch_status_callback_ != nullptr) {
        watch_status_callback_(cert_name, true, true);
      }
      callback_lock.Unlock();
      lock.Lock();
    }
    return;
  }
  if (root_cert_name.has_value()) {
    CertificateInfo& root_cert_info = certificate_info_map_[*root_cert_name];
    root_cert_info.root_cert_watchers.insert(watcher_ptr);
    ++root_cert_info.root_cert_watcher_count;
    if (root_cert_info.root_cert_watcher_count == 1) {
      lock.Unlock();
      grpc_core::ReleasableMutexLock callback_lock(&callback_mu_);
      if (watch_status_callback_ != nullptr) {
        watch_status_callback_(*root_cert_name, true,
                               root_cert_info.identity_cert_watcher_count > 0);
      }
      callback_lock.Unlock();
      lock.Lock();
    }
  }
  if (identity_cert_name.has_value()) {
    CertificateInfo& identity_cert_info =
        certificate_info_map_[*identity_cert_name];
    identity_cert_info.identity_cert_watchers.insert(watcher_ptr);
    ++identity_cert_info.identity_cert_watcher_count;
    if (identity_cert_info.identity_cert_watcher_count == 1) {
      lock.Unlock();
      grpc_core::ReleasableMutexLock callback_lock(&callback_mu_);
      if (watch_status_callback_ != nullptr) {
        watch_status_callback_(*identity_cert_name,
                               identity_cert_info.root_cert_watcher_count > 0,
                               true);
      }
      callback_lock.Unlock();
      lock.Lock();
    }
  }
};

void grpc_tls_certificate_distributor::CancelTlsCertificatesWatch(
    TlsCertificatesWatcherInterface* watcher) {
  grpc_core::ReleasableMutexLock lock(&mu_);
  const auto watcher_it = watchers_.find(watcher);
  if (watcher_it == watchers_.end()) {
    return;
  }
  WatcherInfo& watcher_info = watcher_it->second;
  if (watcher_info.root_cert_name.has_value() &&
      watcher_info.identity_cert_name.has_value() &&
      *watcher_info.root_cert_name == *watcher_info.identity_cert_name) {
    const std::string& cert_name = *watcher_info.root_cert_name;
    const auto it = certificate_info_map_.find(cert_name);
    GPR_ASSERT(it != certificate_info_map_.end());
    CertificateInfo& cert_info = it->second;
    GPR_ASSERT(cert_info.root_cert_watchers.find(watcher) !=
               cert_info.root_cert_watchers.end());
    GPR_ASSERT(cert_info.identity_cert_watchers.find(watcher) !=
               cert_info.identity_cert_watchers.end());
    cert_info.root_cert_watchers.erase(watcher);
    cert_info.identity_cert_watchers.erase(watcher);
    GPR_ASSERT(cert_info.root_cert_watcher_count > 0);
    --cert_info.root_cert_watcher_count;
    GPR_ASSERT(cert_info.identity_cert_watcher_count > 0);
    --cert_info.identity_cert_watcher_count;
    if (cert_info.root_cert_watcher_count == 0 ||
        cert_info.identity_cert_watcher_count == 0) {
      lock.Unlock();
      grpc_core::ReleasableMutexLock callback_lock(&callback_mu_);
      if (watch_status_callback_ != nullptr) {
        watch_status_callback_(cert_name, cert_info.root_cert_watcher_count > 0,
                               cert_info.identity_cert_watcher_count > 0);
      }
      callback_lock.Unlock();
      lock.Lock();
    }
    if (cert_info.root_cert_watcher_count == 0 &&
        cert_info.identity_cert_watcher_count == 0) {
      certificate_info_map_.erase(cert_name);
    }
    return;
  }
  if (watcher_info.root_cert_name.has_value()) {
    const std::string& root_cert_name = *(watcher_info.root_cert_name);
    const auto it = certificate_info_map_.find(root_cert_name);
    GPR_ASSERT(it != certificate_info_map_.end());
    CertificateInfo& cert_info = it->second;
    GPR_ASSERT(cert_info.root_cert_watchers.find(watcher) !=
               cert_info.root_cert_watchers.end());
    cert_info.root_cert_watchers.erase(watcher);
    GPR_ASSERT(cert_info.root_cert_watcher_count > 0);
    --cert_info.root_cert_watcher_count;
    if (cert_info.root_cert_watcher_count == 0) {
      lock.Unlock();
      grpc_core::ReleasableMutexLock callback_lock(&callback_mu_);
      if (watch_status_callback_ != nullptr) {
        watch_status_callback_(root_cert_name, false,
                               cert_info.identity_cert_watcher_count > 0);
      }
      callback_lock.Unlock();
      lock.Lock();
    }
    if (cert_info.root_cert_watcher_count == 0 &&
        cert_info.identity_cert_watcher_count == 0) {
      certificate_info_map_.erase(root_cert_name);
    }
  }
  if (watcher_info.identity_cert_name.has_value()) {
    const std::string& identity_cert_name = *(watcher_info.identity_cert_name);
    const auto it = certificate_info_map_.find(identity_cert_name);
    GPR_ASSERT(it != certificate_info_map_.end());
    CertificateInfo& cert_info = it->second;
    GPR_ASSERT(cert_info.identity_cert_watchers.find(watcher) !=
               cert_info.identity_cert_watchers.end());
    cert_info.identity_cert_watchers.erase(watcher);
    GPR_ASSERT(cert_info.identity_cert_watcher_count > 0);
    --cert_info.identity_cert_watcher_count;
    if (cert_info.identity_cert_watcher_count == 0) {
      lock.Unlock();
      grpc_core::ReleasableMutexLock callback_lock(&callback_mu_);
      if (watch_status_callback_ != nullptr) {
        watch_status_callback_(identity_cert_name,
                               cert_info.root_cert_watcher_count > 0, false);
      }
      callback_lock.Unlock();
      lock.Lock();
    }
    if (cert_info.root_cert_watcher_count == 0 &&
        cert_info.identity_cert_watcher_count == 0) {
      certificate_info_map_.erase(identity_cert_name);
    }
  }
  watchers_.erase(watcher);
};

void grpc_tls_certificate_distributor::SendErrorToWatchers(
    const std::string& cert_name, grpc_error* error, bool root_cert_error,
    bool identity_cert_error) {
  GPR_ASSERT(root_cert_error || identity_cert_error);
  GPR_ASSERT(error != nullptr);
  const auto it = certificate_info_map_.find(cert_name);
  if (it != certificate_info_map_.end()) {
    CertificateInfo& cert_info = it->second;
    if (root_cert_error) {
      for (const auto watcher_ptr : cert_info.root_cert_watchers) {
        watcher_ptr->OnError(GRPC_ERROR_REF(error));
      }
    }
    if (identity_cert_error) {
      for (const auto watcher_ptr : cert_info.identity_cert_watchers) {
        const auto watcher_it = watchers_.find(watcher_ptr);
        // If the watcher's root cert name is also cert_name, we already invoked
        // the watcher's OnError when checking root_cert_error, so we will skip
        // here.
        if (watcher_it != watchers_.end() &&
            watcher_it->second.root_cert_name.has_value() &&
            *watcher_it->second.root_cert_name == cert_name) {
          continue;
        }
        watcher_ptr->OnError(GRPC_ERROR_REF(error));
      }
    }
    GRPC_ERROR_UNREF(error);
  }
};

void grpc_tls_certificate_distributor::SendErrorToWatchers(grpc_error* error) {
  GPR_ASSERT(error != nullptr);
  for (const auto& watcher : watchers_) {
    const auto watcher_ptr = watcher.first;
    GPR_ASSERT(watcher_ptr != nullptr);
    watcher_ptr->OnError(GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
};

void grpc_tls_certificate_distributor::CertificateInfo::CertificatesUpdated(
    const std::string& cert_name, bool root_cert_changed,
    bool identity_cert_changed,
    const std::map<TlsCertificatesWatcherInterface*, WatcherInfo>& watchers,
    const std::map<std::string, CertificateInfo>& certificate_info_map) {
  GPR_ASSERT(root_cert_changed || identity_cert_changed);
  // Go through each affected watchers and invoke OnCertificatesChanged.
  if (root_cert_changed) {
    for (const auto watcher_ptr : root_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers.end());
      GPR_ASSERT(watcher_it->second.root_cert_name.has_value());
      if (watcher_it->second.identity_cert_name.has_value()) {
        // Check if the identity certs this watcher is watching are just the
        // ones we are updating.
        if (identity_cert_changed &&
            cert_name == *watcher_it->second.identity_cert_name) {
          watcher_ptr->OnCertificatesChanged(pem_root_certs,
                                             pem_key_cert_pairs);
          continue;
        }
        // Find the contents of the identity certs this watcher is watching,
        // if there is any.
        const auto cert_info_map_it =
            certificate_info_map.find(*watcher_it->second.identity_cert_name);
        if (cert_info_map_it != certificate_info_map.end()) {
          watcher_ptr->OnCertificatesChanged(
              pem_root_certs, cert_info_map_it->second.pem_key_cert_pairs);
          continue;
        }
      }
      watcher_ptr->OnCertificatesChanged(pem_root_certs, absl::nullopt);
    }
  }
  if (identity_cert_changed) {
    for (const auto watcher_ptr : identity_cert_watchers) {
      GPR_ASSERT(watcher_ptr != nullptr);
      const auto watcher_it = watchers.find(watcher_ptr);
      GPR_ASSERT(watcher_it != watchers.end());
      GPR_ASSERT(watcher_it->second.identity_cert_name.has_value());
      if (watcher_it->second.root_cert_name.has_value()) {
        // If the root certs this watcher is watching are just the one we are
        // updating, we already invoked OnCertificatesChanged when checking
        // root_cert_name, so we will skip here.
        if (root_cert_changed &&
            cert_name == *watcher_it->second.root_cert_name) {
          continue;
        }
        // Find the contents of the root certs this watcher is watching, if
        // there is any.
        const auto cert_info_map_it =
            certificate_info_map.find(*watcher_it->second.root_cert_name);
        if (cert_info_map_it != certificate_info_map.end()) {
          watcher_ptr->OnCertificatesChanged(
              cert_info_map_it->second.pem_root_certs, pem_key_cert_pairs);
          continue;
        }
      }
      watcher_ptr->OnCertificatesChanged(absl::nullopt, pem_key_cert_pairs);
    }
  }
};
