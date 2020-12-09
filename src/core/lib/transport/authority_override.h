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

#ifndef GRPC_CORE_LIB_TRANSPORT_AUTHORITY_OVERRIDE_H
#define GRPC_CORE_LIB_TRANSPORT_AUTHORITY_OVERRIDE_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

#include <grpc/grpc.h>

namespace grpc_core {

/// Returns a channel argument containing \a authority.
grpc_arg CreateAuthorityOverrideChannelArg(const char* authority);

/// Returns the authority override from \a args or the empty string. The return
/// value is a string_view into the `args` data structure.
absl::string_view FindAuthorityOverrideInArgs(const grpc_channel_args* args);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_TRANSPORT_AUTHORITY_OVERRIDE_H */
