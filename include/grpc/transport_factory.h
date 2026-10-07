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

#ifndef GRPC_TRANSPORT_FACTORY_H
#define GRPC_TRANSPORT_FACTORY_H

#include <grpc/credentials.h>
#include <grpc/impl/grpc_types.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

// Channel argument key for a pointer to a
// std::shared_ptr<grpc_core::experimental::TransportFactory>, stored with
// TransportFactory::ChannelArgVtable().
#define GRPC_ARG_TRANSPORT_FACTORY "grpc.sidechannel.transport_factory"

namespace grpc_core {
namespace experimental {

// Implementations of this class must be thread-safe.
class TransportFactory {
 public:
  // Opaque handle representing a side-channel transport (not a
  // grpc_core::Transport). A TransportHandle may outlive the TransportFactory
  // that created it.
  class TransportHandle {
   public:
    virtual ~TransportHandle() = default;
  };

  virtual ~TransportFactory() = default;

  // Creates a transport for key. Must be thread-safe.
  // Implementations must return a TransportHandle obtained from
  // CreateChannelTransport(), CreateLameTransport(), or another gRPC-provided
  // TransportFactory, because gRPC core downcasts the returned handle.
  virtual std::unique_ptr<TransportHandle> CreateTransport(
      absl::string_view key) = 0;

  // Returns the channel argument vtable for std::shared_ptr<TransportFactory>.
  static const grpc_arg_pointer_vtable* ChannelArgVtable();

  // Creates a channel-backed transport. Returns a lame transport on failure.
  // Does not take ownership of creds.
  static std::unique_ptr<TransportHandle> CreateChannelTransport(
      absl::string_view target, grpc_channel_credentials* creds);

  // Creates a lame transport. RPCs on it fail with status.
  // status must not be OK.
  static std::unique_ptr<TransportHandle> CreateLameTransport(
      absl::string_view target, absl::Status status);
};

}  // namespace experimental
}  // namespace grpc_core

#endif /* GRPC_TRANSPORT_FACTORY_H */
