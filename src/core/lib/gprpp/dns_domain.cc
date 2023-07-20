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

#include <grpc/support/port_platform.h>

#include "absl/strings/ascii.h"

#include "src/core/lib/gprpp/dns_domain.h"

namespace grpc_core {

bool IsValidDnsDomain(absl::string_view domain) {
  bool label_start = true;
  for (unsigned char c : domain) {
    if (label_start) {
      if (!absl::ascii_isalpha(c)) return false;
      label_start = false;
    } else if (c == '.') {
      label_start = true;
    } else {
      if (!absl::ascii_isalnum(c)) return false;
    }
  }
  return true;
}

}  // namespace grpc_core
