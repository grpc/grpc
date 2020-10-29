//
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
//

#include <grpc/support/port_platform.h>

#include "absl/functional/bind_front.h"
#include "absl/strings/str_cat.h"

#include "src/core/ext/xds/xds_certificate_provider.h"

namespace grpc_core {

namespace {

class RootCertificatesWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  // Takes a ref to \a parent instead of a raw pointer since the watcher is
  // owned by the root certificate distributor and not by \a parent. Note that
  // presently, the watcher is immediately deleted when
  // CancelTlsCertificatesWatch() is called, but that can potentially change in
  // the future.
  explicit RootCertificatesWatcher(
      RefCountedPtr<grpc_tls_certificate_distributor> parent)
      : parent_(std::move(parent)) {}

  void OnCertificatesChanged(absl::optional<absl::string_view> root_certs,
                             absl::optional<PemKeyCertPairList>
                             /* key_cert_pairs */) override {
    if (root_certs.has_value()) {
      parent_->SetKeyMaterials("", std::string(root_certs.value()),
                               absl::nullopt);
    }
  }

  void OnError(grpc_error* root_cert_error,
               grpc_error* identity_cert_error) override {
    if (root_cert_error != GRPC_ERROR_NONE) {
      parent_->SetErrorForCert("", root_cert_error /* pass the ref */,
                               absl::nullopt);
    }
    GRPC_ERROR_UNREF(identity_cert_error);
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> parent_;
};

class IdentityCertificatesWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  // Takes a ref to \a parent instead of a raw pointer since the watcher is
  // owned by the root certificate distributor and not by \a parent. Note that
  // presently, the watcher is immediately deleted when
  // CancelTlsCertificatesWatch() is called, but that can potentially change in
  // the future.
  explicit IdentityCertificatesWatcher(
      RefCountedPtr<grpc_tls_certificate_distributor> parent)
      : parent_(std::move(parent)) {}

  void OnCertificatesChanged(
      absl::optional<absl::string_view> /* root_certs */,
      absl::optional<PemKeyCertPairList> key_cert_pairs) override {
    if (key_cert_pairs.has_value()) {
      parent_->SetKeyMaterials("", absl::nullopt, key_cert_pairs);
    }
  }

  void OnError(grpc_error* root_cert_error,
               grpc_error* identity_cert_error) override {
    if (identity_cert_error != GRPC_ERROR_NONE) {
      parent_->SetErrorForCert("", absl::nullopt,
                               identity_cert_error /* pass the ref */);
    }
    GRPC_ERROR_UNREF(root_cert_error);
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> parent_;
};

}  // namespace

XdsCertificateProvider::XdsCertificateProvider(
    absl::string_view root_cert_name,
    RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor,
    absl::string_view identity_cert_name,
    RefCountedPtr<grpc_tls_certificate_distributor> identity_cert_distributor)
    : root_cert_name_(root_cert_name),
      identity_cert_name_(identity_cert_name),
      root_cert_distributor_(std::move(root_cert_distributor)),
      identity_cert_distributor_(std::move(identity_cert_distributor)),
      distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()) {
  distributor_->SetWatchStatusCallback(
      absl::bind_front(&XdsCertificateProvider::WatchStatusCallback, this));
}

void XdsCertificateProvider::UpdateRootCertNameAndDistributor(
    absl::string_view root_cert_name,
    RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor) {
  MutexLock lock(&mu_);
  root_cert_name_ = std::string(root_cert_name);
  if (watching_root_certs_) {
    // The root certificates are being watched. Swap out the watcher.
    if (root_cert_distributor_ != nullptr) {
      root_cert_distributor_->CancelTlsCertificatesWatch(root_cert_watcher_);
    }
    if (root_cert_distributor != nullptr) {
      UpdateRootCertWatcher(root_cert_distributor.get());
    } else {
      root_cert_watcher_ = nullptr;
      distributor_->SetErrorForCert(
          "",
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for root certificates"),
          absl::nullopt);
    }
  }
  // Swap out the root certificate distributor
  root_cert_distributor_ = std::move(root_cert_distributor);
}

void XdsCertificateProvider::UpdateIdentityCertNameAndDistributor(
    absl::string_view identity_cert_name,
    RefCountedPtr<grpc_tls_certificate_distributor> identity_cert_distributor) {
  MutexLock lock(&mu_);
  identity_cert_name_ = std::string(identity_cert_name);
  if (watching_identity_certs_) {
    // The identity certificates are being watched. Swap out the watcher.
    if (identity_cert_distributor_ != nullptr) {
      identity_cert_distributor_->CancelTlsCertificatesWatch(
          identity_cert_watcher_);
    }
    if (identity_cert_distributor != nullptr) {
      UpdateIdentityCertWatcher(identity_cert_distributor.get());
    } else {
      identity_cert_watcher_ = nullptr;
      distributor_->SetErrorForCert(
          "", absl::nullopt,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for identity certificates"));
    }
  }
  // Swap out the identity certificate distributor
  identity_cert_distributor_ = std::move(identity_cert_distributor);
}

void XdsCertificateProvider::WatchStatusCallback(std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
  // We aren't specially handling the case where root_cert_distributor is same
  // as identity_cert_distributor. Always using two separate watchers
  // irrespective of the fact results in a straightforward design, and using a
  // single watcher does not seem to provide any benefit other than cutting down
  // on the number of callbacks.
  MutexLock lock(&mu_);
  if (!cert_name.empty()) {
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Illegal certificate name: \'", cert_name,
                     "\'. Should be empty.")
            .c_str());
    distributor_->SetErrorForCert(cert_name, GRPC_ERROR_REF(error),
                                  GRPC_ERROR_REF(error));
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (root_being_watched && !watching_root_certs_) {
    // We need to start watching root certs.
    watching_root_certs_ = true;
    if (root_cert_distributor_ == nullptr) {
      distributor_->SetErrorForCert(
          "",
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for root certificates"),
          absl::nullopt);
    } else {
      UpdateRootCertWatcher(root_cert_distributor_.get());
    }
  } else if (!root_being_watched && watching_root_certs_) {
    // We need to cancel root certs watch.
    watching_root_certs_ = false;
    if (root_cert_distributor_ != nullptr) {
      root_cert_distributor_->CancelTlsCertificatesWatch(root_cert_watcher_);
      root_cert_watcher_ = nullptr;
    }
    GPR_ASSERT(root_cert_watcher_ == nullptr);
  }
  if (identity_being_watched && !watching_identity_certs_) {
    watching_identity_certs_ = true;
    if (identity_cert_distributor_ == nullptr) {
      distributor_->SetErrorForCert(
          "", absl::nullopt,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for identity certificates"));
    } else {
      UpdateIdentityCertWatcher(identity_cert_distributor_.get());
    }
  } else if (!identity_being_watched && watching_identity_certs_) {
    watching_identity_certs_ = false;
    if (identity_cert_distributor_ != nullptr) {
      identity_cert_distributor_->CancelTlsCertificatesWatch(
          identity_cert_watcher_);
      identity_cert_watcher_ = nullptr;
    }
    GPR_ASSERT(identity_cert_watcher_ == nullptr);
  }
}

void XdsCertificateProvider::UpdateRootCertWatcher(
    grpc_tls_certificate_distributor* root_cert_distributor) {
  auto watcher = absl::make_unique<RootCertificatesWatcher>(distributor());
  root_cert_watcher_ = watcher.get();
  root_cert_distributor->WatchTlsCertificates(std::move(watcher),
                                              root_cert_name_, absl::nullopt);
}

void XdsCertificateProvider::UpdateIdentityCertWatcher(
    grpc_tls_certificate_distributor* identity_cert_distributor) {
  auto watcher = absl::make_unique<IdentityCertificatesWatcher>(distributor());
  identity_cert_watcher_ = watcher.get();
  identity_cert_distributor->WatchTlsCertificates(
      std::move(watcher), absl::nullopt, identity_cert_name_);
}

}  // namespace grpc_core
