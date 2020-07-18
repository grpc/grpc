//
// Copyright 2015 gRPC authors.
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

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "src/core/ext/filters/http/client/util.h"

namespace grpc_core {
std::string GenerateUserAgentFromArgs(const grpc_channel_args* args,
                                      const char* transport_name) {
  std::vector<std::string> user_agent_fields;
  for (size_t i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_PRIMARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_PRIMARY_USER_AGENT_STRING);
      } else {
        user_agent_fields.push_back(args->args[i].value.string);
      }
    }
  }
  user_agent_fields.push_back(
      absl::StrFormat("grpc-c/%s (%s; %s)", grpc_version_string(),
                      GPR_PLATFORM_STRING, transport_name));
  for (size_t i = 0; args && i < args->num_args; i++) {
    if (0 == strcmp(args->args[i].key, GRPC_ARG_SECONDARY_USER_AGENT_STRING)) {
      if (args->args[i].type != GRPC_ARG_STRING) {
        gpr_log(GPR_ERROR, "Channel argument '%s' should be a string",
                GRPC_ARG_SECONDARY_USER_AGENT_STRING);
      } else {
        user_agent_fields.push_back(args->args[i].value.string);
      }
    }
  }
  return absl::StrJoin(user_agent_fields, " ");
}
}  // namespace grpc_core
