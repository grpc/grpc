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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/crash.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

namespace grpc_core {

void Crash(absl::string_view message, SourceLocation location) {
  gpr_log(location.file(), location.line(), GPR_LOG_SEVERITY_ERROR, "%s",
          std::string(message).c_str());
  abort();
}

void CrashWithStdio(absl::string_view message, SourceLocation location) {
  fputs(absl::StrCat(location.file(), ":", location.line(), ": ", message, "\n")
            .c_str(),
        stderr);
  abort();
}

}  // namespace grpc_core
