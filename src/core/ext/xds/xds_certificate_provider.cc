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

#include "src/core/ext/xds/xds_certificate_provider.h"

#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

namespace grpc_core {

namespace {

class RootCertificatesWatcher final
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

  void OnError(grpc_error_handle root_cert_error,
               grpc_error_handle /*identity_cert_error*/) override {
    if (!root_cert_error.ok()) {
      parent_->SetErrorForCert("", root_cert_error /* pass the ref */,
                               absl::nullopt);
    }
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> parent_;
};

class IdentityCertificatesWatcher final
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

  void OnError(grpc_error_handle /*root_cert_error*/,
               grpc_error_handle identity_cert_error) override {
    if (!identity_cert_error.ok()) {
      parent_->SetErrorForCert("", absl::nullopt,
                               identity_cert_error /* pass the ref */);
    }
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> parent_;
};

}  // namespace

//
// XdsCertificateProvider
//

XdsCertificateProvider::XdsCertificateProvider(
    RefCountedPtr<grpc_tls_certificate_provider> root_cert_provider,
    absl::string_view root_cert_name,
    RefCountedPtr<grpc_tls_certificate_provider> identity_cert_provider,
    absl::string_view identity_cert_name,
    std::vector<StringMatcher> san_matchers)
    : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()),
      root_cert_provider_(std::move(root_cert_provider)),
      root_cert_name_(root_cert_name),
      identity_cert_provider_(std::move(identity_cert_provider)),
      identity_cert_name_(identity_cert_name),
      san_matchers_(std::move(san_matchers)),
      require_client_certificate_(false) {
  distributor_->SetWatchStatusCallback(
      absl::bind_front(&XdsCertificateProvider::WatchStatusCallback, this));
}

XdsCertificateProvider::XdsCertificateProvider(
    RefCountedPtr<grpc_tls_certificate_provider> root_cert_provider,
    absl::string_view root_cert_name,
    RefCountedPtr<grpc_tls_certificate_provider> identity_cert_provider,
    absl::string_view identity_cert_name, bool require_client_certificate)
    : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()),
      root_cert_provider_(std::move(root_cert_provider)),
      root_cert_name_(root_cert_name),
      identity_cert_provider_(std::move(identity_cert_provider)),
      identity_cert_name_(identity_cert_name),
      require_client_certificate_(require_client_certificate) {
  distributor_->SetWatchStatusCallback(
      absl::bind_front(&XdsCertificateProvider::WatchStatusCallback, this));
}

XdsCertificateProvider::~XdsCertificateProvider() {
  distributor_->SetWatchStatusCallback(nullptr);
}

UniqueTypeName XdsCertificateProvider::type() const {
  static UniqueTypeName::Factory kFactory("Xds");
  return kFactory.Create();
}

void XdsCertificateProvider::WatchStatusCallback(std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
  if (!cert_name.empty()) {
    if (root_being_watched) {
      distributor_->SetErrorForCert(
          cert_name,
          GRPC_ERROR_CREATE(
              "No certificate provider available for root certificates"),
          absl::nullopt);
    }
    if (identity_being_watched) {
      distributor_->SetErrorForCert(
          cert_name, absl::nullopt,
          GRPC_ERROR_CREATE(
              "No certificate provider available for identity certificates"));
    }
    return;
  }
  // We aren't specially handling the case where root_cert_distributor is same
  // as identity_cert_distributor. Always using two separate watchers
  // irrespective of the fact results in a straightforward design, and using a
  // single watcher does not seem to provide any benefit other than cutting down
  // on the number of callbacks.
  if (root_being_watched && root_cert_watcher_ == nullptr) {
    // Start watching root cert.
    if (root_cert_provider_ == nullptr) {
      distributor_->SetErrorForCert(
          cert_name,
          GRPC_ERROR_CREATE(
              "No certificate provider available for root certificates"),
          absl::nullopt);
    } else {
      auto watcher = std::make_unique<RootCertificatesWatcher>(distributor_);
      root_cert_watcher_ = watcher.get();
      root_cert_provider_->distributor()->WatchTlsCertificates(
          std::move(watcher), root_cert_name_, absl::nullopt);
    }
  } else if (!root_being_watched && root_cert_watcher_ != nullptr) {
    // Cancel root cert watch.
    GPR_ASSERT(root_cert_provider_ != nullptr);
    root_cert_provider_->distributor()->CancelTlsCertificatesWatch(
        root_cert_watcher_);
    root_cert_watcher_ = nullptr;
  }
  if (identity_being_watched && identity_cert_watcher_ == nullptr) {
    // Start watching identity cert.
    if (identity_cert_provider_ == nullptr) {
      distributor_->SetErrorForCert(
          cert_name, absl::nullopt,
          GRPC_ERROR_CREATE(
              "No certificate provider available for identity certificates"));
    } else {
      auto watcher =
          std::make_unique<IdentityCertificatesWatcher>(distributor_);
      identity_cert_watcher_ = watcher.get();
      identity_cert_provider_->distributor()->WatchTlsCertificates(
          std::move(watcher), absl::nullopt, identity_cert_name_);
    }
  } else if (!identity_being_watched && identity_cert_watcher_ != nullptr) {
    GPR_ASSERT(identity_cert_provider_ != nullptr);
    identity_cert_provider_->distributor()->CancelTlsCertificatesWatch(
        identity_cert_watcher_);
    identity_cert_watcher_ = nullptr;
  }
}

}  // namespace grpc_core
