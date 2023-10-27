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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#if defined(GPR_LINUX) || defined(GPR_ANDROID) || defined(GPR_FREEBSD) || \
    defined(GPR_APPLE)

#include <dirent.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/directory_reader.h"

namespace grpc_core {

namespace {
std::string GetAbsoluteFilePath(absl::string_view valid_file_dir,
                                absl::string_view file_entry_name) {
  return absl::StrFormat("%s/%s", valid_file_dir, file_entry_name);
}
}  // namespace

class DirectoryReaderImpl : public DirectoryReader {
 public:
  explicit DirectoryReaderImpl(absl::string_view directory_path)
      : directory_path_(directory_path) {}
  absl::StatusOr<std::vector<std::string>> GetDirectoryContents() override;
  absl::string_view Name() override { return directory_path_; }

 private:
  std::string directory_path_;
};

std::unique_ptr<DirectoryReader> MakeDirectoryReader(
    absl::string_view filename) {
  return std::make_unique<DirectoryReaderImpl>(filename);
}

absl::StatusOr<std::vector<std::string>>
DirectoryReaderImpl::GetDirectoryContents() {
  // Open the dir for reading
  DIR* directory = opendir(directory_path_.c_str());
  if (directory == nullptr) {
    return absl::InternalError("Could not read crl directory.");
  }
  std::vector<std::string> contents;
  struct dirent* directory_entry;
  // Iterate over everything in the directory
  while ((directory_entry = readdir(directory)) != nullptr) {
    const char* file_name = directory_entry->d_name;
    // Skip "." and ".."
    if ((strcmp(file_name, kSkipEntriesSelf) == 0 ||
         strcmp(file_name, kSkipEntriesParent) == 0)) {
      continue;
    }

    // std::string file_path =
    //     GetAbsoluteFilePath(directory_path_.c_str(), file_name);
    contents.push_back(file_name);
  }
  closedir(directory);
  return contents;
}
}  // namespace grpc_core

#endif  // GPR_LINUX || GPR_ANDROID || GPR_FREEBSD || GPR_APPLE
