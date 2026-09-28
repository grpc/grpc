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

#ifndef GRPC_CHANNEL_FACTORY_H
#define GRPC_CHANNEL_FACTORY_H

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class Channel;

namespace experimental {

// EXPERIMENTAL API - Subject to change
//
// Creates channels for LB policies (gRFC A119). To use it, set a
// std::shared_ptr<ChannelFactory> in the channel args with the key
// GRPC_ARG_CHANNEL_FACTORY.
//
// Implementations must be thread-safe.
class ChannelFactory {
 public:
  virtual ~ChannelFactory() = default;

  // Returns the channel for key. Must not return null. If key is not valid,
  // return CreateLameChannel(). Must not block.
  virtual std::shared_ptr<Channel> CreateChannel(absl::string_view key) = 0;

 protected:
  // Creates a client channel. Returns a lame channel on failure.
  static std::shared_ptr<Channel> CreateCoreChannel(
      absl::string_view target, grpc_channel_credentials* creds,
      const grpc_channel_args* args);

  // Creates a lame channel. RPCs on it fail with status.
  static std::shared_ptr<Channel> CreateLameChannel(absl::string_view target,
                                                    absl::Status status);
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_CHANNEL_FACTORY_H
