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

FileWatcherCertificateProvider::FileWatcherCertificateProvider(
    std::string identity_key_cert_directory, std::string private_key_file_name,
    std::string identity_certificate_file_name, std::string root_cert_full_path,
    unsigned int refresh_interval_sec)
    : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()) {
  refresh_thread_ =
      std::thread([identity_key_cert_directory, private_key_file_name,
                   identity_certificate_file_name, root_cert_full_path,
                   refresh_interval_sec, this]() {
        while (true) {
          {
            grpc_core::MutexLock lock(&mu_);
            absl::optional<std::string> root_certificate;
            absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
            if (!root_cert_full_path.empty()) {
              root_certificate = FileWatcherCertificateProvider::
                  ReadRootCertificatesFromFileLocked(root_cert_full_path);
            }
            if (!identity_key_cert_directory.empty() &&
                !private_key_file_name.empty() &&
                !identity_certificate_file_name.empty()) {
              pem_key_cert_pairs = FileWatcherCertificateProvider::
                  ReadIdentityKeyCertPairFromFilesLocked(
                      identity_key_cert_directory, private_key_file_name,
                      identity_certificate_file_name);
            }
            if (root_certificate != absl::nullopt ||
                pem_key_cert_pairs != absl::nullopt) {
              // TODO(ZhenLian): possible improvement: cache the root and
              // identity certs. If the ones being updated are the same as those
              // currently cached, skip.
              for (std::pair<std::string, WatcherInfo> info : watcher_info_) {
                // We will push the updates regardless of whether the
                // root/identity certificates are being watched right now.
                std::string cert_name = info.first;
                distributor_->SetKeyMaterials(cert_name,
                                              std::move(root_certificate),
                                              std::move(pem_key_cert_pairs));
              }
            }
          }
          if (is_shutdown()) return;
          std::this_thread::sleep_for(
              std::chrono::milliseconds(refresh_interval_sec));
          if (is_shutdown()) return;
        }
      });
  distributor_->SetWatchStatusCallback([identity_key_cert_directory,
                                        private_key_file_name,
                                        identity_certificate_file_name,
                                        root_cert_full_path,
                                        this](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    grpc_core::MutexLock lock(&mu_);
    absl::optional<std::string> root_certificate;
    absl::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs;
    FileWatcherCertificateProvider::WatcherInfo info = watcher_info_[cert_name];
    if (!info.root_being_watched && root_being_watched) {
      info.root_being_watched = true;
      if (!root_cert_full_path.empty()) {
        root_certificate =
            FileWatcherCertificateProvider::ReadRootCertificatesFromFileLocked(
                root_cert_full_path);
      }
    }
    if (info.root_being_watched && !root_being_watched) {
      info.root_being_watched = false;
    }
    if (!info.identity_being_watched && identity_being_watched) {
      info.identity_being_watched = true;
      if (!identity_key_cert_directory.empty() &&
          !private_key_file_name.empty() &&
          !identity_certificate_file_name.empty()) {
        pem_key_cert_pairs = FileWatcherCertificateProvider::
            ReadIdentityKeyCertPairFromFilesLocked(
                identity_key_cert_directory, private_key_file_name,
                identity_certificate_file_name);
      }
    }
    if (info.identity_being_watched && !identity_being_watched) {
      info.identity_being_watched = false;
    }
    if (!info.root_being_watched && !root_being_watched &&
        !info.identity_being_watched && !identity_being_watched) {
      watcher_info_.erase(cert_name);
    }
    if (root_certificate == absl::nullopt &&
        pem_key_cert_pairs == absl::nullopt) {
      return;
    }
    distributor_->SetKeyMaterials(cert_name, std::move(root_certificate),
                                  std::move(pem_key_cert_pairs));
  });
}

FileWatcherCertificateProvider::~FileWatcherCertificateProvider() {
  set_is_shutdown(true);
  refresh_thread_.join();
}

absl::optional<std::string>
FileWatcherCertificateProvider::ReadRootCertificatesFromFileLocked(
    const std::string& root_cert_full_path) {
  // Checking the last modification of root file before reading.
  time_t root_ts_before = 0;
  absl::Status root_status_before = grpc_core::GetFileModificationTime(
      root_cert_full_path.c_str(), &root_ts_before);
  if (root_status_before.code() != absl::StatusCode::kOk ||
      root_ts_before == 0) {
    // Question: it is said https://abseil.io/tips/1 that not safe to use
    // root_status_before.message().data. Do we have other alternatives here?
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            root_cert_full_path.c_str(),
            std::string(root_status_before.message()).c_str());
    return absl::nullopt;
  };
  // Read the root file.
  grpc_slice root_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file(root_cert_full_path.c_str(), 1, &root_slice)));
  std::string root_cert = std::string(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(root_slice)),
      GRPC_SLICE_LENGTH(root_slice));
  grpc_slice_unref(root_slice);
  // Checking the last modification of root file after reading.
  time_t root_ts_after = 0;
  absl::Status root_status_after = grpc_core::GetFileModificationTime(
      root_cert_full_path.c_str(), &root_ts_after);
  if (root_status_after.code() != absl::StatusCode::kOk || root_ts_after == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            root_cert_full_path.c_str(),
            std::string(root_status_after.message()).c_str());
    return absl::nullopt;
  };
  // Checking if the contents read just now was valid.
  if (root_ts_before != root_ts_after) {
    gpr_log(GPR_ERROR,
            "Last modified time before and after reading %s is not the same. "
            "Skip this read",
            root_cert_full_path.c_str());
    return absl::nullopt;
  }
  return root_cert;
}

absl::optional<PemKeyCertPairList>
FileWatcherCertificateProvider::ReadIdentityKeyCertPairFromFilesLocked(
    const std::string& identity_key_cert_directory,
    const std::string& private_key_file_name,
    const std::string& identity_certificate_file_name) {
  // TODO(ZhenLian): need another PR to make it also applicable to Windows.
  std::string private_key_full_path =
      identity_key_cert_directory + "/" + private_key_file_name;
  std::string identity_cert_full_path =
      identity_key_cert_directory + "/" + identity_certificate_file_name;
  // Checking the last modification of identity directory and files before
  // reading.
  time_t identity_dir_ts_before = 0;
  absl::Status identity_dir_status_before = grpc_core::GetFileModificationTime(
      identity_key_cert_directory.c_str(), &identity_dir_ts_before);
  if (identity_dir_status_before.code() != absl::StatusCode::kOk ||
      identity_dir_ts_before == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            identity_key_cert_directory.c_str(),
            std::string(identity_dir_status_before.message()).c_str());
    return absl::nullopt;
  };
  time_t identity_key_ts_before = 0;
  absl::Status identity_key_status_before = grpc_core::GetFileModificationTime(
      private_key_full_path.c_str(), &identity_key_ts_before);
  if (identity_key_status_before.code() != absl::StatusCode::kOk ||
      identity_key_ts_before == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            private_key_full_path.c_str(),
            std::string(identity_key_status_before.message()).c_str());
    return absl::nullopt;
  };
  time_t identity_cert_ts_before = 0;
  absl::Status identity_cert_status_before = grpc_core::GetFileModificationTime(
      identity_cert_full_path.c_str(), &identity_cert_ts_before);
  if (identity_cert_status_before.code() != absl::StatusCode::kOk ||
      identity_dir_ts_before == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            identity_cert_full_path.c_str(),
            std::string(identity_cert_status_before.message()).c_str());
    return absl::nullopt;
  };
  // Read the identity files.
  grpc_slice key_slice, cert_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file(private_key_full_path.c_str(), 1, &key_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file(identity_cert_full_path.c_str(), 1, &cert_slice)));
  PemKeyCertPairList identity_pairs;
  grpc_ssl_pem_key_cert_pair* ssl_pair =
      static_cast<grpc_ssl_pem_key_cert_pair*>(
          gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
  ssl_pair->private_key = gpr_strdup(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(key_slice)));
  ssl_pair->cert_chain = gpr_strdup(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(cert_slice)));
  identity_pairs.emplace_back(ssl_pair);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  // Checking the last modification of identity directory and files before
  // reading.
  time_t identity_dir_ts_after = 0;
  absl::Status identity_dir_status_after = grpc_core::GetFileModificationTime(
      identity_key_cert_directory.c_str(), &identity_dir_ts_after);
  if (identity_dir_status_after.code() != absl::StatusCode::kOk ||
      identity_dir_ts_after == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            identity_key_cert_directory.c_str(),
            std::string(identity_dir_status_after.message()).c_str());
    return absl::nullopt;
  };
  // Checking if the contents read just now were valid.
  if (identity_dir_ts_before != identity_dir_ts_after) {
    gpr_log(GPR_ERROR,
            "Last modified time before and after reading %s is not the same. "
            "Skip this read",
            identity_key_cert_directory.c_str());
    return absl::nullopt;
  }
  time_t identity_key_ts_after = 0;
  absl::Status identity_key_status_after = grpc_core::GetFileModificationTime(
      private_key_full_path.c_str(), &identity_key_ts_after);
  if (identity_key_status_after.code() != absl::StatusCode::kOk ||
      identity_key_ts_after == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            private_key_full_path.c_str(),
            std::string(identity_key_status_after.message()).c_str());
    return absl::nullopt;
  };
  if (identity_key_ts_before != identity_key_ts_after) {
    gpr_log(GPR_ERROR,
            "Last modified time before and after reading %s is not the same. "
            "Skip this read",
            private_key_full_path.c_str());
    return absl::nullopt;
  }
  time_t identity_cert_ts_after = 0;
  absl::Status identity_cert_status_after = grpc_core::GetFileModificationTime(
      identity_cert_full_path.c_str(), &identity_cert_ts_after);
  if (identity_cert_status_after.code() != absl::StatusCode::kOk ||
      identity_dir_ts_after == 0) {
    gpr_log(GPR_ERROR, "Getting modification time of %s failed: %s",
            identity_cert_full_path.c_str(),
            std::string(identity_cert_status_after.message()).c_str());
    return absl::nullopt;
  };
  if (identity_cert_ts_before != identity_cert_ts_after) {
    gpr_log(GPR_ERROR,
            "Last modified time before and after reading %s is not the same. "
            "Skip this read",
            identity_cert_full_path.c_str());
    return absl::nullopt;
  }
  return identity_pairs;
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
  bool identity_set = (identity_key_cert_directory != nullptr &&
                       private_key_file_name != nullptr &&
                       identity_certificate_file_name != nullptr);
  GPR_ASSERT(root_cert_full_path != nullptr || identity_set);
  if (!identity_set) {
    GPR_ASSERT(identity_key_cert_directory == nullptr &&
               private_key_file_name == nullptr &&
               identity_certificate_file_name == nullptr);
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
