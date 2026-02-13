//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_VIRTUAL_CHANNEL_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_VIRTUAL_CHANNEL_H

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// A channel that is backed by a stream on another channel.
class VirtualChannel : public Channel {
 public:
  static absl::StatusOr<RefCountedPtr<Channel>> Create(grpc_call* call,
                                                       ChannelArgs args);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_VIRTUAL_CHANNEL_H
