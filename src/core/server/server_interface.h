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

#ifndef GRPC_SRC_CORE_SERVER_SERVER_INTERFACE_H
#define GRPC_SRC_CORE_SERVER_SERVER_INTERFACE_H

#include "absl/functional/any_invocable.h"

#include <grpc/impl/compression_types.h>
#include <grpc/support/port_platform.h>

#include "src/core/channelz/channelz.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/step_timer.h"

namespace grpc_core {

class ServerCallTracerFactory;
class Transport;

class ConnectionId {
 public:
  explicit ConnectionId(uintptr_t id) : id_(id) {}

  ChannelArgs::Pointer ToChannelArgsPointer() {
    return ChannelArgs::UnownedPointer(reinterpret_cast<void*>(id_));
  }

  ConnectionId FromChannelArgsPointer(void* ptr) {
    return ConnectionId(reinterpret_cast<uintptr_t>(ptr));
  }

 private:
  uintptr_t id_ = 0;
};

// This class is a hack to avoid a circular dependency that would be
// caused by the code in call.cc depending directly on the server code.
// TODO(roth): After the call v3 migration, find a cleaner way to do this.
class ServerInterface {
 public:
  virtual ~ServerInterface() = default;

  virtual const ChannelArgs& channel_args() const = 0;
  virtual channelz::ServerNode* channelz_node() const = 0;
  virtual ServerCallTracerFactory* server_call_tracer_factory() const = 0;
  virtual grpc_compression_options compression_options() const = 0;

  virtual RefCountedPtr<Transport> GetTransport(ConnectionId id) = 0;
  virtual void RemoveTransport(ConnectionId id) = 0;

  virtual StepTimer::Handle RunWithNextMaxAgeTimer(
      absl::AnyInvocable<void()> fn) = 0;
  virtual void CancelMaxAgeTimer(StepTimer::Handle handle) = 0;
  virtual StepTimer::Handle RunWithNextMaxAgeGraceTimer(
      absl::AnyInvocable<void()> fn) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_SERVER_SERVER_INTERFACE_H
