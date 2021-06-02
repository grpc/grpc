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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/stat.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

StaticDataCertificateProvider::StaticDataCertificateProvider(
    std::string root_certificate,
    grpc_core::PemKeyCertPairList pem_key_cert_pairs)
    : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()),
      root_certificate_(std::move(root_certificate)),
      pem_key_cert_pairs_(std::move(pem_key_cert_pairs)) {
  distributor_->SetWatchStatusCallback([this](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    grpc_core::MutexLock lock(&mu_);
    absl::optional<std::string> root_certificate;
    absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
    StaticDataCertificateProvider::WatcherInfo& info = watcher_info_[cert_name];
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

StaticDataCertificateProvider::~StaticDataCertificateProvider() {
  // Reset distributor's callback to make sure the callback won't be invoked
  // again after this object(provider) is destroyed.
  distributor_->SetWatchStatusCallback(nullptr);
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
  refresh_thread_ = grpc_core::Thread(
      "FileWatcherCertificateProvider_refreshing_thread", thread_lambda, this);
  refresh_thread_.Start();
  distributor_->SetWatchStatusCallback([this](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    grpc_core::MutexLock lock(&mu_);
    absl::optional<std::string> root_certificate;
    absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
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
  absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
  if (!root_cert_path_.empty()) {
    root_certificate = ReadRootCertificatesFromFile(root_cert_path_);
  }
  if (!private_key_path_.empty()) {
    pem_key_cert_pairs = ReadIdentityKeyCertPairFromFiles(
        private_key_path_, identity_certificate_path_);
  }
  grpc_core::MutexLock lock(&mu_);
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
      absl::optional<grpc_core::PemKeyCertPairList> identity_to_report;
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
  absl::Status status = grpc_core::GetFileModificationTime(filename, &ts);
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

}  // namespace grpc_core

/** -- Wrapper APIs declared in grpc_security.h -- **/

grpc_tls_certificate_provider* grpc_tls_certificate_provider_static_data_create(
    const char* root_certificate, grpc_tls_identity_pairs* pem_key_cert_pairs) {
  GPR_ASSERT(root_certificate != nullptr || pem_key_cert_pairs != nullptr);
  grpc_core::ExecCtx exec_ctx;
  grpc_core::PemKeyCertPairList identity_pairs_core;
  if (pem_key_cert_pairs != nullptr) {
    identity_pairs_core = std::move(pem_key_cert_pairs->pem_key_cert_pairs);
    delete pem_key_cert_pairs;
  }
  std::string root_cert_core;
  if (root_certificate != nullptr) {
    root_cert_core = root_certificate;
  }
  return new grpc_core::StaticDataCertificateProvider(
      std::move(root_cert_core), std::move(identity_pairs_core));
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
