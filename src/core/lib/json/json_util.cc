//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/lib/json/json_util.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"

namespace grpc_core {

bool ParseDurationFromJson(const Json& field, grpc_millis* duration) {
  if (field.type() != Json::Type::STRING) return false;
  size_t len = field.string_value().size();
  if (field.string_value()[len - 1] != 's') return false;
  grpc_core::UniquePtr<char> buf(gpr_strdup(field.string_value().c_str()));
  *(buf.get() + len - 1) = '\0';  // Remove trailing 's'.
  char* decimal_point = strchr(buf.get(), '.');
  int nanos = 0;
  if (decimal_point != nullptr) {
    *decimal_point = '\0';
    nanos = gpr_parse_nonnegative_int(decimal_point + 1);
    if (nanos == -1) {
      return false;
    }
    int num_digits = static_cast<int>(strlen(decimal_point + 1));
    if (num_digits > 9) {  // We don't accept greater precision than nanos.
      return false;
    }
    for (int i = 0; i < (9 - num_digits); ++i) {
      nanos *= 10;
    }
  }
  int seconds =
      decimal_point == buf.get() ? 0 : gpr_parse_nonnegative_int(buf.get());
  if (seconds == -1) return false;
  *duration = seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
  return true;
}

}  // namespace grpc_core
