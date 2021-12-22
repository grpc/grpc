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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#include <openssl/ssl.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/stat.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

CertificateProviderWatcherNotifier::CertificateProviderWatcherNotifier(DataWatcherCertificateProvider* provider) : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()), provider_(provider) {
  distributor_->SetWatchStatusCallback([this](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    GPR_ASSERT(provider_ != nullptr);
    absl::optional<std::string> root_certificate;
    absl::optional<PemKeyCertPairList> pem_key_cert_pairs;
    CertificateProviderWatcherNotifier::WatcherInfo* info = nullptr;
    {
      MutexLock lock(&mu_);
      info = &watcher_info_[cert_name];
    }
    std::string root_certs_being_cached = provider_->root_certificate();
    if (!info->root_being_watched && root_being_watched &&
        !root_certs_being_cached.empty()) {
      root_certificate = root_certs_being_cached;
    }
    info->root_being_watched = root_being_watched;
    PemKeyCertPairList identity_certs_being_cached = provider_->pem_key_cert_pairs();
    if (!info->identity_being_watched && identity_being_watched &&
        !identity_certs_being_cached.empty()) {
      GPR_ASSERT(provider_ != nullptr);
      pem_key_cert_pairs = identity_certs_being_cached;
    }
    info->identity_being_watched = identity_being_watched;
    if (!info->root_being_watched && !info->identity_being_watched) {
      {
        MutexLock lock(&mu_);
        watcher_info_.erase(cert_name);
      }
    }
    const bool root_has_update = root_certificate.has_value();
    const bool identity_has_update = pem_key_cert_pairs.has_value();
    if (root_has_update || identity_has_update) {
      distributor_->SetKeyMaterials(cert_name, std::move(root_certificate),
                                    std::move(pem_key_cert_pairs));
    }
    grpc_error_handle root_cert_error = GRPC_ERROR_NONE;
    grpc_error_handle identity_cert_error = GRPC_ERROR_NONE;
    if (root_being_watched && !root_has_update) {
      root_cert_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Unable to get latest root certificates.");
    }
    if (identity_being_watched && !identity_has_update) {
      identity_cert_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Unable to get latest identity certificates.");
    }
    if (root_cert_error != GRPC_ERROR_NONE ||
        identity_cert_error != GRPC_ERROR_NONE) {
      distributor_->SetErrorForCert(cert_name, root_cert_error,
                                    identity_cert_error);
    }
  });
}

void CertificateProviderWatcherNotifier::SetRootCertificate(std::string root_certificate) {
  MutexLock lock(&mu_);
  ExecCtx exec_ctx;
  grpc_error_handle root_cert_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Unable to get latest root certificates.");
  for (const auto& p : watcher_info_) {
    const std::string& cert_name = p.first;
    const WatcherInfo& info = p.second;
    absl::optional<std::string> root_to_report;
    if (info.root_being_watched) {
      root_to_report = root_certificate;
    }
    if (root_to_report.has_value() && !root_to_report.value().empty()) {
      distributor_->SetKeyMaterials(cert_name, std::move(root_to_report),
                                    absl::nullopt);
    }
    if (info.root_being_watched && root_certificate.empty()) {
      distributor_->SetErrorForCert(cert_name, GRPC_ERROR_REF(root_cert_error),
                                    absl::nullopt);
    }
  }
  GRPC_ERROR_UNREF(root_cert_error);
}

void CertificateProviderWatcherNotifier::SetKeyCertificatePairs(PemKeyCertPairList pem_key_cert_pairs) {
  MutexLock lock(&mu_);
  ExecCtx exec_ctx;
  grpc_error_handle identity_cert_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Unable to get latest identity certificates.");
  for (const auto& p : watcher_info_) {
    const std::string& cert_name = p.first;
    const WatcherInfo& info = p.second;
    absl::optional<grpc_core::PemKeyCertPairList> identity_to_report;
    if (info.identity_being_watched) {
      identity_to_report = pem_key_cert_pairs;
    }
    if (identity_to_report.has_value() && !identity_to_report.value().empty()) {
      distributor_->SetKeyMaterials(cert_name, absl::nullopt,
                                    std::move(identity_to_report));
    }
    if (info.identity_being_watched && pem_key_cert_pairs.empty()) {
      distributor_->SetErrorForCert(cert_name, absl::nullopt,
                                    GRPC_ERROR_REF(identity_cert_error));
    }
  }
  GRPC_ERROR_UNREF(identity_cert_error);
}

CertificateProviderWatcherNotifier::~CertificateProviderWatcherNotifier() {
  // Reset distributor's callback to make sure the callback won't be invoked
  // again after this object(provider) is destroyed.
  distributor_->SetWatchStatusCallback(nullptr);
}

/*void CertificateProviderWatcherNotifier::SetRootCertificateAndKeyCertificatePairs(
      std::string root_certificate, PemKeyCertPairList pem_key_cert_pairs);*/

DataWatcherCertificateProvider::DataWatcherCertificateProvider() : distributor_notifier_(MakeRefCounted<CertificateProviderWatcherNotifier>(this)) {

}

absl::Status DataWatcherCertificateProvider::SetRootCertificate(
    std::string root_certificate) {
  MutexLock lock(&mu_);
  if (root_certificate_ == root_certificate) {
    gpr_log(GPR_INFO, "The root certs have not changed.");
    return absl::OkStatus();
  }
  root_certificate_ = root_certificate;
  distributor_notifier_->SetRootCertificate(std::move(root_certificate));
  return absl::OkStatus();
}

absl::Status DataWatcherCertificateProvider::SetKeyCertificatePairs(
    PemKeyCertPairList pem_key_cert_pairs) {
  MutexLock lock(&mu_);
  if (pem_key_cert_pairs == pem_key_cert_pairs_) {
    gpr_log(GPR_INFO, "The key-cert pair list has not changed.");
    return absl::OkStatus();
  }
  absl::StatusOr<bool> match_result =
      PrivateKeyAndCertificateMatch(pem_key_cert_pairs);
  if (!match_result.ok()) {
    gpr_log(GPR_ERROR, "The key-cert match check failed: %s",
            std::string(match_result.status().message()).c_str());
    return match_result.status();
  }
  if (!(*match_result)) {
    std::string error_message = "the key-cert pair list contains invalid pair(s)";
    gpr_log(GPR_ERROR, "The key-cert match check failed: %s", error_message.c_str());
    return absl::InvalidArgumentError(error_message);
  }
  pem_key_cert_pairs_ = pem_key_cert_pairs;
  distributor_notifier_->SetKeyCertificatePairs(std::move(pem_key_cert_pairs));
  return absl::OkStatus();
}

std::string DataWatcherCertificateProvider::root_certificate() {
  MutexLock lock(&mu_);
  return root_certificate_;
}

PemKeyCertPairList DataWatcherCertificateProvider::pem_key_cert_pairs() {
  MutexLock lock(&mu_);
  return pem_key_cert_pairs_;
}

namespace {

gpr_timespec TimeoutSecondsToDeadline(int64_t seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

}  // namespace

FileWatcherCertificateProvider::FileWatcherCertificateProvider(
    std::string private_key_path, std::string identity_certificate_path,
    std::string root_cert_path, unsigned int refresh_interval_sec)
    : private_key_path_(std::move(private_key_path)),
      identity_certificate_path_(std::move(identity_certificate_path)),
      root_cert_path_(std::move(root_cert_path)),
      refresh_interval_sec_(refresh_interval_sec),
      distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()) {
  // Private key and identity cert files must be both set or both unset.
  GPR_ASSERT(private_key_path_.empty() == identity_certificate_path_.empty());
  // Must be watching either root or identity certs.
  GPR_ASSERT(!private_key_path_.empty() || !root_cert_path_.empty());
  gpr_event_init(&shutdown_event_);
  ForceUpdate();
  auto thread_lambda = [](void* arg) {
    FileWatcherCertificateProvider* provider =
        static_cast<FileWatcherCertificateProvider*>(arg);
    GPR_ASSERT(provider != nullptr);
    while (true) {
      void* value = gpr_event_wait(
          &provider->shutdown_event_,
          TimeoutSecondsToDeadline(provider->refresh_interval_sec_));
      if (value != nullptr) {
        return;
      };
      provider->ForceUpdate();
    }
  };
  refresh_thread_ = Thread("FileWatcherCertificateProvider_refreshing_thread",
                           thread_lambda, this);
  refresh_thread_.Start();
  distributor_->SetWatchStatusCallback([this](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    MutexLock lock(&mu_);
    absl::optional<std::string> root_certificate;
    absl::optional<PemKeyCertPairList> pem_key_cert_pairs;
    FileWatcherCertificateProvider::WatcherInfo& info =
        watcher_info_[cert_name];
    if (!info.root_being_watched && root_being_watched &&
        !root_certificate_.empty()) {
      root_certificate = root_certificate_;
    }
    info.root_being_watched = root_being_watched;
    if (!info.identity_being_watched && identity_being_watched &&
        !pem_key_cert_pairs_.empty()) {
      pem_key_cert_pairs = pem_key_cert_pairs_;
    }
    info.identity_being_watched = identity_being_watched;
    if (!info.root_being_watched && !info.identity_being_watched) {
      watcher_info_.erase(cert_name);
    }
    ExecCtx exec_ctx;
    if (root_certificate.has_value() || pem_key_cert_pairs.has_value()) {
      distributor_->SetKeyMaterials(cert_name, root_certificate,
                                    pem_key_cert_pairs);
    }
    grpc_error_handle root_cert_error = GRPC_ERROR_NONE;
    grpc_error_handle identity_cert_error = GRPC_ERROR_NONE;
    if (root_being_watched && !root_certificate.has_value()) {
      root_cert_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Unable to get latest root certificates.");
    }
    if (identity_being_watched && !pem_key_cert_pairs.has_value()) {
      identity_cert_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Unable to get latest identity certificates.");
    }
    if (root_cert_error != GRPC_ERROR_NONE ||
        identity_cert_error != GRPC_ERROR_NONE) {
      distributor_->SetErrorForCert(cert_name, root_cert_error,
                                    identity_cert_error);
    }
  });
}

FileWatcherCertificateProvider::~FileWatcherCertificateProvider() {
  // Reset distributor's callback to make sure the callback won't be invoked
  // again after this object(provider) is destroyed.
  distributor_->SetWatchStatusCallback(nullptr);
  gpr_event_set(&shutdown_event_, reinterpret_cast<void*>(1));
  refresh_thread_.Join();
}

void FileWatcherCertificateProvider::ForceUpdate() {
  absl::optional<std::string> root_certificate;
  absl::optional<PemKeyCertPairList> pem_key_cert_pairs;
  if (!root_cert_path_.empty()) {
    root_certificate = ReadRootCertificatesFromFile(root_cert_path_);
  }
  if (!private_key_path_.empty()) {
    pem_key_cert_pairs = ReadIdentityKeyCertPairFromFiles(
        private_key_path_, identity_certificate_path_);
  }
  MutexLock lock(&mu_);
  const bool root_cert_changed =
      (!root_certificate.has_value() && !root_certificate_.empty()) ||
      (root_certificate.has_value() && root_certificate_ != *root_certificate);
  if (root_cert_changed) {
    if (root_certificate.has_value()) {
      root_certificate_ = std::move(*root_certificate);
    } else {
      root_certificate_ = "";
    }
  }
  const bool identity_cert_changed =
      (!pem_key_cert_pairs.has_value() && !pem_key_cert_pairs_.empty()) ||
      (pem_key_cert_pairs.has_value() &&
       pem_key_cert_pairs_ != *pem_key_cert_pairs);
  if (identity_cert_changed) {
    if (pem_key_cert_pairs.has_value()) {
      pem_key_cert_pairs_ = std::move(*pem_key_cert_pairs);
    } else {
      pem_key_cert_pairs_ = {};
    }
  }
  if (root_cert_changed || identity_cert_changed) {
    ExecCtx exec_ctx;
    grpc_error_handle root_cert_error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Unable to get latest root certificates.");
    grpc_error_handle identity_cert_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "Unable to get latest identity certificates.");
    for (const auto& p : watcher_info_) {
      const std::string& cert_name = p.first;
      const WatcherInfo& info = p.second;
      absl::optional<std::string> root_to_report;
      absl::optional<PemKeyCertPairList> identity_to_report;
      // Set key materials to the distributor if their contents changed.
      if (info.root_being_watched && !root_certificate_.empty() &&
          root_cert_changed) {
        root_to_report = root_certificate_;
      }
      if (info.identity_being_watched && !pem_key_cert_pairs_.empty() &&
          identity_cert_changed) {
        identity_to_report = pem_key_cert_pairs_;
      }
      if (root_to_report.has_value() || identity_to_report.has_value()) {
        distributor_->SetKeyMaterials(cert_name, std::move(root_to_report),
                                      std::move(identity_to_report));
      }
      // Report errors to the distributor if the contents are empty.
      const bool report_root_error =
          info.root_being_watched && root_certificate_.empty();
      const bool report_identity_error =
          info.identity_being_watched && pem_key_cert_pairs_.empty();
      if (report_root_error || report_identity_error) {
        distributor_->SetErrorForCert(
            cert_name,
            report_root_error ? GRPC_ERROR_REF(root_cert_error)
                              : GRPC_ERROR_NONE,
            report_identity_error ? GRPC_ERROR_REF(identity_cert_error)
                                  : GRPC_ERROR_NONE);
      }
    }
    GRPC_ERROR_UNREF(root_cert_error);
    GRPC_ERROR_UNREF(identity_cert_error);
  }
}

absl::optional<std::string>
FileWatcherCertificateProvider::ReadRootCertificatesFromFile(
    const std::string& root_cert_full_path) {
  // Read the root file.
  grpc_slice root_slice = grpc_empty_slice();
  grpc_error_handle root_error =
      grpc_load_file(root_cert_full_path.c_str(), 0, &root_slice);
  if (root_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Reading file %s failed: %s",
            root_cert_full_path.c_str(),
            grpc_error_std_string(root_error).c_str());
    GRPC_ERROR_UNREF(root_error);
    return absl::nullopt;
  }
  std::string root_cert(StringViewFromSlice(root_slice));
  grpc_slice_unref_internal(root_slice);
  return root_cert;
}

namespace {

// This helper function gets the last-modified time of |filename|. When failed,
// it logs the error and returns 0.
time_t GetModificationTime(const char* filename) {
  time_t ts = 0;
  absl::Status status = GetFileModificationTime(filename, &ts);
  return ts;
}

}  // namespace

absl::optional<PemKeyCertPairList>
FileWatcherCertificateProvider::ReadIdentityKeyCertPairFromFiles(
    const std::string& private_key_path,
    const std::string& identity_certificate_path) {
  struct SliceWrapper {
    grpc_slice slice = grpc_empty_slice();
    ~SliceWrapper() { grpc_slice_unref_internal(slice); }
  };
  const int kNumRetryAttempts = 3;
  for (int i = 0; i < kNumRetryAttempts; ++i) {
    // TODO(ZhenLian): replace the timestamp approach with key-match approach
    //  once the latter is implemented.
    // Checking the last modification of identity files before reading.
    time_t identity_key_ts_before =
        GetModificationTime(private_key_path.c_str());
    if (identity_key_ts_before == 0) {
      gpr_log(
          GPR_ERROR,
          "Failed to get the file's modification time of %s. Start retrying...",
          private_key_path.c_str());
      continue;
    }
    time_t identity_cert_ts_before =
        GetModificationTime(identity_certificate_path.c_str());
    if (identity_cert_ts_before == 0) {
      gpr_log(
          GPR_ERROR,
          "Failed to get the file's modification time of %s. Start retrying...",
          identity_certificate_path.c_str());
      continue;
    }
    // Read the identity files.
    SliceWrapper key_slice, cert_slice;
    grpc_error_handle key_error =
        grpc_load_file(private_key_path.c_str(), 0, &key_slice.slice);
    if (key_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Reading file %s failed: %s. Start retrying...",
              private_key_path.c_str(),
              grpc_error_std_string(key_error).c_str());
      GRPC_ERROR_UNREF(key_error);
      continue;
    }
    grpc_error_handle cert_error =
        grpc_load_file(identity_certificate_path.c_str(), 0, &cert_slice.slice);
    if (cert_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Reading file %s failed: %s. Start retrying...",
              identity_certificate_path.c_str(),
              grpc_error_std_string(cert_error).c_str());
      GRPC_ERROR_UNREF(cert_error);
      continue;
    }
    std::string private_key(StringViewFromSlice(key_slice.slice));
    std::string cert_chain(StringViewFromSlice(cert_slice.slice));
    PemKeyCertPairList identity_pairs;
    identity_pairs.emplace_back(private_key, cert_chain);
    // Checking the last modification of identity files before reading.
    time_t identity_key_ts_after =
        GetModificationTime(private_key_path.c_str());
    if (identity_key_ts_before != identity_key_ts_after) {
      gpr_log(GPR_ERROR,
              "Last modified time before and after reading %s is not the same. "
              "Start retrying...",
              private_key_path.c_str());
      continue;
    }
    time_t identity_cert_ts_after =
        GetModificationTime(identity_certificate_path.c_str());
    if (identity_cert_ts_before != identity_cert_ts_after) {
      gpr_log(GPR_ERROR,
              "Last modified time before and after reading %s is not the same. "
              "Start retrying...",
              identity_certificate_path.c_str());
      continue;
    }
    return identity_pairs;
  }
  gpr_log(GPR_ERROR,
          "All retry attempts failed. Will try again after the next interval.");
  return absl::nullopt;
}

absl::StatusOr<bool> PrivateKeyAndCertificateMatch(
    absl::string_view private_key, absl::string_view cert_chain) {
  if (private_key.empty()) {
    return absl::InvalidArgumentError("Private key string is empty.");
  }
  if (cert_chain.empty()) {
    return absl::InvalidArgumentError("Certificate string is empty.");
  }
  BIO* cert_bio = BIO_new_mem_buf(cert_chain.data(), cert_chain.size());
  if (cert_bio == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from certificate string to BIO failed.");
  }
  // Reads the first cert from the cert_chain which is expected to be the leaf
  // cert
  X509* x509 = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
  BIO_free(cert_bio);
  if (x509 == nullptr) {
    return absl::InvalidArgumentError(
        "Conversion from PEM string to X509 failed.");
  }
  EVP_PKEY* public_evp_pkey = X509_get_pubkey(x509);
  X509_free(x509);
  if (public_evp_pkey == nullptr) {
    return absl::InvalidArgumentError(
        "Extraction of public key from x.509 certificate failed.");
  }
  BIO* private_key_bio =
      BIO_new_mem_buf(private_key.data(), private_key.size());
  if (private_key_bio == nullptr) {
    EVP_PKEY_free(public_evp_pkey);
    return absl::InvalidArgumentError(
        "Conversion from private key string to BIO failed.");
  }
  EVP_PKEY* private_evp_pkey =
      PEM_read_bio_PrivateKey(private_key_bio, nullptr, nullptr, nullptr);
  BIO_free(private_key_bio);
  if (private_evp_pkey == nullptr) {
    EVP_PKEY_free(public_evp_pkey);
    return absl::InvalidArgumentError(
        "Conversion from PEM string to EVP_PKEY failed.");
  }
  bool result = EVP_PKEY_cmp(private_evp_pkey, public_evp_pkey) == 1;
  EVP_PKEY_free(private_evp_pkey);
  EVP_PKEY_free(public_evp_pkey);
  return result;
}

absl::StatusOr<bool> PrivateKeyAndCertificateMatch(
    const PemKeyCertPairList& pair_list) {
  for (size_t i = 0; i < pair_list.size(); ++i) {
    absl::StatusOr<bool> matched_or = PrivateKeyAndCertificateMatch(
        pair_list[i].private_key(), pair_list[i].cert_chain());
    if (!(matched_or.ok() && *matched_or)) {
      return matched_or;
    }
  }
  return true;
}

}  // namespace grpc_core

/** -- Wrapper APIs declared in grpc_security.h -- **/

namespace {

grpc_core::PemKeyCertPairList ConvertToCoreObject(
    grpc_tls_identity_pairs* pem_key_cert_pairs) {
  grpc_core::PemKeyCertPairList identity_pairs_core = {};
  if (pem_key_cert_pairs != nullptr) {
    identity_pairs_core = std::move(pem_key_cert_pairs->pem_key_cert_pairs);
    delete pem_key_cert_pairs;
  }
  return identity_pairs_core;
}

std::string ConvertToCoreObject(const char* root_certificate) {
  return root_certificate == nullptr ? "" : root_certificate;
}

}  // namespace

grpc_tls_certificate_provider* grpc_tls_certificate_provider_data_watcher_create() {
  grpc_core::ExecCtx exec_ctx;
  return new grpc_core::DataWatcherCertificateProvider();
}

grpc_status_code grpc_tls_certificate_provider_data_watcher_set_root_cert(
    grpc_tls_certificate_provider* provider, const char* root_certificate,
    char** error_details) {
  GPR_ASSERT(provider != nullptr && root_certificate != nullptr);
  grpc_core::DataWatcherCertificateProvider* data_provider =
      dynamic_cast<grpc_core::DataWatcherCertificateProvider*>(provider);
  absl::Status status =
      data_provider->SetRootCertificate(ConvertToCoreObject(root_certificate));
  if (!status.ok()) {
    *error_details = gpr_strdup(std::string(status.message()).c_str());
  }
  return static_cast<grpc_status_code>(status.code());
}

grpc_status_code grpc_tls_certificate_provider_data_watcher_set_key_cert_pairs(
    grpc_tls_certificate_provider* provider,
    grpc_tls_identity_pairs* pem_key_cert_pairs, char** error_details) {
  GPR_ASSERT(provider != nullptr && pem_key_cert_pairs != nullptr);
  grpc_core::DataWatcherCertificateProvider* data_provider =
      dynamic_cast<grpc_core::DataWatcherCertificateProvider*>(provider);
  absl::Status status = data_provider->SetKeyCertificatePairs(
      ConvertToCoreObject(pem_key_cert_pairs));
  if (!status.ok()) {
    *error_details = gpr_strdup(std::string(status.message()).c_str());
  }
  return static_cast<grpc_status_code>(status.code());
}

grpc_tls_certificate_provider*
grpc_tls_certificate_provider_file_watcher_create(
    const char* private_key_path, const char* identity_certificate_path,
    const char* root_cert_path, unsigned int refresh_interval_sec) {
  grpc_core::ExecCtx exec_ctx;
  return new grpc_core::FileWatcherCertificateProvider(
      private_key_path == nullptr ? "" : private_key_path,
      identity_certificate_path == nullptr ? "" : identity_certificate_path,
      root_cert_path == nullptr ? "" : root_cert_path, refresh_interval_sec);
}

void grpc_tls_certificate_provider_release(
    grpc_tls_certificate_provider* provider) {
  GRPC_API_TRACE("grpc_tls_certificate_provider_release(provider=%p)", 1,
                 (provider));
  grpc_core::ExecCtx exec_ctx;
  if (provider != nullptr) provider->Unref();
}
