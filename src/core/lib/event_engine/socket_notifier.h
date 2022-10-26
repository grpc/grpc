// Copyright 2022 gRPC authors.
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_SOCKET_NOTIFIER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_SOCKET_NOTIFIER_H

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace experimental {

// Generically wraps a socket/fd, and manages the registration of callbacks and
// triggering of notifications on it.
class SocketNotifier {
 public:
  virtual ~SocketNotifier() = default;
  // Schedule on_read to be invoked when the underlying socket
  // becomes readable.
  // If the socket is already readable, the callback will be executed as soon as
  // possible.
  virtual void NotifyOnRead(EventEngine::Closure* on_read) = 0;
  // Schedule on_write to be invoked when the underlying socket
  // becomes writable.
  // If the socket is already writable, the callback will be executed as soon as
  // possible.
  virtual void NotifyOnWrite(EventEngine::Closure* on_write) = 0;
  // Set a readable event on the underlying socket.
  virtual void SetReadable() = 0;
  // Set a writable event on the underlying socket.
  virtual void SetWritable() = 0;
  // Shutdown a SocketNotifier. After this operation, NotifyXXX and SetXXX
  // operations cannot be performed.
  virtual void MaybeShutdown(absl::Status why) = 0;
  // Returns true if the SocketNotifier has been shutdown.
  virtual bool IsShutdown() = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_SOCKET_NOTIFIER_H