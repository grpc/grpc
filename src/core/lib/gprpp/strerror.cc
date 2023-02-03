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

#include "src/core/lib/gprpp/strerror.h"

#include <string.h>

#include <initializer_list>

#include "absl/strings/str_format.h"

namespace grpc_core {

#ifdef GPR_WINDOWS
std::string StrError(int err) { return strerror(err); }
#else
std::string StrError(int err) {
  struct Finish {
    static std::string Run(char* buf, int err, int r) {
      if (r == 0) return buf;
      return absl::StrFormat("strerror_r(%d) failed: %d", err, r);
    }
    static std::string Run(char*, int, const char* r) { return r; }
  };
  char buf[256];
  return Finish::Run(buf, err, strerror_r(err, buf, sizeof(buf)));
}
#endif  // !GPR_WINDOWS

}  // namespace grpc_core
