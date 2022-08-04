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
#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <chrono>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"

namespace grpc_event_engine {
namespace experimental {

IOCP::IOCP(Executor* executor) noexcept
    : executor_(executor),
      iocp_handle_(CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL,
                                          (ULONG_PTR)NULL, 0)) {
  GPR_ASSERT(iocp_handle_);
  WSASocketFlagsInit();
}

// Shutdown must be called prior to deletion
IOCP::~IOCP() {}

WinSocket* IOCP::Watch(SOCKET socket) {
  WinSocket* wrapped_socket = new WinSocket(socket, executor_);
  HANDLE ret =
      CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), iocp_handle_,
                             reinterpret_cast<uintptr_t>(wrapped_socket), 0);
  if (!ret) {
    char* utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_ERROR, "Unable to add socket to iocp: %s", utf8_message);
    gpr_free(utf8_message);
    __debugbreak();
    abort();
  }
  GPR_ASSERT(ret == iocp_handle_);
  return wrapped_socket;
}

void IOCP::Shutdown() {
  while (outstanding_kicks_.load() > 0) {
    Work(std::chrono::hours(42));
  }
  GPR_ASSERT(CloseHandle(iocp_handle_));
}

Poller::WorkResult IOCP::Work(EventEngine::Duration timeout) {
  static const absl::Status kDeadlineExceeded = absl::DeadlineExceededError(
      absl::StrFormat("IOCP::%p: Received no completions", this));
  static const absl::Status kKicked =
      absl::AbortedError(absl::StrFormat("IOCP::%p: Awoken from a kick", this));
  DWORD bytes = 0;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    gpr_log(GPR_DEBUG, "IOCP::%p doing work", this);
  }
  BOOL success = GetQueuedCompletionStatus(
      iocp_handle_, &bytes, &completion_key, &overlapped,
      static_cast<DWORD>(Milliseconds(timeout)));
  if (success == 0 && overlapped == NULL) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
      gpr_log(GPR_DEBUG, "IOCP::%p deadline exceeded", this);
    }
    return Poller::DeadlineExceeded{};
  }
  GPR_ASSERT(completion_key && overlapped);
  if (overlapped == &kick_overlap_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
      gpr_log(GPR_DEBUG, "IOCP::%p kicked", this);
    }
    outstanding_kicks_.fetch_sub(1);
    if (completion_key == (ULONG_PTR)&kick_token_) {
      return Poller::Kicked{};
    }
    gpr_log(GPR_ERROR, "Unknown custom completion key: %p", completion_key);
    abort();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    gpr_log(GPR_DEBUG, "IOCP::%p got event on OVERLAPPED::%p", this,
            overlapped);
  }
  WinSocket* socket = reinterpret_cast<WinSocket*>(completion_key);
  // TODO(hork): move the following logic into the WinSocket impl.
  WinSocket::OpState* info = socket->GetOpInfoForOverlapped(overlapped);
  GPR_ASSERT(info != nullptr);
  if (socket->IsShutdown()) {
    info->SetError(WSAESHUTDOWN);
  } else {
    info->GetOverlappedResult();
  }
  if (info->closure() != nullptr) return Events{info->closure()};
  // No callback registered. Set ready and return an empty set
  info->SetReady();
  return Events{};
}

void IOCP::Kick() {
  outstanding_kicks_.fetch_add(1);
  GPR_ASSERT(PostQueuedCompletionStatus(
      iocp_handle_, 0, reinterpret_cast<ULONG_PTR>(&kick_token_),
      &kick_overlap_));
}

DWORD IOCP::GetDefaultSocketFlags() {
  static DWORD wsa_socket_flags = WSASocketFlagsInit();
  return wsa_socket_flags;
}

DWORD IOCP::WSASocketFlagsInit() {
  DWORD wsa_socket_flags = WSA_FLAG_OVERLAPPED;
  /* WSA_FLAG_NO_HANDLE_INHERIT may be not supported on the older Windows
     versions, see
     https://msdn.microsoft.com/en-us/library/windows/desktop/ms742212(v=vs.85).aspx
     for details. */
  SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                          wsa_socket_flags | WSA_FLAG_NO_HANDLE_INHERIT);
  if (sock != INVALID_SOCKET) {
    /* Windows 7, Windows 2008 R2 with SP1 or later */
    wsa_socket_flags |= WSA_FLAG_NO_HANDLE_INHERIT;
    closesocket(sock);
  }
  return wsa_socket_flags;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
