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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_SOCKET_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_SOCKET_H

#include <grpc/support/port_platform.h>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"

#include "src/core/lib/event_engine/windows/event_poller.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class WinWrappedSocket final : public WrappedSocket {
 public:
  class OpInfo {
   public:
    explicit OpInfo(WinWrappedSocket* win_socket) noexcept;
    // Signal an IOCP result has returned
    // This method will execute the callback inline if one is set.
    void SetReady();
    // Set error results for a completed op
    void SetError();
    // Retrieve results of overlapped operation (via Winsock API)
    void GetOverlappedResult();

   private:
    friend class WinWrappedSocket;
    WinWrappedSocket* win_socket_;
    OVERLAPPED overlapped_;
    bool has_pending_iocp_{false};
    DWORD bytes_transferred_;
    int wsa_error_;
    absl::AnyInvocable<void()> callback;
  };

  WinWrappedSocket(SOCKET socket, EventEngine* event_engine) noexcept;
  ~WinWrappedSocket();
  SOCKET Socket() override;
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
  // Same semantics as NotifyOnRead
  void NotifyOnWrite(absl::AnyInvocable<void()> on_write) override;
  void SetReadable() override;
  void SetWritable() override;
  bool IsShutdown() override;

  // Return the appropriate OpInfo for a given OVERLAPPED
  // Returns nullptr if the overlapped does not match either read or write ops.
  OpInfo* GetOpInfoForOverlapped(OVERLAPPED* overlapped);

 private:
  void NotifyOnReady(OpInfo& info, absl::AnyInvocable<void()> callback);

  SOCKET socket_;
  grpc_core::Mutex mu_;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // DO NOT SUBMIT(hork): is this needed?
  // std::atomic<bool> added_to_iocp_{false};
  EventEngine* event_engine_;
  OpInfo read_info_ ABSL_GUARDED_BY(mu_);
  OpInfo write_info_ ABSL_GUARDED_BY(mu_);
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_SOCKET_H
