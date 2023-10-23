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

#include <memory>
#include <utility>

// IWYU pragma: no_include <openssl/mem.h>
#include <dirent.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <openssl/bio.h>
#include <openssl/crypto.h>  // IWYU pragma: keep
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"

namespace grpc_core {
namespace experimental {

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

absl::StatusOr<std::shared_ptr<Crl>> ReadCrlFromFile(
    absl::string_view crl_path) {
  grpc_slice crl_slice = grpc_empty_slice();
  grpc_error_handle err = grpc_load_file(crl_path.data(), 1, &crl_slice);
  if (!err.ok()) {
    // TODO(gtcooke94) log error differently?
    gpr_log(GPR_ERROR, "Error reading file %s", err.message().data());
    grpc_slice_unref(crl_slice);
    return absl::InvalidArgumentError("Could not load file");
  }
  std::string raw_crl = std::string(StringViewFromSlice(crl_slice));
  absl::StatusOr<std::unique_ptr<Crl>> crl = Crl::Parse(raw_crl);
  if (!crl.ok()) {
    grpc_slice_unref(crl_slice);
    return absl::InvalidArgumentError(absl::StrCat(
        "Parsing crl string failed with result ", crl.status().ToString()));
  }
  grpc_slice_unref(crl_slice);
  return crl;
}

struct FileData {
  char path[MAXPATHLEN];
  off_t size;
};

#if defined(GPR_LINUX) || defined(GPR_ANDROID) || defined(GPR_FREEBSD) || \
    defined(GPR_APPLE)
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

std::vector<FileData> GetFilesInDirectory(
    const std::string& crl_directory_path) {
  DIR* crl_directory;
  if ((crl_directory = opendir(crl_directory_path.c_str())) == nullptr) {
    // Try getting full absolute path of crl_directory_path
  }
  std::vector<FileData> crl_files;
  struct dirent* directory_entry;
  // bool all_files_successful = true;
  while ((directory_entry = readdir(crl_directory)) != nullptr) {
    const char* file_name = directory_entry->d_name;

    FileData file_data;
    GetAbsoluteFilePath(crl_directory_path.c_str(), file_name, file_data.path);
    struct stat dir_entry_stat;
    int stat_return = stat(file_data.path, &dir_entry_stat);
    if (stat_return == -1 || !S_ISREG(dir_entry_stat.st_mode)) {
      if (stat_return == -1) {
        gpr_log(GPR_ERROR, "failed to get status for file: %s", file_data.path);
      }
      continue;
    }
    file_data.size = dir_entry_stat.st_size;
    crl_files.push_back(file_data);
  }
  closedir(crl_directory);
  return crl_files;
}
#elif defined(GPR_WINDOWS)

// TODO(gtcooke94) How to best test this?
#include <windows.h>

void GetAbsoluteFilePath(const char* valid_file_dir,
                         const char* file_entry_name, char* path_buffer) {
  if (valid_file_dir != nullptr && file_entry_name != nullptr) {
    int path_len = snprintf(path_buffer, MAXPATHLEN, "%s\%s", valid_file_dir,
                            file_entry_name);
    if (path_len == 0) {
      gpr_log(GPR_ERROR, "failed to get absolute path for file: %s",
              file_entry_name);
    }
  }
}

// Reference for reading directory in Windows:
// https://stackoverflow.com/questions/612097/how-can-i-get-the-list-of-files-in-a-directory-using-c-or-c
// https://learn.microsoft.com/en-us/windows/win32/fileio/listing-the-files-in-a-directory
std::vector<FileData> GetFilesInDirectory(
    const std::string& crl_directory_path) {
  std::string search_path = crl_directory_path + "/*.*";
  std::vector<FileData> crl_files;
  windows::WIN32_FIND_DATA find_data;
  HANDLE hFind = ::FindFirstFile(search_path.c_str(), &find_data);
  if (hFind != windows::INVALID_HANDLE_VALUE) {
    do {
      if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        FileData file_data;
        GetAbsoluteFilePath(crl_directory_path.c_str(), find_data.cFileName,
                            file_data.path);
        crl_files.push_back(file_data);
      }
    } while (::FindNextFile(hFind, &find_data));
    ::FindClose(hFind);
  }
  return crl_files;

  vector<string> names;
  string search_path = folder + "/*.*";
  WIN32_FIND_DATA fd;
  HANDLE hFind = ::FindFirstFile(search_path.c_str(), &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      // read all (real) files in current folder
      // , delete '!' read other 2 default folder . and ..
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        names.push_back(fd.cFileName);
      }
    } while (::FindNextFile(hFind, &fd));
    ::FindClose(hFind);
  }
  return names;

  DIR* crl_directory;
  if ((crl_directory = opendir(crl_directory_.c_str())) == nullptr) {
    // Try getting full absolute path of crl_directory_
  }
  std::vector<FileData> crl_files;
  struct dirent* directory_entry;
  // bool all_files_successful = true;
  while ((directory_entry = readdir(crl_directory)) != nullptr) {
    const char* file_name = directory_entry->d_name;

    FileData file_data;
    GetAbsoluteFilePath(crl_directory_.c_str(), file_name, file_data.path);
    struct stat dir_entry_stat;
    int stat_return = stat(file_data.path, &dir_entry_stat);
    if (stat_return == -1 || !S_ISREG(dir_entry_stat.st_mode)) {
      // TODO(gtcooke94) More checks here
      // no subdirectories.
      if (stat_return == -1) {
        // TODO(gtcooke94) does this constitute a failure to read? What cases
        // will this fail on?
        // all_files_successful = false;
        gpr_log(GPR_ERROR, "failed to get status for file: %s", file_data.path);
      }
      continue;
    }
    file_data.size = dir_entry_stat.st_size;
    crl_files.push_back(file_data);
  }
  closedir(crl_directory);
  return crl_files;
}

#endif  // GPR_LINUX || GPR_ANDROID || GPR_FREEBSD || GPR_APPLE

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

DirectoryReloaderCrlProviderImpl::~DirectoryReloaderCrlProviderImpl() {
  // TODO(gtcooke94) do we need to worry about a race here?
  if (refresh_handle_.has_value()) {
    event_engine_->Cancel(refresh_handle_.value());
  }
}

absl::StatusOr<std::shared_ptr<CrlProvider>> CreateDirectoryReloaderProvider(
    absl::string_view directory, std::chrono::seconds refresh_duration,
    std::function<void(absl::Status)> reload_error_callback) {
  if (refresh_duration < std::chrono::seconds(60)) {
    return absl::InvalidArgumentError("Refresh duration minimum is 60 seconds");
  }
  struct stat dir_stat;
  if (stat(directory.data(), &dir_stat) != 0) {
    return absl::InvalidArgumentError("The directory path is not valid.");
  }
  auto provider = std::make_shared<DirectoryReloaderCrlProviderImpl>(
      directory, refresh_duration, reload_error_callback);
  absl::Status initial_status = provider->Update();
  if (!initial_status.ok()) {
    return initial_status;
  }
  provider->ScheduleReload();
  return provider;
}

void DirectoryReloaderCrlProviderImpl::OnNextUpdateTimer() {
  absl::Status status = Update();
  if (!status.ok()) {
    if (reload_error_callback_ != nullptr) {
      reload_error_callback_(status);
    }
  }
  ScheduleReload();
}

void DirectoryReloaderCrlProviderImpl::ScheduleReload() {
  std::weak_ptr<DirectoryReloaderCrlProviderImpl> self = shared_from_this();
  refresh_handle_ = event_engine_->RunAfter(refresh_duration_, [self]() {
    ApplicationCallbackExecCtx callback_exec_ctx;
    ExecCtx exec_ctx;
    {
      if (std::shared_ptr<DirectoryReloaderCrlProviderImpl> valid_ptr =
              self.lock()) {
        valid_ptr->OnNextUpdateTimer();
      }
    }
  });
}

absl::Status DirectoryReloaderCrlProviderImpl::Update() {
  std::vector<FileData> crl_files = GetFilesInDirectory(crl_directory_);
  bool all_files_successful = true;

  absl::flat_hash_map<std::string, std::shared_ptr<Crl>> new_crls;
  for (const FileData& file : crl_files) {
    // Build a map of new_crls to update to. If all files successful, do a
    // full swap of the map. Otherwise update in place
    absl::StatusOr<std::shared_ptr<Crl>> result = ReadCrlFromFile(file.path);
    if (!result.ok()) {
      all_files_successful = false;
      if (reload_error_callback_ != nullptr) {
        reload_error_callback_(absl::InvalidArgumentError(absl::StrFormat(
            "CRL Reloader failed to read file: %s", file.path)));
      }
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
