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
#include "absl/memory/memory.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

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
  RootCertificatesWatcher(
      RefCountedPtr<grpc_tls_certificate_distributor> parent,
      std::string cert_name)
      : parent_(std::move(parent)), cert_name_(std::move(cert_name)) {}

  void OnCertificatesChanged(absl::optional<absl::string_view> root_certs,
                             absl::optional<PemKeyCertPairList>
                             /* key_cert_pairs */) override {
    if (root_certs.has_value()) {
      parent_->SetKeyMaterials(cert_name_, std::string(root_certs.value()),
                               absl::nullopt);
    }
  }

  void OnError(grpc_error_handle root_cert_error,
               grpc_error_handle identity_cert_error) override {
    if (!GRPC_ERROR_IS_NONE(root_cert_error)) {
      parent_->SetErrorForCert(cert_name_, root_cert_error /* pass the ref */,
                               absl::nullopt);
    }
    GRPC_ERROR_UNREF(identity_cert_error);
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> parent_;
  std::string cert_name_;
};

class IdentityCertificatesWatcher
    : public grpc_tls_certificate_distributor::TlsCertificatesWatcherInterface {
 public:
  // Takes a ref to \a parent instead of a raw pointer since the watcher is
  // owned by the root certificate distributor and not by \a parent. Note that
  // presently, the watcher is immediately deleted when
  // CancelTlsCertificatesWatch() is called, but that can potentially change in
  // the future.
  IdentityCertificatesWatcher(
      RefCountedPtr<grpc_tls_certificate_distributor> parent,
      std::string cert_name)
      : parent_(std::move(parent)), cert_name_(std::move(cert_name)) {}

  void OnCertificatesChanged(
      absl::optional<absl::string_view> /* root_certs */,
      absl::optional<PemKeyCertPairList> key_cert_pairs) override {
    if (key_cert_pairs.has_value()) {
      parent_->SetKeyMaterials(cert_name_, absl::nullopt, key_cert_pairs);
    }
  }

  void OnError(grpc_error_handle root_cert_error,
               grpc_error_handle identity_cert_error) override {
    if (!GRPC_ERROR_IS_NONE(identity_cert_error)) {
      parent_->SetErrorForCert(cert_name_, absl::nullopt,
                               identity_cert_error /* pass the ref */);
    }
    GRPC_ERROR_UNREF(root_cert_error);
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> parent_;
  std::string cert_name_;
};

}  // namespace

//
// XdsCertificateProvider::ClusterCertificateState
//

XdsCertificateProvider::ClusterCertificateState::~ClusterCertificateState() {
  if (root_cert_watcher_ != nullptr) {
    root_cert_distributor_->CancelTlsCertificatesWatch(root_cert_watcher_);
  }
  if (identity_cert_watcher_ != nullptr) {
    identity_cert_distributor_->CancelTlsCertificatesWatch(
        identity_cert_watcher_);
  }
}

bool XdsCertificateProvider::ClusterCertificateState::IsSafeToRemove() const {
  return !watching_root_certs_ && !watching_identity_certs_ &&
         root_cert_distributor_ == nullptr &&
         identity_cert_distributor_ == nullptr;
}

void XdsCertificateProvider::ClusterCertificateState::
    UpdateRootCertNameAndDistributor(
        const std::string& cert_name, absl::string_view root_cert_name,
        RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor) {
  if (root_cert_name_ == root_cert_name &&
      root_cert_distributor_ == root_cert_distributor) {
    return;
  }
  root_cert_name_ = std::string(root_cert_name);
  if (watching_root_certs_) {
    // The root certificates are being watched. Swap out the watcher.
    if (root_cert_distributor_ != nullptr) {
      root_cert_distributor_->CancelTlsCertificatesWatch(root_cert_watcher_);
    }
    if (root_cert_distributor != nullptr) {
      UpdateRootCertWatcher(cert_name, root_cert_distributor.get());
    } else {
      root_cert_watcher_ = nullptr;
      xds_certificate_provider_->distributor_->SetErrorForCert(
          "",
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for root certificates"),
          absl::nullopt);
    }
  }
  // Swap out the root certificate distributor
  root_cert_distributor_ = std::move(root_cert_distributor);
}

void XdsCertificateProvider::ClusterCertificateState::
    UpdateIdentityCertNameAndDistributor(
        const std::string& cert_name, absl::string_view identity_cert_name,
        RefCountedPtr<grpc_tls_certificate_distributor>
            identity_cert_distributor) {
  if (identity_cert_name_ == identity_cert_name &&
      identity_cert_distributor_ == identity_cert_distributor) {
    return;
  }
  identity_cert_name_ = std::string(identity_cert_name);
  if (watching_identity_certs_) {
    // The identity certificates are being watched. Swap out the watcher.
    if (identity_cert_distributor_ != nullptr) {
      identity_cert_distributor_->CancelTlsCertificatesWatch(
          identity_cert_watcher_);
    }
    if (identity_cert_distributor != nullptr) {
      UpdateIdentityCertWatcher(cert_name, identity_cert_distributor.get());
    } else {
      identity_cert_watcher_ = nullptr;
      xds_certificate_provider_->distributor_->SetErrorForCert(
          "", absl::nullopt,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for identity certificates"));
    }
  }
  // Swap out the identity certificate distributor
  identity_cert_distributor_ = std::move(identity_cert_distributor);
}

void XdsCertificateProvider::ClusterCertificateState::UpdateRootCertWatcher(
    const std::string& cert_name,
    grpc_tls_certificate_distributor* root_cert_distributor) {
  auto watcher = absl::make_unique<RootCertificatesWatcher>(
      xds_certificate_provider_->distributor_, cert_name);
  root_cert_watcher_ = watcher.get();
  root_cert_distributor->WatchTlsCertificates(std::move(watcher),
                                              root_cert_name_, absl::nullopt);
}

void XdsCertificateProvider::ClusterCertificateState::UpdateIdentityCertWatcher(
    const std::string& cert_name,
    grpc_tls_certificate_distributor* identity_cert_distributor) {
  auto watcher = absl::make_unique<IdentityCertificatesWatcher>(
      xds_certificate_provider_->distributor_, cert_name);
  identity_cert_watcher_ = watcher.get();
  identity_cert_distributor->WatchTlsCertificates(
      std::move(watcher), absl::nullopt, identity_cert_name_);
}

void XdsCertificateProvider::ClusterCertificateState::WatchStatusCallback(
    const std::string& cert_name, bool root_being_watched,
    bool identity_being_watched) {
  // We aren't specially handling the case where root_cert_distributor is same
  // as identity_cert_distributor. Always using two separate watchers
  // irrespective of the fact results in a straightforward design, and using a
  // single watcher does not seem to provide any benefit other than cutting down
  // on the number of callbacks.
  if (root_being_watched && !watching_root_certs_) {
    // We need to start watching root certs.
    watching_root_certs_ = true;
    if (root_cert_distributor_ == nullptr) {
      xds_certificate_provider_->distributor_->SetErrorForCert(
          cert_name,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for root certificates"),
          absl::nullopt);
    } else {
      UpdateRootCertWatcher(cert_name, root_cert_distributor_.get());
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
      xds_certificate_provider_->distributor_->SetErrorForCert(
          cert_name, absl::nullopt,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "No certificate provider available for identity certificates"));
    } else {
      UpdateIdentityCertWatcher(cert_name, identity_cert_distributor_.get());
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

//
// XdsCertificateProvider
//

XdsCertificateProvider::XdsCertificateProvider()
    : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()) {
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

bool XdsCertificateProvider::ProvidesRootCerts(const std::string& cert_name) {
  MutexLock lock(&mu_);
  auto it = certificate_state_map_.find(cert_name);
  if (it == certificate_state_map_.end()) return false;
  return it->second->ProvidesRootCerts();
}

void XdsCertificateProvider::UpdateRootCertNameAndDistributor(
    const std::string& cert_name, absl::string_view root_cert_name,
    RefCountedPtr<grpc_tls_certificate_distributor> root_cert_distributor) {
  MutexLock lock(&mu_);
  auto it = certificate_state_map_.find(cert_name);
  if (it == certificate_state_map_.end()) {
    it = certificate_state_map_
             .emplace(cert_name,
                      absl::make_unique<ClusterCertificateState>(this))
             .first;
  }
  it->second->UpdateRootCertNameAndDistributor(cert_name, root_cert_name,
                                               root_cert_distributor);
  // Delete unused entries.
  if (it->second->IsSafeToRemove()) certificate_state_map_.erase(it);
}

bool XdsCertificateProvider::ProvidesIdentityCerts(
    const std::string& cert_name) {
  MutexLock lock(&mu_);
  auto it = certificate_state_map_.find(cert_name);
  if (it == certificate_state_map_.end()) return false;
  return it->second->ProvidesIdentityCerts();
}

void XdsCertificateProvider::UpdateIdentityCertNameAndDistributor(
    const std::string& cert_name, absl::string_view identity_cert_name,
    RefCountedPtr<grpc_tls_certificate_distributor> identity_cert_distributor) {
  MutexLock lock(&mu_);
  auto it = certificate_state_map_.find(cert_name);
  if (it == certificate_state_map_.end()) {
    it = certificate_state_map_
             .emplace(cert_name,
                      absl::make_unique<ClusterCertificateState>(this))
             .first;
  }
  it->second->UpdateIdentityCertNameAndDistributor(
      cert_name, identity_cert_name, identity_cert_distributor);
  // Delete unused entries.
  if (it->second->IsSafeToRemove()) certificate_state_map_.erase(it);
}

bool XdsCertificateProvider::GetRequireClientCertificate(
    const std::string& cert_name) {
  MutexLock lock(&mu_);
  auto it = certificate_state_map_.find(cert_name);
  if (it == certificate_state_map_.end()) return false;
  return it->second->require_client_certificate();
}

void XdsCertificateProvider::UpdateRequireClientCertificate(
    const std::string& cert_name, bool require_client_certificate) {
  MutexLock lock(&mu_);
  auto it = certificate_state_map_.find(cert_name);
  if (it == certificate_state_map_.end()) return;
  it->second->set_require_client_certificate(require_client_certificate);
}

std::vector<StringMatcher> XdsCertificateProvider::GetSanMatchers(
    const std::string& cluster) {
  MutexLock lock(&san_matchers_mu_);
  auto it = san_matcher_map_.find(cluster);
  if (it == san_matcher_map_.end()) return {};
  return it->second;
}

void XdsCertificateProvider::UpdateSubjectAlternativeNameMatchers(
    const std::string& cluster, std::vector<StringMatcher> matchers) {
  MutexLock lock(&san_matchers_mu_);
  if (matchers.empty()) {
    san_matcher_map_.erase(cluster);
  } else {
    san_matcher_map_[cluster] = std::move(matchers);
  }
}

void XdsCertificateProvider::WatchStatusCallback(std::string cert_name,
                                                 bool root_being_watched,
                                                 bool identity_being_watched) {
  MutexLock lock(&mu_);
  auto it = certificate_state_map_.find(cert_name);
  if (it == certificate_state_map_.end()) {
    it = certificate_state_map_
             .emplace(cert_name,
                      absl::make_unique<ClusterCertificateState>(this))
             .first;
  }
  it->second->WatchStatusCallback(cert_name, root_being_watched,
                                  identity_being_watched);
  // Delete unused entries.
  if (it->second->IsSafeToRemove()) certificate_state_map_.erase(it);
}

namespace {

void* XdsCertificateProviderArgCopy(void* p) {
  XdsCertificateProvider* xds_certificate_provider =
      static_cast<XdsCertificateProvider*>(p);
  return xds_certificate_provider->Ref().release();
}

void XdsCertificateProviderArgDestroy(void* p) {
  XdsCertificateProvider* xds_certificate_provider =
      static_cast<XdsCertificateProvider*>(p);
  xds_certificate_provider->Unref();
}

int XdsCertificateProviderArgCmp(void* p, void* q) {
  return QsortCompare(p, q);
}

const grpc_arg_pointer_vtable kChannelArgVtable = {
    XdsCertificateProviderArgCopy, XdsCertificateProviderArgDestroy,
    XdsCertificateProviderArgCmp};

}  // namespace

grpc_arg XdsCertificateProvider::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_XDS_CERTIFICATE_PROVIDER),
      const_cast<XdsCertificateProvider*>(this), &kChannelArgVtable);
}

RefCountedPtr<XdsCertificateProvider>
XdsCertificateProvider::GetFromChannelArgs(const grpc_channel_args* args) {
  XdsCertificateProvider* xds_certificate_provider =
      grpc_channel_args_find_pointer<XdsCertificateProvider>(
          args, GRPC_ARG_XDS_CERTIFICATE_PROVIDER);
  return xds_certificate_provider != nullptr ? xds_certificate_provider->Ref()
                                             : nullptr;
}

}  // namespace grpc_core
