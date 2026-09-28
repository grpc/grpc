//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_CREATE_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_CREATE_H

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class Transport;

// Creates a client channel.
absl::StatusOr<RefCountedPtr<Channel>> ChannelCreate(
    std::string target, ChannelArgs args,
    grpc_channel_stack_type channel_stack_type, Transport* optional_transport);

absl::StatusOr<grpc_channel*> CreateClientEndpointChannel(
    const char* target, grpc_channel_credentials* creds,
    const ChannelArgs& args);

// Creates a lame channel. Calls on it fail with status, or UNKNOWN if status
// is OK.
RefCountedPtr<Channel> MakeLameChannel(absl::string_view target,
                                       absl::Status status);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_CREATE_H
