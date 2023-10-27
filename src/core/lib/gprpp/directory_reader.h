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

#ifndef GRPC_SRC_CORE_LIB_GPRPP_DIRECTORY_H
#define GRPC_SRC_CORE_LIB_GPRPP_DIRECTORY_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class DirectoryReader {
 public:
  virtual ~DirectoryReader() = default;
  virtual absl::StatusOr<std::vector<std::string>> GetFilesInDirectory() = 0;
};

std::unique_ptr<DirectoryReader> MakeDirectoryReader(
    absl::string_view filename);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_DIRECTORY_H