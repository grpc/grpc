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
    std::string root_cert_name,
    absl::optional<absl::string_view> pem_root_certs,
    std::string identity_cert_name,
    absl::optional<PemKeyCertPairList> pem_key_cert_pairs) {
  grpc_core::MutexLock lock(&mu_);
  if (!pem_root_certs.has_value() && !pem_key_cert_pairs.has_value()) {
    return;
  }
  absl::optional<std::string> updated_root_cert_name;
  absl::optional<std::string> updated_identity_cert_name;
  if (pem_root_certs.has_value()) {
    store_[root_cert_name].pem_root_certs = *pem_root_certs;
    updated_root_cert_name = absl::make_optional(root_cert_name);
  }
  if (pem_key_cert_pairs.has_value()) {
    store_[identity_cert_name].pem_key_cert_pair = *pem_key_cert_pairs;
    updated_identity_cert_name = absl::make_optional(identity_cert_name);
  }
  if (updated_root_cert_name.has_value() ||
      updated_identity_cert_name.has_value()) {
    CertificatesUpdated(updated_root_cert_name, updated_identity_cert_name);
  }
}

void grpc_tls_certificate_distributor::SetRootCerts(
    std::string root_cert_name, absl::string_view pem_root_certs) {
  // move the root_cert_name to avoid unnecessary copy.
  SetKeyMaterials(std::move(root_cert_name),
                  absl::make_optional(pem_root_certs), "", absl::nullopt);
};

void grpc_tls_certificate_distributor::SetKeyCertPairs(
    std::string identity_cert_name, PemKeyCertPairList pem_key_cert_pairs) {
  // move the identity_cert_name and pem_key_cert_pairs to avoid unnecessary
  // copies.
  SetKeyMaterials("", absl::nullopt, std::move(identity_cert_name),
                  absl::make_optional(std::move(pem_key_cert_pairs)));
};

bool grpc_tls_certificate_distributor::HasRootCerts(
    const std::string& root_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  const auto it = store_.find(root_cert_name);
  return it != store_.end() && it->second.pem_root_certs != "";
};

bool grpc_tls_certificate_distributor::HasKeyCertPairs(
    const std::string& identity_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  const auto it = store_.find(identity_cert_name);
  return it != store_.end() && it->second.pem_key_cert_pair.size() != 0;
};

void grpc_tls_certificate_distributor::WatchTlsCertificates(
    std::unique_ptr<TlsCertificatesWatcherInterface> watcher,
    absl::optional<std::string> root_cert_name,
    absl::optional<std::string> identity_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  TlsCertificatesWatcherInterface* watcher_ptr = watcher.get();
  if (watcher_ptr == nullptr) {
    return;
  }
  if (!root_cert_name.has_value() && !identity_cert_name.has_value()) {
    return;
  }
  watchers_[watcher_ptr] = {std::move(watcher), root_cert_name,
                            identity_cert_name};
  if (root_cert_name.has_value()) {
    Store& store = store_[*root_cert_name];
    store.root_cert_watchers.insert(watcher_ptr);
    // We will only notify the caller(e.g. the Producer) the first time a
    // particular certificate name is being watched.
    if (store.root_cert_watcher_count == 0 &&
        watch_status_callback_ != nullptr) {
      watch_status_callback_(*root_cert_name, true,
                             store.identity_cert_watcher_count > 0);
    }
    ++store.root_cert_watcher_count;
  }
  if (identity_cert_name.has_value()) {
    Store& store = store_[*identity_cert_name];
    store.identity_cert_watchers.insert(watcher_ptr);
    if (store.identity_cert_watcher_count == 0 &&
        watch_status_callback_ != nullptr) {
      // We will only invoke the callback when we are sure the same callback is
      // not invoked when checking root_cert_name.
      if (!root_cert_name.has_value() ||
          *root_cert_name != *identity_cert_name) {
        watch_status_callback_(*identity_cert_name,
                               store.root_cert_watcher_count > 0, true);
      }
    }
    ++store.identity_cert_watcher_count;
  }
};

void grpc_tls_certificate_distributor::CancelTlsCertificatesWatch(
    TlsCertificatesWatcherInterface* watcher) {
  grpc_core::MutexLock lock(&mu_);
  const auto watcher_it = watchers_.find(watcher);
  if (watcher_it == watchers_.end()) {
    return;
  }
  if (watcher_it->second.root_cert_name.has_value()) {
    const std::string& root_cert_name = *(watcher_it->second.root_cert_name);
    const auto it = store_.find(root_cert_name);
    GPR_ASSERT(it != store_.end());
    GPR_ASSERT(it->second.root_cert_watchers.find(watcher) !=
               it->second.root_cert_watchers.end());
    it->second.root_cert_watchers.erase(watcher);
    GPR_ASSERT(it->second.root_cert_watcher_count > 0);
    --it->second.root_cert_watcher_count;
    if (it->second.root_cert_watcher_count == 0 &&
        watch_status_callback_ != nullptr) {
      watch_status_callback_(root_cert_name, false,
                             it->second.identity_cert_watcher_count > 0);
    }
  }
  if (watcher_it->second.identity_cert_name.has_value()) {
    const std::string& identity_cert_name =
        *(watcher_it->second.identity_cert_name);
    const auto it = store_.find(identity_cert_name);
    GPR_ASSERT(it != store_.end());
    GPR_ASSERT(it->second.identity_cert_watchers.find(watcher) !=
               it->second.identity_cert_watchers.end());
    it->second.identity_cert_watchers.erase(watcher);
    GPR_ASSERT(it->second.identity_cert_watcher_count > 0);
    --it->second.identity_cert_watcher_count;
    if (it->second.identity_cert_watcher_count == 0 &&
        watch_status_callback_ != nullptr) {
      // We will only invoke the callback when we are sure the same callback is
      // not invoked when checking watcher_it->second.root_cert_name.
      if (!watcher_it->second.root_cert_name.has_value() ||
          *(watcher_it->second.root_cert_name) != identity_cert_name) {
        watch_status_callback_(identity_cert_name,
                               it->second.root_cert_watcher_count > 0, false);
      }
    }
  }
  watchers_.erase(watcher);
};

void grpc_tls_certificate_distributor::CertificatesUpdated(
    const absl::optional<std::string>& root_cert_name,
    const absl::optional<std::string>& identity_cert_name) {
  // Get the updated certificate contents based on the names.
  absl::optional<absl::string_view> updated_root_certs;
  if (root_cert_name.has_value()) {
    const auto it = store_.find(*root_cert_name);
    if (it != store_.end()) {
      updated_root_certs = absl::make_optional(it->second.pem_root_certs);
    }
  }
  absl::optional<PemKeyCertPairList> updated_identity_key_cert_pair;
  if (identity_cert_name.has_value()) {
    const auto it = store_.find(*identity_cert_name);
    if (it != store_.end()) {
      updated_identity_key_cert_pair =
          absl::make_optional(it->second.pem_key_cert_pair);
    }
  }
  // Go through each affected watchers and invoke OnCertificatesChanged based on
  // if their watching root and identity certs are updated.
  if (root_cert_name.has_value()) {
    const auto store_it = store_.find(*root_cert_name);
    if (store_it != store_.end()) {
      for (const auto watcher_ptr : store_it->second.root_cert_watchers) {
        GPR_ASSERT(watcher_ptr != nullptr);
        const auto watcher_it = watchers_.find(watcher_ptr);
        GPR_ASSERT(watcher_it != watchers_.end());
        GPR_ASSERT(watcher_it->second.root_cert_name.has_value());
        GPR_ASSERT(*root_cert_name == *watcher_it->second.root_cert_name);
        if (watcher_it->second.identity_cert_name.has_value()) {
          // Check if the identity certs this watcher is watching are just the
          // ones we are updating.
          if (identity_cert_name.has_value() &&
              *identity_cert_name == *watcher_it->second.identity_cert_name) {
            watcher_ptr->OnCertificatesChanged(updated_root_certs,
                                               updated_identity_key_cert_pair);
            continue;
          }
          // Find the contents of the identity certs this watcher is watching,
          // if there is any.
          const auto identity_store_it =
              store_.find(*watcher_it->second.identity_cert_name);
          if (identity_store_it != store_.end()) {
            watcher_ptr->OnCertificatesChanged(
                updated_root_certs,
                absl::make_optional(
                    identity_store_it->second.pem_key_cert_pair));
            continue;
          }
        }
        watcher_ptr->OnCertificatesChanged(updated_root_certs, absl::nullopt);
      }
    }
  }
  if (identity_cert_name.has_value()) {
    const auto store_it = store_.find(*identity_cert_name);
    if (store_it != store_.end()) {
      for (const auto watcher_ptr : store_it->second.identity_cert_watchers) {
        GPR_ASSERT(watcher_ptr != nullptr);
        const auto watcher_it = watchers_.find(watcher_ptr);
        GPR_ASSERT(watcher_it != watchers_.end());
        GPR_ASSERT(watcher_it->second.identity_cert_name.has_value());
        GPR_ASSERT(*identity_cert_name ==
                   *watcher_it->second.identity_cert_name);
        if (watcher_it->second.root_cert_name.has_value()) {
          // If the root certs this watcher is watching are just the one we are
          // updating, we already invoked OnCertificatesChanged when checking
          // root_cert_name, so we will skip here.
          if (root_cert_name.has_value() &&
              *root_cert_name == *watcher_it->second.root_cert_name) {
            continue;
          }
          // Find the contents of the root certs this watcher is watching", if
          // there is any.
          const auto root_store_it =
              store_.find(*watcher_it->second.root_cert_name);
          if (root_store_it != store_.end()) {
            watcher_ptr->OnCertificatesChanged(
                absl::make_optional(root_store_it->second.pem_root_certs),
                updated_identity_key_cert_pair);
            continue;
          }
        }
        watcher_ptr->OnCertificatesChanged(absl::nullopt,
                                           updated_identity_key_cert_pair);
      }
    }
  }
}
