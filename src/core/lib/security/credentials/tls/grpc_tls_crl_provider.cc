//
//
// Copyright 2023 gRPC authors.
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

#include "src/core/lib/security/credentials/tls/grpc_tls_crl_provider.h"

#include <limits.h>

// IWYU pragma: no_include <ratio>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

// IWYU pragma: no_include <openssl/mem.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>  // IWYU pragma: keep
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/directory_reader.h"
#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {
namespace experimental {

namespace {
std::string IssuerFromCrl(X509_CRL* crl) {
  if (crl == nullptr) {
    return "";
  }
  char* buf = X509_NAME_oneline(X509_CRL_get_issuer(crl), nullptr, 0);
  std::string ret;
  if (buf != nullptr) {
    ret = buf;
  }
  OPENSSL_free(buf);
  return ret;
}

absl::StatusOr<std::shared_ptr<Crl>> ReadCrlFromFile(
    const std::string& crl_path) {
  absl::StatusOr<Slice> crl_slice = LoadFile(crl_path, false);
  if (!crl_slice.ok()) {
    return crl_slice.status();
  }
  absl::StatusOr<std::unique_ptr<Crl>> crl =
      Crl::Parse(crl_slice->as_string_view());
  if (!crl.ok()) {
    return crl.status();
  }
  return crl;
}

}  // namespace

absl::StatusOr<std::unique_ptr<Crl>> Crl::Parse(absl::string_view crl_string) {
  if (crl_string.size() >= INT_MAX) {
    return absl::InvalidArgumentError("crl_string cannot be of size INT_MAX");
  }
  BIO* crl_bio =
      BIO_new_mem_buf(crl_string.data(), static_cast<int>(crl_string.size()));
  // Errors on BIO
  if (crl_bio == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from crl string to BIO failed.");
  }
  X509_CRL* crl = PEM_read_bio_X509_CRL(crl_bio, nullptr, nullptr, nullptr);
  BIO_free(crl_bio);
  if (crl == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from PEM string to X509 CRL failed.");
  }
  return CrlImpl::Create(crl);
}

absl::StatusOr<std::unique_ptr<CrlImpl>> CrlImpl::Create(X509_CRL* crl) {
  std::string issuer = IssuerFromCrl(crl);
  if (issuer.empty()) {
    return absl::InvalidArgumentError("Issuer of crl cannot be empty");
  }
  return std::make_unique<CrlImpl>(crl, issuer);
}

CrlImpl::~CrlImpl() { X509_CRL_free(crl_); }

absl::StatusOr<std::shared_ptr<CrlProvider>> CreateStaticCrlProvider(
    absl::Span<const std::string> crls) {
  absl::flat_hash_map<std::string, std::shared_ptr<Crl>> crl_map;
  for (const auto& raw_crl : crls) {
    absl::StatusOr<std::unique_ptr<Crl>> crl = Crl::Parse(raw_crl);
    if (!crl.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Parsing crl string failed with result ", crl.status().ToString()));
    }
    bool inserted = crl_map.emplace((*crl)->Issuer(), std::move(*crl)).second;
    if (!inserted) {
      gpr_log(GPR_ERROR,
              "StaticCrlProvider received multiple CRLs with the same issuer. "
              "The first one in the span will be used.");
    }
  }
  StaticCrlProvider provider = StaticCrlProvider(std::move(crl_map));
  return std::make_shared<StaticCrlProvider>(std::move(provider));
}

std::shared_ptr<Crl> StaticCrlProvider::GetCrl(
    const CertificateInfo& certificate_info) {
  auto it = crls_.find(certificate_info.Issuer());
  if (it == crls_.end()) {
    return nullptr;
  }
  return it->second;
}

absl::StatusOr<std::shared_ptr<CrlProvider>> CreateDirectoryReloaderCrlProvider(
    absl::string_view directory, std::chrono::seconds refresh_duration,
    std::function<void(absl::Status)> reload_error_callback) {
  if (refresh_duration < std::chrono::seconds(60)) {
    return absl::InvalidArgumentError("Refresh duration minimum is 60 seconds");
  }
  auto provider = std::make_shared<DirectoryReloaderCrlProvider>(
      refresh_duration, reload_error_callback, /*event_engine=*/nullptr,
      MakeDirectoryReader(directory));
  // This could be slow to do at startup, but we want to
  // make sure it's done before the provider is used.
  provider->UpdateAndStartTimer();
  return provider;
}

DirectoryReloaderCrlProvider::DirectoryReloaderCrlProvider(
    std::chrono::seconds duration, std::function<void(absl::Status)> callback,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine,
    std::shared_ptr<DirectoryReader> directory_impl)
    : refresh_duration_(Duration::FromSecondsAsDouble(duration.count())),
      reload_error_callback_(std::move(callback)),
      crl_directory_(std::move(directory_impl)) {
  // Must be called before `GetDefaultEventEngine`
  grpc_init();
  if (event_engine == nullptr) {
    event_engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
  } else {
    event_engine_ = std::move(event_engine);
  }
}

DirectoryReloaderCrlProvider::~DirectoryReloaderCrlProvider() {
  if (refresh_handle_.has_value()) {
    event_engine_->Cancel(refresh_handle_.value());
  }
  // Call here because we call grpc_init in the constructor
  grpc_shutdown();
}

void DirectoryReloaderCrlProvider::UpdateAndStartTimer() {
  absl::Status status = Update();
  if (!status.ok() && reload_error_callback_ != nullptr) {
    reload_error_callback_(status);
  }
  std::weak_ptr<DirectoryReloaderCrlProvider> self = shared_from_this();
  refresh_handle_ =
      event_engine_->RunAfter(refresh_duration_, [self = std::move(self)]() {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        if (std::shared_ptr<DirectoryReloaderCrlProvider> valid_ptr =
                self.lock()) {
          valid_ptr->UpdateAndStartTimer();
        }
      });
}

absl::Status DirectoryReloaderCrlProvider::Update() {
  absl::flat_hash_map<std::string, std::shared_ptr<Crl>> new_crls;
  std::vector<std::string> files_with_errors;
  absl::Status status = crl_directory_->ForEach([&](absl::string_view file) {
    std::string file_path = absl::StrCat(crl_directory_->Name(), "/", file);
    // Build a map of new_crls to update to. If all files successful, do a
    // full swap of the map. Otherwise update in place.
    absl::StatusOr<std::shared_ptr<Crl>> crl = ReadCrlFromFile(file_path);
    if (!crl.ok()) {
      files_with_errors.push_back(
          absl::StrCat(file_path, ": ", crl.status().ToString()));
      return;
    }
    // Now we have a good CRL to update in our map.
    // It's not safe to say crl->Issuer() on the LHS and std::move(crl) on the
    // RHS, because C++ does not guarantee which of those will be executed
    // first.
    std::string issuer((*crl)->Issuer());
    new_crls[std::move(issuer)] = std::move(*crl);
  });
  if (!status.ok()) {
    return status;
  }
  MutexLock lock(&mu_);
  if (!files_with_errors.empty()) {
    // Need to make sure CRLs we read successfully into new_crls are still
    // in-place updated in crls_.
    for (auto& kv : new_crls) {
      std::shared_ptr<Crl>& crl = kv.second;
      // It's not safe to say crl->Issuer() on the LHS and std::move(crl) on
      // the RHS, because C++ does not guarantee which of those will be
      // executed first.
      std::string issuer(crl->Issuer());
      crls_[std::move(issuer)] = std::move(crl);
    }
    return absl::UnknownError(absl::StrCat(
        "Errors reading the following files in the CRL directory: [",
        absl::StrJoin(files_with_errors, "; "), "]"));
  } else {
    crls_ = std::move(new_crls);
  }
  return absl::OkStatus();
}

std::shared_ptr<Crl> DirectoryReloaderCrlProvider::GetCrl(
    const CertificateInfo& certificate_info) {
  MutexLock lock(&mu_);
  auto it = crls_.find(certificate_info.Issuer());
  if (it == crls_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace experimental
}  // namespace grpc_core
