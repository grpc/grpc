//
//
// Copyright 2025 gRPC authors.
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
#include <grpc/grpc.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#define GRPC_ARG_TRANSPORT_FACTORY "grpc.internal.transport_factory"

namespace grpc_core {

class TransportFactory {
 public:
  // Opaque type representing a transport.
  class Transport {
   public:
    virtual ~Transport() = default;
  };

  virtual ~TransportFactory() = default;
  virtual std::unique_ptr<Transport> CreateTransport(absl::string_view key) = 0;

  // Returns the channel argument vtable for std::shared_ptr<TransportFactory>.
  static const grpc_arg_pointer_vtable* ChannelArgVtable();

  // Channel arg name.
  static absl::string_view ChannelArgName() {
    return GRPC_ARG_TRANSPORT_FACTORY;
  }

  // Creates a core transport. Returns a lame transport on failure.
  static std::unique_ptr<Transport> CreateCoreTransport(
      absl::string_view target, grpc_channel_credentials* creds);

  // Creates a lame transport. RPCs on it fail with status.
  // status must not be OK.
  static std::unique_ptr<Transport> CreateLameTransport(
      absl::string_view target, absl::Status status);
};

}  // namespace grpc_core

#endif /* GRPC_TRANSPORT_FACTORY_H */