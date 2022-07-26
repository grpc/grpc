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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_SOCKET_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_SOCKET_H

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/socket_notifier.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class WinSocket final : public SocketNotifier {
 public:
  class OpInfo {
   public:
    explicit OpInfo(WinSocket* win_socket) noexcept;
    // Signal an IOCP result has returned
    // If a callback is already primed for notification, it will be executed via
    // the WinSocket's EventEngine. Otherwise, a "pending iocp" flag will
    // be set.
    void SetReady();
    // Set error results for a completed op
    void SetError();
    // Retrieve results of overlapped operation (via Winsock API)
    void GetOverlappedResult();
    // TODO(hork): consider if the socket can be TOLD to do an operation instead
    // of leaking these internals.
    OVERLAPPED* overlapped() { return &overlapped_; }
    DWORD bytes_transferred() const { return bytes_transferred_; }
    int wsa_error() const { return wsa_error_; }

   private:
    friend class WinSocket;
    WinSocket* win_socket_;
    OVERLAPPED overlapped_;
    bool has_pending_iocp_{false};
    DWORD bytes_transferred_;
    int wsa_error_;
    absl::AnyInvocable<void()> callback;
  };

  WinSocket(SOCKET socket, EventEngine* event_engine) noexcept;
  ~WinSocket();
  // Schedule a shutdown of the socket operations. Will call the pending
  // operations to abort them. We need to do that this way because of the
  // various callsites of that function, which happens to be in various
  // mutex hold states, and that'd be unsafe to call them directly.
  void MaybeShutdown(absl::Status why) override;
  // Calling NotifyOnRead means either of two things:
  //  - The IOCP already completed in the background, and we need to call
  //    the callback now.
  //  - The IOCP hasn't completed yet, and we're queuing it for later.
  void NotifyOnRead(absl::AnyInvocable<void()> on_read) override;
  void NotifyOnWrite(absl::AnyInvocable<void()> on_write) override;
  void SetReadable() override;
  void SetWritable() override;
  bool IsShutdown() override;

  // Return the appropriate OpInfo for a given OVERLAPPED
  // Returns nullptr if the overlapped does not match either read or write ops.
  OpInfo* GetOpInfoForOverlapped(OVERLAPPED* overlapped);
  // -------------------------------------------------
  // TODO(hork): We need access to these for WSA* ops in TCP code.
  // Maybe we can encapsulate these calls inside of the OpInfo class. Would need
  // to rename it.
  OpInfo* read_info() { return &read_info_; }
  OpInfo* write_info() { return &write_info_; }
  // -------------------------------------------------
  // Accessor method for underlying socket
  SOCKET socket();

 private:
  void NotifyOnReady(OpInfo& info, absl::AnyInvocable<void()> callback);

  SOCKET socket_;
  grpc_core::Mutex mu_;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  EventEngine* event_engine_;
  OpInfo read_info_ ABSL_GUARDED_BY(mu_);
  OpInfo write_info_ ABSL_GUARDED_BY(mu_);
};

// Attempt to configure default socket settings
absl::Status PrepareSocket(SOCKET sock);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_SOCKET_H
