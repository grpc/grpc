//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_UTIL_FILE_UTIL_H
#define GRPC_CORE_LIB_SECURITY_UTIL_FILE_UTIL_H

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/string_view.h"

namespace grpc_core {

class TmpFile {
 public:
  // Create a temporary file with |data| written in.
  explicit TmpFile(absl::string_view data);

  ~TmpFile();

  const std::string& name() { return name_; }

  // Rewrite |data| to the temporary file, in an atomic way.
  void RewriteFile(absl::string_view data);

 private:
  std::string CreateTmpFileAndWriteData(absl::string_view data);

  std::string name_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_UTIL_FILE_UTIL_H
