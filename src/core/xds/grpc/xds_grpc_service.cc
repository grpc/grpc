//
// Copyright 2018 gRPC authors.
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

#include "src/core/xds/grpc/xds_grpc_service.h"

#include <string>

#include "src/core/util/string.h"
#include "src/core/util/time.h"

namespace grpc_core {

std::string XdsGrpcService::ToString() const {
  std::string result = "{";
  bool is_first = true;
  if (server_target != nullptr) {
    StrAppend(result, "server_target=");
    StrAppend(result, server_target->Key());
    is_first = false;
  }
  if (timeout != Duration::Zero()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "timeout=");
    StrAppend(result, timeout.ToString());
    is_first = false;
  }
  if (!initial_metadata.empty()) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, "initial_metadata=[");
    bool is_first_metadata = true;
    for (const auto& [key, value] : initial_metadata) {
      if (!is_first_metadata) StrAppend(result, ", ");
      StrAppend(result, "{key=");
      StrAppend(result, key);
      StrAppend(result, ", value=");
      StrAppend(result, value);
      StrAppend(result, "}");
      is_first_metadata = false;
    }
    StrAppend(result, "]");
  }
  StrAppend(result, "}");
  return result;
}

}  // namespace grpc_core
