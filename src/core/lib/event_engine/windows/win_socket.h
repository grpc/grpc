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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WIN_SOCKET_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WIN_SOCKET_H

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/executor/executor.h"
#include "src/core/lib/event_engine/socket_notifier.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class WinSocket final : public SocketNotifier {
 public:
  // State related to a Read or Write socket operation
  class OpState {
   public:
    explicit OpState(WinSocket* win_socket) noexcept;
    // Signal a result has returned
    // If a callback is already primed for notification, it will be executed via
    // the WinSocket's Executor. Otherwise, a "pending iocp" flag will
    // be set.
    void SetReady();
    // Set error results for a completed op
    void SetError(int wsa_error);
    // Retrieve results of overlapped operation (via Winsock API)
    void GetOverlappedResult();
    // OVERLAPPED, needed for Winsock API calls
    LPOVERLAPPED overlapped() { return &overlapped_; }
    // Data from the previous operation, set via GetOverlappedResult
    DWORD bytes_transferred() const { return bytes_transferred_; }
    // Previous error if set.
    int wsa_error() const { return wsa_error_; }
    EventEngine::Closure* closure() { return closure_; }

   private:
    friend class WinSocket;

    OVERLAPPED overlapped_;
    WinSocket* win_socket_ = nullptr;
    EventEngine::Closure* closure_ = nullptr;
    bool has_pending_iocp_ = false;
    DWORD bytes_transferred_;
    int wsa_error_;
  };

  WinSocket(SOCKET socket, Executor* executor) noexcept;
  ~WinSocket();
  // Calling NotifyOnRead means either of two things:
  //  - The IOCP already completed in the background, and we need to call
  //    the callback now.
  //  - The IOCP hasn't completed yet, and we're queuing it for later.
  void NotifyOnRead(EventEngine::Closure* on_read) override;
  void NotifyOnWrite(EventEngine::Closure* on_write) override;
  void SetReadable() override;
  void SetWritable() override;
  // Schedule a shutdown of the socket operations. Will call the pending
  // operations to abort them. We need to do that this way because of the
  // various callsites of that function, which happens to be in various
  // mutex hold states, and that'd be unsafe to call them directly.
  void MaybeShutdown(absl::Status why) override;
  bool IsShutdown() override;

  // Return the appropriate OpState for a given OVERLAPPED
  // Returns nullptr if the overlapped does not match either read or write ops.
  OpState* GetOpInfoForOverlapped(OVERLAPPED* overlapped);
  // Getters for the operation state data.
  OpState* read_info() { return &read_info_; }
  OpState* write_info() { return &write_info_; }
  // Accessor method for underlying socket
  SOCKET socket();

 private:
  void NotifyOnReady(OpState& info, EventEngine::Closure* closure);

  SOCKET socket_;
  std::atomic<bool> is_shutdown_{false};
  Executor* executor_;
  // These OpStates are effectively synchronized using their respective
  // OVERLAPPED structures and the Overlapped I/O APIs. For example, OpState
  // users should not attempt to read their bytes_transeferred until
  // GetOverlappedResult has returned, to ensure there are no two threads
  // reading and writing the same values concurrently.
  //
  // Callers must also ensure that at most one read and write operation are
  // occurring at a time. Attempting to do multiple concurrent reads/writes will
  // have undefined behavior.
  OpState read_info_;
  OpState write_info_;
};

// Attempt to configure default socket settings
absl::Status PrepareSocket(SOCKET sock);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WIN_SOCKET_H
