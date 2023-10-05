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

#include <dirent.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include <openssl/bio.h>
#include <openssl/mem.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {
namespace experimental {
using ::absl::Mutex;
using ::std::thread;

namespace {
std::string IssuerFromCrl(X509_CRL* crl) {
  char* buf = X509_NAME_oneline(X509_CRL_get_issuer(crl), nullptr, 0);
  std::string ret;
  if (buf != nullptr) {
    ret = buf;
  }
  OPENSSL_free(buf);
  return ret;
}

gpr_timespec TimeoutSecondsToDeadline(int64_t seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

absl::StatusOr<std::shared_ptr<Crl>> ReadCrlFromFile(
    absl::string_view crl_path) {
  grpc_slice crl_slice = grpc_empty_slice();
  grpc_error_handle err = grpc_load_file(crl_path.data(), 1, &crl_slice);
  if (!err.ok()) {
    gpr_log(GPR_ERROR, "Error reading file %s", err.message().data());
    // TODO(gtcooke94) log error
    return absl::InvalidArgumentError("Could not load file");
  }
  std::string raw_crl = std::string(StringViewFromSlice(crl_slice));
  absl::StatusOr<std::unique_ptr<Crl>> result = Crl::Parse(raw_crl);
  if (!result.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Parsing crl string failed with result ", result.status().ToString()));
  }
  std::shared_ptr<Crl> crl = std::move(*result);
  grpc_slice_unref(crl_slice);
  return crl;
}

void GetAbsoluteFilePath(const char* valid_file_dir,
                         const char* file_entry_name, char* path_buffer) {
  if (valid_file_dir != nullptr && file_entry_name != nullptr) {
    int path_len = snprintf(path_buffer, MAXPATHLEN, "%s/%s", valid_file_dir,
                            file_entry_name);
    if (path_len == 0) {
      gpr_log(GPR_ERROR, "failed to get absolute path for file: %s",
              file_entry_name);
    }
  }
}
}  // namespace

CertificateInfoImpl::CertificateInfoImpl(absl::string_view issuer)
    : issuer_(issuer) {}

absl::string_view CertificateInfoImpl::Issuer() const { return issuer_; }

CrlImpl::CrlImpl(X509_CRL* crl, const std::string& issuer)
    : crl_(crl), issuer_(issuer) {}

// Copy constructor needs to duplicate the X509_CRL* since the destructor frees
// it
CrlImpl::CrlImpl(const CrlImpl& other)
    : crl_(X509_CRL_dup(other.crl())), issuer_(other.issuer_) {}

absl::StatusOr<CrlImpl> CrlImpl::Create(X509_CRL* crl) {
  std::string issuer = IssuerFromCrl(crl);
  if (issuer.empty()) {
    return absl::InvalidArgumentError("Issuer of crl cannot be empty");
  }
  return CrlImpl(crl, issuer);
}

CrlImpl::~CrlImpl() { X509_CRL_free(crl_); }

X509_CRL* CrlImpl::crl() const { return crl_; }

absl::string_view CrlImpl::Issuer() { return issuer_; }

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
  absl::StatusOr<CrlImpl> result = CrlImpl::Create(crl);
  if (!result.ok()) {
    return result.status();
  }
  return std::make_unique<CrlImpl>(std::move(*result));
}

StaticCrlProvider::StaticCrlProvider(
    const absl::flat_hash_map<std::string, std::shared_ptr<Crl>>& crls)
    : crls_(crls) {}

absl::StatusOr<std::shared_ptr<CrlProvider>> StaticCrlProvider::FromVector(
    const std::vector<std::string> crls) {
  absl::flat_hash_map<std::string, std::shared_ptr<Crl>> crl_map;
  for (const auto& raw_crl : crls) {
    absl::StatusOr<std::unique_ptr<Crl>> result = Crl::Parse(raw_crl);
    if (!result.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parsing crl string failed with result ",
                       result.status().ToString()));
    }
    std::unique_ptr<Crl> crl = std::move(*result);
    crl_map[crl->Issuer()] = std::move(crl);
  }
  StaticCrlProvider provider = StaticCrlProvider(crl_map);
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

DirectoryReloaderCrlProviderImpl::~DirectoryReloaderCrlProviderImpl() {
  // TODO(gtcooke94) do we need to worry about a race here?
  if (refresh_handle_.has_value()) {
    event_engine_->Cancel(refresh_handle_.value());
  }
}

absl::StatusOr<std::shared_ptr<CrlProvider>>
DirectoryReloaderCrlProvider::CreateDirectoryReloaderProvider(
    absl::string_view directory, std::chrono::seconds refresh_duration,
    std::function<void(absl::Status)> reload_error_callback) {
  // TODO(gtcooke94) validate directory, inputs, etc
  // TODO(gtcooke94) do first load here or in the thread?
  // TODO(gtcooke94) now that it's an internal impl we can have more //
  // constructors
  auto provider = std::make_shared<DirectoryReloaderCrlProviderImpl>(
      directory, refresh_duration, reload_error_callback);
  gpr_event_init(&provider->shutdown_event_);
  absl::Status initial_status = provider->Update();
  // TODO(gtcooke94) Return for bad initial status?
  provider->ScheduleReload();
  return provider;
}

bool DirectoryReloaderCrlProviderImpl::OnNextUpdateTimer() {
  // absl::Status status = absl::OkStatus();
  gpr_log(GPR_ERROR, "GREG: Before Update");
  absl::Status status = Update();
  gpr_log(GPR_ERROR, "GREG: After Update");
  if (!status.ok()) {
    // TODO(gtcooke94) log here or just in the Update loop per file?
    // if (reload_error_callback_ != nullptr) {
    //   reload_error_callback_(status);
    // }
  }
  ScheduleReload();
  return false;
}

void DirectoryReloaderCrlProviderImpl::ScheduleReload() {
  std::weak_ptr<DirectoryReloaderCrlProviderImpl> self = shared_from_this();
  gpr_log(GPR_ERROR, "GREG: in ScheduleReload");
  refresh_handle_ = event_engine_->RunAfter(refresh_duration_, [self]() {
    gpr_log(GPR_ERROR, "GREG: In RunAfter closure");
    ApplicationCallbackExecCtx callback_exec_ctx;
    ExecCtx exec_ctx;
    {
      if (std::shared_ptr<DirectoryReloaderCrlProviderImpl> valid_ptr =
              self.lock()) {
        valid_ptr->callback_count += 1;
        gpr_log(GPR_ERROR, "GREG: %i;", valid_ptr->callback_count);
        if (valid_ptr->OnNextUpdateTimer()) {
          // TODO(gtcooke94)
        }
      } else {
        gpr_log(GPR_ERROR, "GREG: weak_ptr no longer valid");
      }
    }
  });
}

absl::Status DirectoryReloaderCrlProviderImpl::Update() {
  // for () absl::MutexLock lock(&mu_);
  // TODO(gtcooke94) reading directory in C++ on windows vs. unix
  gpr_log(GPR_ERROR, "GREG: CRL Directory %s", crl_directory_.c_str());
  DIR* crl_directory;
  if ((crl_directory = opendir(crl_directory_.c_str())) == nullptr) {
    // Try getting full absolute path of crl_directory_
  }
  struct FileData {
    char path[MAXPATHLEN];
    off_t size;
  };
  std::vector<FileData> crl_files;
  struct dirent* directory_entry;
  bool all_files_successful = true;
  while ((directory_entry = readdir(crl_directory)) != nullptr) {
    const char* file_name = directory_entry->d_name;
    gpr_log(GPR_ERROR, "GREG: reading file %s", file_name);

    FileData file_data;
    GetAbsoluteFilePath(crl_directory_.c_str(), file_name, file_data.path);
    gpr_log(GPR_ERROR, "GREG: reading file full path %s", file_data.path);
    struct stat dir_entry_stat;
    int stat_return = stat(file_data.path, &dir_entry_stat);
    if (stat_return == -1 || !S_ISREG(dir_entry_stat.st_mode)) {
      // TODO(gtcooke94) More checks here
      // no subdirectories.
      if (stat_return == -1) {
        all_files_successful = false;
        gpr_log(GPR_ERROR, "failed to get status for file: %s", file_data.path);
      }
      continue;
    }
    file_data.size = dir_entry_stat.st_size;
    crl_files.push_back(file_data);
  }
  closedir(crl_directory);
  absl::flat_hash_map<std::string, std::shared_ptr<Crl>> new_crls;
  for (const FileData& file : crl_files) {
    // Build a map of new_crls to update to. If all files successful, do a full
    // swap of the map. Otherwise update in place
    absl::StatusOr<std::shared_ptr<Crl>> result = ReadCrlFromFile(file.path);
    if (!result.ok()) {
      all_files_successful = false;
      reload_error_callback_(absl::InvalidArgumentError(
          absl::StrFormat("CRL Reloader failed to read file: %s", file.path)));
      continue;
    }
    // Now we have a good CRL to update in our map
    std::shared_ptr<Crl> crl = *result;
    new_crls[crl->Issuer()] = std::move(crl);
  }
  if (!all_files_successful) {
    // Need to make sure CRLs we read successfully into new_crls are still
    // in-place updated in crls_
    for (auto& kv : new_crls) {
      std::shared_ptr<Crl> crl = kv.second;
      mu_.Lock();
      crls_[crl->Issuer()] = std::move(crl);
      mu_.Unlock();
    }
    return absl::UnknownError(
        "Not all files in CRL directory read successfully during async "
        "update.");
  } else {
    crls_ = std::move(new_crls);
  }
  return absl::OkStatus();
}

std::shared_ptr<Crl> DirectoryReloaderCrlProviderImpl::GetCrl(
    const CertificateInfo& certificate_info) {
  absl::MutexLock lock(&mu_);
  auto it = crls_.find(certificate_info.Issuer());
  if (it == crls_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace experimental
}  // namespace grpc_core