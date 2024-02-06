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

#ifndef GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INTERFACE_H
#define GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INTERFACE_H

#include <grpc/support/port_platform.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/transport/call_factory.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

// New channel interface for use with the call v3 stack.
class ChannelInterface : public CallFactory {
 public:
  virtual ~ChannelInterface() = default;

  virtual absl::string_view target() const = 0;

  virtual void GetChannelInfo(const grpc_channel_info* channel_info) = 0;

  virtual grpc_connectivity_state CheckConnectivityState(
      bool try_to_connect) = 0;

  virtual void AddConnectivityWatcher(
      grpc_connectivity_state initial_state,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher) = 0;

  virtual void RemoveConnectivityWatcher(
      AsyncConnectivityStateWatcherInterface* watcher) = 0;

  virtual void ResetConnectionBackoff() = 0;

  // For use in tests only.
  virtual void SendPing(absl::AnyInvocable<void(absl::Status)> on_initiate,
                        absl::AnyInvocable<void(absl::Status)> on_ack) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_CHANNEL_INTERFACE_H
