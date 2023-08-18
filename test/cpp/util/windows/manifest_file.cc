// Copyright 2023 The gRPC Authors.
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

#include "test/cpp/util/windows/manifest_file.h"

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <fstream>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"

namespace grpc {
namespace testing {

std::string NormalizeFilePath(const std::string& filepath) {
  return absl::StrReplaceAll(filepath, {{"/", "\\"}});
}

ManifestFile::ManifestFile(const std::string& filepath)
    : filestream_(filepath, std::ios::in | std::ios::binary) {
  if (!filestream_.is_open()) {
    grpc_core::Crash(absl::StrFormat("Failed to open %s", filepath));
  }
}

std::string ManifestFile::Get(const std::string& key) {
  auto iter = cache_.find(key);
  if (iter != cache_.end()) {
    return iter->second;
  }
  do {
    std::string line;
    std::getline(filestream_, line);
    if (!line.empty()) {
      std::vector<std::string> kv = absl::StrSplit(line, " ");
      GPR_ASSERT(kv.size() == 2);
      cache_.emplace(kv[0], kv[1]);
      if (kv[0] == key) {
        return kv[1];
      }
    }
  } while (!filestream_.eof() && !filestream_.fail());
  grpc_core::Crash(
      absl::StrFormat("Failed to find key: %s in MANIFEST file", key));
}

}  // namespace testing
}  // namespace grpc

#endif  // GPR_WINDOWS
