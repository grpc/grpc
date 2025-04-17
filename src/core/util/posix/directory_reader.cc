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

#include <memory>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#if defined(GPR_LINUX) || defined(GPR_ANDROID) || defined(GPR_FREEBSD) || \
    defined(GPR_APPLE) || defined(GPR_NETBSD)

#include <dirent.h>

#include <string>

#include "src/core/util/directory_reader.h"

namespace grpc_core {

namespace {
const char kSkipEntriesSelf[] = ".";
const char kSkipEntriesParent[] = "..";
}  // namespace

class DirectoryReaderImpl : public DirectoryReader {
 public:
  explicit DirectoryReaderImpl(absl::string_view directory_path)
      : directory_path_(directory_path) {}
  absl::string_view Name() const override { return directory_path_; }
  absl::Status ForEach(absl::FunctionRef<void(absl::string_view)>) override;

 private:
  const std::string directory_path_;
};

std::unique_ptr<DirectoryReader> MakeDirectoryReader(
    absl::string_view filename) {
  return std::make_unique<DirectoryReaderImpl>(filename);
}

absl::Status DirectoryReaderImpl::ForEach(
    absl::FunctionRef<void(absl::string_view)> callback) {
  // Open the dir for reading
  DIR* directory = opendir(directory_path_.c_str());
  if (directory == nullptr) {
    return absl::InternalError("Could not read crl directory.");
  }
  struct dirent* directory_entry;
  // Iterate over everything in the directory
  while ((directory_entry = readdir(directory)) != nullptr) {
    const absl::string_view file_name = directory_entry->d_name;
    // Skip "." and ".."
    if (file_name == kSkipEntriesParent || file_name == kSkipEntriesSelf) {
      continue;
    }
    // Call the callback with this filename
    callback(file_name);
  }
  closedir(directory);
  return absl::OkStatus();
}
}  // namespace grpc_core

#endif  // GPR_LINUX || GPR_ANDROID || GPR_FREEBSD || GPR_APPLE
