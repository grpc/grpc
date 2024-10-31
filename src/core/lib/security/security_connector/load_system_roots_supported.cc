//
//
// Copyright 2018 gRPC authors.
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

#include <algorithm>
#include <string>
#include <vector>

#if defined(GPR_LINUX) || defined(GPR_ANDROID) || defined(GPR_FREEBSD) || \
    defined(GPR_APPLE)

#include <dirent.h>
#include <fcntl.h>
#include <grpc/support/alloc.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "absl/log/log.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/security_connector/load_system_roots_supported.h"
#include "src/core/util/load_file.h"
#include "src/core/util/useful.h"

namespace grpc_core {
namespace {

#if defined(GPR_LINUX) || defined(GPR_ANDROID)
const char* kCertFiles[] = {
    "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cacert.pem",
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"};
const char* kCertDirectories[] = {
    "/etc/ssl/certs", "/system/etc/security/cacerts", "/usr/local/share/certs",
    "/etc/pki/tls/certs", "/etc/openssl/certs"};
#elif defined(GPR_FREEBSD)  // endif GPR_LINUX || GPR_ANDROID
const char* kCertFiles[] = {"/etc/ssl/cert.pem",
                            "/usr/local/share/certs/ca-root-nss.crt"};
const char* kCertDirectories[] = {""};
#elif defined(GPR_APPLE)    // endif GPR_FREEBSD
const char* kCertFiles[] = {"/etc/ssl/cert.pem"};
const char* kCertDirectories[] = {""};
#endif                      // GPR_APPLE

grpc_slice GetSystemRootCerts() {
  size_t num_cert_files_ = GPR_ARRAY_SIZE(kCertFiles);
  for (size_t i = 0; i < num_cert_files_; i++) {
    auto slice = LoadFile(kCertFiles[i], /*add_null_terminator=*/true);
    if (slice.ok()) return slice->TakeCSlice();
  }
  return grpc_empty_slice();
}

}  // namespace

void GetAbsoluteFilePath(const char* valid_file_dir,
                         const char* file_entry_name, char* path_buffer) {
  if (valid_file_dir != nullptr && file_entry_name != nullptr) {
    int path_len = snprintf(path_buffer, MAXPATHLEN, "%s/%s", valid_file_dir,
                            file_entry_name);
    if (path_len == 0) {
      LOG(ERROR) << "failed to get absolute path for file: " << file_entry_name;
    }
  }
}

grpc_slice CreateRootCertsBundle(const char* certs_directory) {
  grpc_slice bundle_slice = grpc_empty_slice();
  if (certs_directory == nullptr) {
    return bundle_slice;
  }
  DIR* ca_directory = opendir(certs_directory);
  if (ca_directory == nullptr) {
    return bundle_slice;
  }
  struct FileData {
    char path[MAXPATHLEN];
    off_t size;
  };
  std::vector<FileData> roots_filenames;
  size_t total_bundle_size = 0;
  struct dirent* directory_entry;
  while ((directory_entry = readdir(ca_directory)) != nullptr) {
    struct stat dir_entry_stat;
    const char* file_entry_name = directory_entry->d_name;
    FileData file_data;
    GetAbsoluteFilePath(certs_directory, file_entry_name, file_data.path);
    int stat_return = stat(file_data.path, &dir_entry_stat);
    if (stat_return == -1 || !S_ISREG(dir_entry_stat.st_mode)) {
      // no subdirectories.
      if (stat_return == -1) {
        LOG(ERROR) << "failed to get status for file: " << file_data.path;
      }
      continue;
    }
    file_data.size = dir_entry_stat.st_size;
    total_bundle_size += file_data.size;
    roots_filenames.push_back(file_data);
  }
  closedir(ca_directory);
  char* bundle_string = static_cast<char*>(gpr_zalloc(total_bundle_size + 1));
  size_t bytes_read = 0;
  for (size_t i = 0; i < roots_filenames.size(); i++) {
    int file_descriptor = open(roots_filenames[i].path, O_RDONLY);
    if (file_descriptor != -1) {
      // Read file into bundle.
      size_t cert_file_size = roots_filenames[i].size;
      int read_ret =
          read(file_descriptor, bundle_string + bytes_read, cert_file_size);
      if (read_ret != -1) {
        bytes_read += read_ret;
      } else {
        LOG(ERROR) << "failed to read file: " << roots_filenames[i].path;
      }
    }
  }
  bundle_slice = grpc_slice_new(bundle_string, bytes_read, gpr_free);
  return bundle_slice;
}

grpc_slice LoadSystemRootCerts() {
  grpc_slice result = grpc_empty_slice();
  // Prioritize user-specified custom directory if flag is set.
  auto custom_dir = ConfigVars::Get().SystemSslRootsDir();
  if (!custom_dir.empty()) {
    result = CreateRootCertsBundle(std::string(custom_dir).c_str());
  }
  // If the custom directory is empty/invalid/not specified, fallback to
  // distribution-specific directory.
  if (GRPC_SLICE_IS_EMPTY(result)) {
    result = GetSystemRootCerts();
  }
  if (GRPC_SLICE_IS_EMPTY(result)) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(kCertDirectories); i++) {
      result = CreateRootCertsBundle(kCertDirectories[i]);
      if (!GRPC_SLICE_IS_EMPTY(result)) {
        break;
      }
    }
  }
  return result;
}

}  // namespace grpc_core

#endif  // GPR_LINUX || GPR_ANDROID || GPR_FREEBSD || GPR_APPLE
