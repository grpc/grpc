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

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include "src/core/lib/channel/channel_args.h"

// Channel arg key for the authority override.
#define GRPC_ARG_AUTHORITY_OVERRIDE "grpc.authority_override"

namespace grpc_core {

grpc_arg CreateAuthorityOverrideChannelArg(const char* authority) {
  return grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_AUTHORITY_OVERRIDE),
      const_cast<char*>(authority));
}

absl::string_view FindAuthorityOverrideInArgs(const grpc_channel_args* args) {
  const char* found =
      grpc_channel_args_find_string(args, GRPC_ARG_AUTHORITY_OVERRIDE);
  return found == nullptr ? "" : found;
}

}  // namespace grpc_core
