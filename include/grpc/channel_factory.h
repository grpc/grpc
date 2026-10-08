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

#ifndef GRPC_CHANNEL_FACTORY_H
#define GRPC_CHANNEL_FACTORY_H

#include <grpc/credentials.h>
#include <grpc/impl/grpc_types.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

// Channel argument key for a pointer to a
// std::shared_ptr<grpc_core::experimental::ChannelFactory>, stored with
// ChannelFactory::ChannelArgVtable().
#define GRPC_ARG_CHANNEL_FACTORY "grpc.sidechannel.channel_factory"

namespace grpc_core {
namespace experimental {

// Implementations of this class must be thread-safe.
class ChannelFactory {
 public:
  // Opaque handle representing a side-channel (not a
  // grpc_core::Channel). A ChannelHandle may outlive the ChannelFactory
  // that created it.
  class ChannelHandle {
   public:
    virtual ~ChannelHandle() = default;
  };

  virtual ~ChannelFactory() = default;

  // Creates a channel for key. Must be thread-safe.
  // Implementations must return a ChannelHandle obtained from
  // CreateCoreChannel(), CreateLameChannel(), or another gRPC-provided
  // ChannelFactory, because gRPC core downcasts the returned handle.
  virtual std::unique_ptr<ChannelHandle> CreateChannel(
      absl::string_view key) = 0;

  // Returns the channel argument vtable for std::shared_ptr<ChannelFactory>.
  static const grpc_arg_pointer_vtable* ChannelArgVtable();

  // Creates a core channel. Returns a lame channel on failure.
  // Does not take ownership of creds.
  static std::unique_ptr<ChannelHandle> CreateCoreChannel(
      absl::string_view target, grpc_channel_credentials* creds);

  // Creates a lame channel. RPCs on it fail with status.
  // status must not be OK.
  static std::unique_ptr<ChannelHandle> CreateLameChannel(
      absl::string_view target, absl::Status status);
};

}  // namespace experimental
}  // namespace grpc_core

#endif /* GRPC_CHANNEL_FACTORY_H */
