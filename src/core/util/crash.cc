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

#include "src/core/util/crash.h"

#include <grpc/support/port_platform.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

void Crash(absl::string_view message, SourceLocation location) {
  LOG(ERROR).AtLocation(location.file(), location.line()) << message;
  abort();
}

void CrashWithStdio(absl::string_view message, SourceLocation location) {
  fputs(absl::StrCat(location.file(), ":", location.line(), ": ", message, "\n")
            .c_str(),
        stderr);
  abort();
}

}  // namespace grpc_core
