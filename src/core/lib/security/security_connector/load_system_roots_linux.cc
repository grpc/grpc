/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <grpc/slice_buffer.h>
#include "src/core/lib/security/security_connector/load_system_roots_linux.h"

#ifdef GPR_LINUX

#include "src/core/lib/security/security_connector/load_system_roots.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/iomgr/load_file.h"

namespace grpc_core {
namespace {

const char* kLinuxCertFiles[] = {
    "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem", "/etc/pki/tls/cacert.pem",
    "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"};
const char* kLinuxCertDirectories[] = {
    "/etc/ssl/certs", "/system/etc/security/cacerts", "/usr/local/share/certs",
    "/etc/pki/tls/certs", "/etc/openssl/certs"};

grpc_slice GetSystemRootCerts() {
  grpc_slice valid_bundle_slice = grpc_empty_slice();
  size_t num_cert_files_ = GPR_ARRAY_SIZE(kLinuxCertFiles);
  for (size_t i = 0; i < num_cert_files_; i++) {
    grpc_error* error =
        grpc_load_file(kLinuxCertFiles[i], 1, &valid_bundle_slice);
    if (error == GRPC_ERROR_NONE) {
      return valid_bundle_slice;
    }
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
      gpr_log(GPR_ERROR, "failed to get absolute path for file: %s",
              file_entry_name);
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
  InlinedVector<FileData, 2> roots_filenames;
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
        gpr_log(GPR_ERROR, "failed to get status for file: %s", file_data.path);
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
        gpr_log(GPR_ERROR, "failed to read file: %s", roots_filenames[i].path);
      }
    }
  }
  bundle_slice = grpc_slice_new(bundle_string, bytes_read, gpr_free);
  return bundle_slice;
}

grpc_slice LoadSystemRootCerts() {
  grpc_slice result = grpc_empty_slice();
  // Prioritize user-specified custom directory if flag is set.
  char* custom_dir = gpr_getenv("GRPC_SYSTEM_SSL_ROOTS_DIR");
  if (custom_dir != nullptr) {
    result = CreateRootCertsBundle(custom_dir);
    gpr_free(custom_dir);
  }
  // If the custom directory is empty/invalid/not specified, fallback to
  // distribution-specific directory.
  if (GRPC_SLICE_IS_EMPTY(result)) {
    result = GetSystemRootCerts();
  }
  if (GRPC_SLICE_IS_EMPTY(result)) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(kLinuxCertDirectories); i++) {
      result = CreateRootCertsBundle(kLinuxCertDirectories[i]);
      if (!GRPC_SLICE_IS_EMPTY(result)) {
        break;
      }
    }
  }
  return result;
}

}  // namespace grpc_core

#endif /* GPR_LINUX */
