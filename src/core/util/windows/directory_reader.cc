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

#if defined(GPR_WINDOWS)

#include <sys/stat.h>
#include <windows.h>

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
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

// Reference for reading directory in Windows:
// https://stackoverflow.com/questions/612097/how-can-i-get-the-list-of-files-in-a-directory-using-c-or-c
// https://learn.microsoft.com/en-us/windows/win32/fileio/listing-the-files-in-a-directory
absl::Status DirectoryReaderImpl::ForEach(
    absl::FunctionRef<void(absl::string_view)> callback) {
  std::string search_path = absl::StrCat(directory_path_, "/*");
  WIN32_FIND_DATAA find_data;
  HANDLE hFind = ::FindFirstFileA(search_path.c_str(), &find_data);
  if (hFind == INVALID_HANDLE_VALUE) {
    return absl::InternalError("Could not read crl directory.");
  }
  do {
    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      callback(find_data.cFileName);
    }
  } while (::FindNextFileA(hFind, &find_data));
  ::FindClose(hFind);
  return absl::OkStatus();
}

}  // namespace grpc_core

#endif  // GPR_WINDOWS
