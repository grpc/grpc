// Copyright 2023 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_LISTENER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_LISTENER_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class WindowsEventEngineListener : public EventEngine::Listener {
 public:
  WindowsEventEngineListener(
      IOCP* iocp, AcceptCallback accept_cb,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory);
  ~WindowsEventEngineListener() override;
  absl::StatusOr<int> Bind(const EventEngine::ResolvedAddress& addr) override;
  absl::Status Start() override;

 private:
  friend class SinglePortSocketListener;

  absl::StatusOr<SinglePortSocketListener*> AddSinglePortSocketListener(
      SOCKET sock, EventEngine::ResolvedAddress addr);

  IOCP* iocp_;
  AcceptCallback accept_cb_;
  absl::AnyInvocable<void(absl::Status)> on_shutdown_;
  std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory_;
  grpc_core::Mutex socket_listeners_mu_;
  std::list<std::unique_ptr<SinglePortSocketListener>> socket_listeners_
      ABSL_GUARDED_BY(socket_listeners_mu_);
  // TODO(hork): This can be managed with SinglePortSocketListener lifetimes
  // instead
  int active_ports_ ABSL_GUARDED_BY(socket_listeners_mu_) = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_LISTENER_H