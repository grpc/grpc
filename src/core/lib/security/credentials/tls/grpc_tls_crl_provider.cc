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

#include <memory>
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
#include "absl/types/span.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/directory.h"
#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/iomgr/exec_ctx.h"

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
    gpr_log(GPR_ERROR, "Error reading file %s",
            crl_slice.status().ToString().c_str());
    return absl::InvalidArgumentError("Could not load file");
  }
  absl::StatusOr<std::unique_ptr<Crl>> crl =
      Crl::Parse(crl_slice->as_string_view());
  if (!crl.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Parsing crl string failed with result ", crl.status().ToString()));
  }
  return crl;
}

// #if defined(GPR_LINUX) || defined(GPR_ANDROID) || defined(GPR_FREEBSD) || \
//     defined(GPR_APPLE)
// std::string GetAbsoluteFilePath(absl::string_view valid_file_dir,
//                                 absl::string_view file_entry_name) {
//   return absl::StrFormat("%s/%s", valid_file_dir, file_entry_name);
// }

// absl::StatusOr<std::vector<std::string>> GetFilesInDirectory(
//     const std::string& crl_directory_path) {
//   DIR* crl_directory;
//   // Open the dir for reading
//   if ((crl_directory = opendir(crl_directory_path.c_str())) == nullptr) {
//     return absl::InternalError("Could not read crl directory.");
//   }
//   std::vector<std::string> crl_files;
//   struct dirent* directory_entry;
//   // Iterate over everything in the directory
//   while ((directory_entry = readdir(crl_directory)) != nullptr) {
//     const char* file_name = directory_entry->d_name;

//     std::string file_path =
//         GetAbsoluteFilePath(crl_directory_path.c_str(), file_name);
//     struct stat dir_entry_stat;
//     int stat_return = stat(file_path.c_str(), &dir_entry_stat);
//     // S_ISREG(dir_entry_stat.st_mode) returns true if this entry is a
//     regular
//     // file
//     //
//     https://stackoverflow.com/questions/40163270/what-is-s-isreg-and-what-does-it-do
//     // This lets us skip over either bad files or things that aren't files to
//     // read. For example, this will properly skip over `..` and `.` which
//     show
//     // up during this iteration, as well as symlinks and sub directories.
//     if (stat_return == -1 || !S_ISREG(dir_entry_stat.st_mode)) {
//       if (stat_return == -1) {
//         gpr_log(GPR_ERROR, "failed to get status for file: %s",
//                 file_path.c_str());
//       }
//       // If stat_return != -1, this just isn't a file so we continue
//       continue;
//     }
//     crl_files.push_back(file_path);
//   }
//   closedir(crl_directory);
//   return crl_files;
// }
#if defined(GPR_WINDOWS)

// TODO(gtcooke94) How to best test this?
#include <windows.h>

std::string GetAbsoluteFilePath(absl::string_view valid_file_dir,
                                absl::string_view file_entry_name) {
  return absl::StrFormat("%s\\t%s", valid_file_dir, file_entry_name);
}

// Reference for reading directory in Windows:
// https://stackoverflow.com/questions/612097/how-can-i-get-the-list-of-files-in-a-directory-using-c-or-c
// https://learn.microsoft.com/en-us/windows/win32/fileio/listing-the-files-in-a-directory
absl::StatusOr<std::vector<std::string>> GetFilesInDirectory(
    const std::string& crl_directory_path) {
  std::string search_path = crl_directory_path + "/*.*";
  std::vector<std::string> crl_files;
  windows::WIN32_FIND_DATA find_data;
  HANDLE hFind = ::FindFirstFile(search_path.c_str(), &find_data);
  if (hFind != windows::INVALID_HANDLE_VALUE) {
    do {
      if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        std::string file_path;
        GetAbsoluteFilePath(crl_directory_path.c_str(), find_data.cFileName,
                            file_path);
        crl_files.push_back(file_path);
      }
    } while (::FindNextFile(hFind, &find_data));
    ::FindClose(hFind);
    return crl_files;
  } else {
    return absl::InternalError("Could not read crl directory.");
  }
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

DirectoryReloaderCrlProvider::~DirectoryReloaderCrlProvider() {
  if (refresh_handle_.has_value()) {
    event_engine_->Cancel(refresh_handle_.value());
  }
}

absl::StatusOr<std::shared_ptr<CrlProvider>> CreateDirectoryReloaderCrlProvider(
    absl::string_view directory, std::chrono::seconds refresh_duration,
    std::function<void(absl::Status)> reload_error_callback,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine) {
  if (refresh_duration < std::chrono::seconds(60)) {
    return absl::InvalidArgumentError("Refresh duration minimum is 60 seconds");
  }
  struct stat dir_stat;
  if (stat(directory.data(), &dir_stat) != 0) {
    return absl::InvalidArgumentError("The directory path is not valid.");
  }
  if (event_engine == nullptr) {
    event_engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  }

  auto provider = std::make_shared<DirectoryReloaderCrlProvider>(
      directory, refresh_duration, reload_error_callback, event_engine);
  // This could be slow to do at startup, but we want to
  // make sure it's done before the provider is used.
  absl::Status initial_status = provider->Update();
  if (!initial_status.ok()) {
    return initial_status;
  }
  provider->ScheduleReload();
  return provider;
}

void DirectoryReloaderCrlProvider::OnNextUpdateTimer() {
  absl::Status status = Update();
  if (!status.ok() && reload_error_callback_ != nullptr) {
    reload_error_callback_(status);
  }
  ScheduleReload();
}

void DirectoryReloaderCrlProvider::ScheduleReload() {
  std::weak_ptr<DirectoryReloaderCrlProvider> self = shared_from_this();
  refresh_handle_ =
      event_engine_->RunAfter(refresh_duration_, [self = std::move(self)]() {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        {
          if (std::shared_ptr<DirectoryReloaderCrlProvider> valid_ptr =
                  self.lock()) {
            valid_ptr->OnNextUpdateTimer();
          }
        }
      });
}

absl::Status DirectoryReloaderCrlProvider::Update() {
  auto crl_files = GetFilesInDirectory(crl_directory_);
  if (!crl_files.ok()) {
    return crl_files.status();
  }
  bool all_files_successful = true;
  absl::flat_hash_map<std::string, std::shared_ptr<Crl>> new_crls;
  for (const std::string& file_path : *crl_files) {
    // Build a map of new_crls to update to. If all files successful, do a
    // full swap of the map. Otherwise update in place.
    absl::StatusOr<std::shared_ptr<Crl>> crl = ReadCrlFromFile(file_path);
    if (!crl.ok()) {
      all_files_successful = false;
      if (reload_error_callback_ != nullptr) {
        reload_error_callback_(absl::InvalidArgumentError(absl::StrFormat(
            "CRL Reloader failed to read file: %s", file_path)));
      }
      continue;
    }
    // Now we have a good CRL to update in our map.
    // It's not safe to say crl->Issuer() on the LHS and std::move(crl) on the
    // RHS, because C++ does not guarantee which of those will be executed
    // first.
    std::string issuer((*crl)->Issuer());
    new_crls[issuer] = std::move(*crl);
  }
  grpc_core::MutexLock lock(&mu_);
  if (!all_files_successful) {
    // Need to make sure CRLs we read successfully into new_crls are still
    // in-place updated in crls_.
    for (auto& kv : new_crls) {
      std::shared_ptr<Crl>& crl = kv.second;
      // It's not safe to say crl->Issuer() on the LHS and std::move(crl) on the
      // RHS, because C++ does not guarantee which of those will be executed
      // first.
      std::string issuer(crl->Issuer());
      crls_[issuer] = std::move(crl);
    }
    return absl::UnknownError(
        "Not all files in CRL directory read successfully during async "
        "update.");
  } else {
    crls_ = std::move(new_crls);
  }
  return absl::OkStatus();
}

std::shared_ptr<Crl> DirectoryReloaderCrlProvider::GetCrl(
    const CertificateInfo& certificate_info) {
  grpc_core::MutexLock lock(&mu_);
  auto it = crls_.find(certificate_info.Issuer());
  if (it == crls_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace experimental
}  // namespace grpc_core
