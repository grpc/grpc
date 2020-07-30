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

grpc_tls_certificate_distributor::~grpc_tls_certificate_distributor() {
  grpc_core::MutexLock lock(&mu_);
  // If watchers_ is not empty in dtor, we need to call OnError on each watcher.
  for (const auto& watcher_it : watchers_) {
    TlsCertificatesWatcherInterface* watcher_interface = watcher_it.first;
    if (watcher_interface == nullptr || watcher_it.second.watcher == nullptr) {
      gpr_log(GPR_ERROR,
              "WatcherInfo in grpc_tls_certificate_distributor is in "
              "inconsistent state.");
      continue;
    }
    grpc_error* err_msg = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "The grpc_tls_certificate_distributor is destructed but the watcher "
        "may still be used.");
    watcher_interface->OnError(err_msg);
  }
//  if (watchers_.size() > 0) {
//    watchers_.clear();
//  }
  // If watch_status_ is not empty, we need to explicitly invoke the callback
  // for each certificate name to indicate that we are not able to watch those
  // certificates anymore.
  for (const auto& it : watch_status_) {
    if ((it.second.root_cert_watcher_cnt != 0 ||
         it.second.identity_cert_watcher_cnt != 0) &&
        this->watch_status_callback_ != nullptr) {
      this->watch_status_callback_(it.first, false, false);
    }
  }
}

void grpc_tls_certificate_distributor::SetKeyMaterials(
    std::string root_cert_name,
    absl::optional<absl::string_view> pem_root_certs,
    std::string identity_cert_name,
    absl::optional<PemKeyCertPairList> pem_key_cert_pairs) {
  grpc_core::MutexLock lock(&mu_);
  if (!pem_root_certs && !pem_key_cert_pairs) {
    return;
  }
  absl::optional<std::string> updated_root_cert_name = absl::nullopt;
  absl::optional<std::string> updated_identity_cert_name = absl::nullopt;
  if (pem_root_certs) {
    const auto& it = this->pem_root_certs_.find(root_cert_name);
    if (it == this->pem_root_certs_.end()) {
      this->pem_root_certs_.insert(
          std::make_pair(root_cert_name, std::string(*pem_root_certs)));
    } else {
      it->second = std::string(*pem_root_certs);
    }
    updated_root_cert_name = absl::make_optional(root_cert_name);
  }
  if (pem_key_cert_pairs) {
    const auto& it = this->pem_key_cert_pair_.find(identity_cert_name);
    if (it == this->pem_key_cert_pair_.end()) {
      this->pem_key_cert_pair_.insert(
          std::make_pair(identity_cert_name, *pem_key_cert_pairs));
    } else {
      it->second = *pem_key_cert_pairs;
    }
    updated_identity_cert_name = absl::make_optional(identity_cert_name);
  }
  if (updated_root_cert_name || updated_identity_cert_name) {
    this->CertificatesUpdated(updated_root_cert_name,
                              updated_identity_cert_name);
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
  const auto& it = this->pem_root_certs_.find(root_cert_name);
  return it != this->pem_root_certs_.end();
};

bool grpc_tls_certificate_distributor::HasKeyCertPairs(
    const std::string& identity_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  const auto& it = this->pem_key_cert_pair_.find(identity_cert_name);
  return it != this->pem_key_cert_pair_.end();
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
  WatcherInfo info = {std::move(watcher), root_cert_name, identity_cert_name};
  watchers_.insert(std::make_pair(watcher_ptr, std::move(info)));
  if (root_cert_name) {
    const auto& it = this->watch_status_.find(*root_cert_name);
    // We will only notify the caller(e.g. the Producer) the first time a
    // particular certificate name is being watched.
    if (it == this->watch_status_.end() ||
        it->second.root_cert_watcher_cnt == 0) {
      bool identity_cert_watched = false;
      if (it == this->watch_status_.end()) {
        CertificateStatus status = {1, 0};
        this->watch_status_.insert(std::make_pair(*root_cert_name, status));
      } else {
        it->second.root_cert_watcher_cnt += 1;
        identity_cert_watched = it->second.identity_cert_watcher_cnt > 0;
      }
      if (this->watch_status_callback_ != nullptr) {
        this->watch_status_callback_(*root_cert_name, true,
                                     identity_cert_watched);
      }
    } else {
      it->second.root_cert_watcher_cnt += 1;
    }
  }
  if (identity_cert_name) {
    const auto& it = this->watch_status_.find(*identity_cert_name);
    // We will only notify the caller(e.g. the Producer) the first time a
    // particular certificate name is being watched.
    if (it == this->watch_status_.end() ||
        it->second.identity_cert_watcher_cnt == 0) {
      bool root_cert_watched = false;
      if (it == this->watch_status_.end()) {
        CertificateStatus status = {0, 1};
        this->watch_status_.insert(std::make_pair(*identity_cert_name, status));
      } else {
        it->second.identity_cert_watcher_cnt += 1;
        root_cert_watched = it->second.root_cert_watcher_cnt > 0;
      }
      if (this->watch_status_callback_ != nullptr) {
        this->watch_status_callback_(*identity_cert_name, root_cert_watched,
                                     true);
      }
    } else {
      it->second.identity_cert_watcher_cnt += 1;
    }
  }
};

void grpc_tls_certificate_distributor::CancelTlsCertificatesWatch(
    TlsCertificatesWatcherInterface* watcher) {
  grpc_core::MutexLock lock(&mu_);
  const auto& watcher_it = watchers_.find(watcher);
  if (watcher_it == watchers_.end()) {
    return;
  }
  if (watcher_it->second.root_cert_name) {
    std::string root_cert_name = *(watcher_it->second.root_cert_name);
    const auto& it = this->watch_status_.find(root_cert_name);
    if (it == this->watch_status_.end() ||
        it->second.root_cert_watcher_cnt <= 0) {
      std::string error_msg =
          "Watcher status messed up: expect to see at least 1 watcher for "
          "name " +
          root_cert_name;
      gpr_log(GPR_ERROR, "%s", error_msg.c_str());
      watcher->OnError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(error_msg.c_str()));
      return;
    }
    it->second.root_cert_watcher_cnt -= 1;
    if (it->second.root_cert_watcher_cnt == 0) {
      if (this->watch_status_callback_ != nullptr) {
        this->watch_status_callback_(root_cert_name, false,
                                     it->second.identity_cert_watcher_cnt > 0);
      }
      if (it->second.identity_cert_watcher_cnt == 0) {
        this->watch_status_.erase(root_cert_name);
      }
    }
  }
  if (watcher_it->second.identity_cert_name) {
    std::string identity_cert_name = *(watcher_it->second.identity_cert_name);
    const auto& it = this->watch_status_.find(identity_cert_name);
    if (it == this->watch_status_.end() ||
        it->second.identity_cert_watcher_cnt <= 0) {
      std::string error_msg =
          "Watcher status messed up: expect to see at least 1 watcher for "
          "name " +
          identity_cert_name;
      gpr_log(GPR_ERROR, "%s", error_msg.c_str());
      watcher->OnError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(error_msg.c_str()));
      return;
    }
    it->second.identity_cert_watcher_cnt -= 1;
    if (it->second.identity_cert_watcher_cnt == 0) {
      if (this->watch_status_callback_ != nullptr) {
        this->watch_status_callback_(
            identity_cert_name, it->second.root_cert_watcher_cnt > 0, false);
      }
      if (it->second.root_cert_watcher_cnt == 0) {
        this->watch_status_.erase(identity_cert_name);
      }
    }
  }
  int result = watchers_.erase(watcher);
  if (result == 0) {
    std::string error_msg = "Failed to erase the watchers in the distributor.";
    gpr_log(GPR_ERROR, "%s", error_msg.c_str());
    watcher->OnError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(error_msg.c_str()));
    return;
  }
};

void grpc_tls_certificate_distributor::CertificatesUpdated(
    const absl::optional<std::string>& root_cert_name,
    const absl::optional<std::string>& identity_cert_name) {
  for (const auto& watcher_it : watchers_) {
    TlsCertificatesWatcherInterface* watcher_interface = watcher_it.first;
    if (watcher_interface == nullptr || watcher_it.second.watcher == nullptr) {
      gpr_log(GPR_ERROR,
              "The watchers in the distributor are in "
              "inconsistent state.");
      continue;
    }
    if (!watcher_it.second.root_cert_name &&
        !watcher_it.second.identity_cert_name) {
      return;
    }
    absl::optional<std::string> updated_root_certs = absl::nullopt;
    if (watcher_it.second.root_cert_name) {
      const auto& it =
          this->pem_root_certs_.find(*watcher_it.second.root_cert_name);
      if (it != this->pem_root_certs_.end()) {
        updated_root_certs = absl::make_optional(it->second);
      }
      // Having the condition "it == this->pem_root_certs_.end()" means a
      // particular root_cert_name is watched before being pushed into
      // pem_root_certs_. That is a valid case, so we will do nothing here.
    }
    absl::optional<PemKeyCertPairList> updated_identity_key_cert_pair =
        absl::nullopt;
    if (watcher_it.second.identity_cert_name) {
      const auto& it =
          this->pem_key_cert_pair_.find(*watcher_it.second.identity_cert_name);
      if (it != this->pem_key_cert_pair_.end()) {
        updated_identity_key_cert_pair = absl::make_optional(it->second);
      }
      // Having the condition "it == this->pem_key_cert_pair_.end()" means a
      // particular identity_cert_name is watched before being pushed into
      // pem_key_cert_pair_. That is a valid case, so we will do nothing here.
    }
    if (updated_root_certs || updated_identity_key_cert_pair) {
      watcher_interface->OnCertificatesChanged(updated_root_certs,
                                               updated_identity_key_cert_pair);
    }
  }
}
