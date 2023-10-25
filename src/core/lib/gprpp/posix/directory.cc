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

#if defined(GPR_LINUX) || defined(GPR_ANDROID) || defined(GPR_FREEBSD) || \
    defined(GPR_APPLE)

#include <dirent.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/support/log.h>

namespace grpc_core {

std::string GetAbsoluteFilePath(absl::string_view valid_file_dir,
                                absl::string_view file_entry_name) {
  return absl::StrFormat("%s/%s", valid_file_dir, file_entry_name);
}

absl::StatusOr<std::vector<std::string>> GetFilesInDirectory(
    const std::string& crl_directory_path) {
  DIR* crl_directory;
  // Open the dir for reading
  if ((crl_directory = opendir(crl_directory_path.c_str())) == nullptr) {
    return absl::InternalError("Could not read crl directory.");
  }
  std::vector<std::string> crl_files;
  struct dirent* directory_entry;
  // Iterate over everything in the directory
  while ((directory_entry = readdir(crl_directory)) != nullptr) {
    const char* file_name = directory_entry->d_name;

    std::string file_path =
        GetAbsoluteFilePath(crl_directory_path.c_str(), file_name);
    struct stat dir_entry_stat;
    int stat_return = stat(file_path.c_str(), &dir_entry_stat);
    // S_ISREG(dir_entry_stat.st_mode) returns true if this entry is a regular
    // file
    // https://stackoverflow.com/questions/40163270/what-is-s-isreg-and-what-does-it-do
    // This lets us skip over either bad files or things that aren't files to
    // read. For example, this will properly skip over `..` and `.` which show
    // up during this iteration, as well as symlinks and sub directories.
    if (stat_return == -1 || !S_ISREG(dir_entry_stat.st_mode)) {
      if (stat_return == -1) {
        gpr_log(GPR_ERROR, "failed to get status for file: %s",
                file_path.c_str());
      }
      // If stat_return != -1, this just isn't a file so we continue
      continue;
    }
    crl_files.push_back(file_path);
  }
  closedir(crl_directory);
  return crl_files;
}
}  // namespace grpc_core

#endif