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
    if (!root_being_watched && !identity_being_watched) return;
    absl::optional<std::string> root_certificate;
    absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
    if (root_being_watched) {
      root_certificate = root_certificate_;
    }
    if (identity_being_watched) {
      pem_key_cert_pairs = pem_key_cert_pairs_;
    }
    distributor_->SetKeyMaterials(cert_name, std::move(root_certificate),
                                  std::move(pem_key_cert_pairs));
  });
}

gpr_timespec grpc_timeout_seconds_to_deadline(int64_t time_s) {
  return gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_millis(static_cast<int64_t>(1e3) * time_s, GPR_TIMESPAN));
}

FileWatcherCertificateProvider::FileWatcherCertificateProvider(
    std::string identity_key_cert_directory, std::string private_key_file_name,
    std::string identity_certificate_file_name, std::string root_cert_full_path,
    unsigned int refresh_interval_sec)
    : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()),
      identity_key_cert_directory_(std::move(identity_key_cert_directory)),
      private_key_file_name_(std::move(private_key_file_name)),
      identity_certificate_file_name_(
          std::move(identity_certificate_file_name)),
      root_cert_full_path_(std::move(root_cert_full_path)),
      refresh_interval_sec_(refresh_interval_sec) {
  gpr_event_init(&event_);
  auto thread_lambda = [](void* arg) {
    FileWatcherCertificateProvider* provider =
        static_cast<FileWatcherCertificateProvider*>(arg);
    GPR_ASSERT(provider != nullptr);
    while (true) {
      absl::optional<std::string> root_certificate;
      absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
      if (!provider->root_cert_full_path_.empty()) {
        root_certificate = provider->ReadRootCertificatesFromFile(
            provider->root_cert_full_path_);
      }
      if (!provider->identity_key_cert_directory_.empty()) {
        pem_key_cert_pairs = provider->ReadIdentityKeyCertPairFromFiles(
            provider->identity_key_cert_directory_,
            provider->private_key_file_name_,
            provider->identity_certificate_file_name_);
      }
      if (root_certificate.has_value() || pem_key_cert_pairs.has_value()) {
        grpc_core::MutexLock lock(&provider->mu_);
        grpc_core::ExecCtx exec_ctx;
        bool root_changed = false;
        bool identity_changed = false;
        if (root_certificate.has_value()) {
          if (provider->root_certificate_ != *root_certificate) {
            root_changed = true;
            provider->root_certificate_ = *root_certificate;
          }
        }
        if (pem_key_cert_pairs.has_value()) {
          if (provider->pem_key_cert_pairs_ != *pem_key_cert_pairs) {
            identity_changed = true;
            provider->pem_key_cert_pairs_ = *pem_key_cert_pairs;
          }
        }
        for (const auto& info : provider->watcher_info_) {
          // We will push the updates regardless of whether the
          // root/identity certificates are being watched right now.
          const std::string& cert_name = info.first;
          if (root_changed || identity_changed) {
            provider->distributor_->SetKeyMaterials(cert_name, root_certificate,
                                                    pem_key_cert_pairs);
          }
        }
      }
      void* value = gpr_event_wait(
          &provider->event_,
          grpc_timeout_seconds_to_deadline(provider->refresh_interval_sec_));
      if (value != nullptr) {
        return;
      };
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
    if (!info.root_being_watched && root_being_watched) {
      if (!root_cert_full_path_.empty()) {
        if (root_certificate_.empty()) {
          // If |root_certificate_| is empty, it could be the case that the
          // refreshing thread is good, but hasn't delivered the result to the
          // provider yet. We will do an extra read here.
          root_certificate =
              FileWatcherCertificateProvider::ReadRootCertificatesFromFile(
                  root_cert_full_path_);
        } else {
          root_certificate = root_certificate_;
        }
      }
    }
    info.root_being_watched = root_being_watched;
    if (!info.identity_being_watched && identity_being_watched) {
      if (!identity_key_cert_directory_.empty()) {
        if (pem_key_cert_pairs_.empty()) {
          // If |pem_key_cert_pairs_| is empty, it could be the case that the
          // refreshing thread is good, but hasn't delivered the result to the
          // provider yet. We will do an extra read here.
          pem_key_cert_pairs =
              FileWatcherCertificateProvider::ReadIdentityKeyCertPairFromFiles(
                  identity_key_cert_directory_, private_key_file_name_,
                  identity_certificate_file_name_);
        } else {
          pem_key_cert_pairs = pem_key_cert_pairs_;
        }
      }
    }
    info.identity_being_watched = identity_being_watched;
    if (!info.root_being_watched && !info.identity_being_watched) {
      watcher_info_.erase(cert_name);
    }
    if (!root_certificate.has_value() && !pem_key_cert_pairs.has_value()) {
      return;
    }
    distributor_->SetKeyMaterials(cert_name, std::move(root_certificate),
                                  std::move(pem_key_cert_pairs));
  });
}

FileWatcherCertificateProvider::~FileWatcherCertificateProvider() {
  gpr_event_set(&event_, (void*)(1));
  refresh_thread_.Join();
  // Reset distributor's callback to make sure the callback won't be invoked
  // again after this object(provider) is destroyed.
  distributor_->SetWatchStatusCallback(nullptr);
}

absl::optional<std::string>
FileWatcherCertificateProvider::ReadRootCertificatesFromFile(
    const std::string& root_cert_full_path) {
  // Read the root file.
  grpc_slice root_slice;
  grpc_error* root_error =
      grpc_load_file(root_cert_full_path.c_str(), 1, &root_slice);
  if (root_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Reading file %s failed: %s",
            root_cert_full_path.c_str(), grpc_error_string(root_error));
    GRPC_ERROR_UNREF(root_error);
  }
  std::string root_cert(StringViewFromSlice(root_slice));
  grpc_slice_unref_internal(root_slice);
  return root_cert;
}

// This helper function gets the last-modified time of |filename|. When failed,
// it logs the error and returns 0.
time_t get_modified_time(const char* filename) {
  time_t ts = 0;
  absl::Status status = grpc_core::GetFileModificationTime(filename, &ts);
  if (!status.ok() || ts == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s", filename,
            std::string(status.message()).c_str());
  };
  return ts;
}

absl::optional<PemKeyCertPairList>
FileWatcherCertificateProvider::ReadIdentityKeyCertPairFromFiles(
    const std::string& identity_key_cert_directory,
    const std::string& private_key_file_name,
    const std::string& identity_certificate_file_name) {
  // TODO(ZhenLian): need another PR to make it also applicable to Windows.
  std::string private_key_full_path =
      identity_key_cert_directory + "/" + private_key_file_name;
  std::string identity_cert_full_path =
      identity_key_cert_directory + "/" + identity_certificate_file_name;
  const int retry_default_times = 3;
  for (int i = 0; i < retry_default_times; ++i) {
    // Checking the last modification of identity directory and files before
    // reading.
    time_t identity_dir_ts_before =
        get_modified_time(identity_key_cert_directory.c_str());
    time_t identity_key_ts_before =
        get_modified_time(private_key_full_path.c_str());
    time_t identity_cert_ts_before =
        get_modified_time(identity_cert_full_path.c_str());
    if (identity_dir_ts_before == 0 || identity_key_ts_before == 0 ||
        identity_cert_ts_before == 0) {
      continue;
    }
    // Read the identity files.
    grpc_slice key_slice, cert_slice;
    grpc_error* key_error =
        grpc_load_file(private_key_full_path.c_str(), 1, &key_slice);
    if (key_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Reading file %s failed: %s",
              private_key_full_path.c_str(), grpc_error_string(key_error));
      GRPC_ERROR_UNREF(key_error);
      continue;
    }
    grpc_error* cert_error =
        grpc_load_file(identity_cert_full_path.c_str(), 1, &cert_slice);
    if (cert_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Reading file %s failed: %s",
              identity_cert_full_path.c_str(), grpc_error_string(cert_error));
      GRPC_ERROR_UNREF(cert_error);
      continue;
    }
    PemKeyCertPairList identity_pairs;
    grpc_ssl_pem_key_cert_pair* ssl_pair =
        static_cast<grpc_ssl_pem_key_cert_pair*>(
            gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
    ssl_pair->private_key = gpr_strdup(
        reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(key_slice)));
    ssl_pair->cert_chain = gpr_strdup(
        reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(cert_slice)));
    identity_pairs.emplace_back(ssl_pair);
    grpc_slice_unref_internal(cert_slice);
    grpc_slice_unref_internal(key_slice);
    // Checking the last modification of identity directory and files before
    // reading.
    time_t identity_dir_ts_after =
        get_modified_time(identity_key_cert_directory.c_str());
    // Checking if the contents read just now were valid.
    if (identity_dir_ts_before != identity_dir_ts_after) {
      gpr_log(GPR_ERROR,
              "Last modified time before and after reading %s is not the same. "
              "Skip this read",
              identity_key_cert_directory.c_str());
      continue;
    }
    time_t identity_key_ts_after =
        get_modified_time(private_key_full_path.c_str());
    if (identity_key_ts_before != identity_key_ts_after) {
      gpr_log(GPR_ERROR,
              "Last modified time before and after reading %s is not the same. "
              "Skip this read",
              private_key_full_path.c_str());
      continue;
    }
    time_t identity_cert_ts_after =
        get_modified_time(identity_cert_full_path.c_str());
    if (identity_cert_ts_before != identity_cert_ts_after) {
      gpr_log(GPR_ERROR,
              "Last modified time before and after reading %s is not the same. "
              "Skip this read",
              identity_cert_full_path.c_str());
      continue;
    }
    return identity_pairs;
  }
  return absl::nullopt;
}

}  // namespace grpc_core

/** -- Wrapper APIs declared in grpc_security.h -- **/

grpc_tls_certificate_provider* grpc_tls_certificate_provider_static_data_create(
    const char* root_certificate, grpc_tls_identity_pairs* pem_key_cert_pairs) {
  GPR_ASSERT(root_certificate != nullptr || pem_key_cert_pairs != nullptr);
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
    const char* identity_key_cert_directory, const char* private_key_file_name,
    const char* identity_certificate_file_name, const char* root_cert_full_path,
    unsigned int refresh_interval_sec) {
  GPR_ASSERT(root_cert_full_path != nullptr ||
             identity_key_cert_directory != nullptr);
  if (identity_key_cert_directory != nullptr) {
    GPR_ASSERT(private_key_file_name != nullptr &&
               identity_certificate_file_name != nullptr);
  }
  return new grpc_core::FileWatcherCertificateProvider(
      identity_key_cert_directory, private_key_file_name,
      identity_certificate_file_name, root_cert_full_path,
      refresh_interval_sec);
}

void grpc_tls_certificate_provider_release(
    grpc_tls_certificate_provider* provider) {
  GRPC_API_TRACE("grpc_tls_certificate_provider_release(provider=%p)", 1,
                 (provider));
  grpc_core::ExecCtx exec_ctx;
  if (provider != nullptr) provider->Unref();
}
