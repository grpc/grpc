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

#include <grpc/support/alloc.h>
#include <grpc/support/log_windows.h>

#include <chrono>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/time_util.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/crash.h"

namespace grpc_event_engine::experimental {

IOCP::IOCP(ThreadPool* thread_pool) noexcept
    : thread_pool_(thread_pool),
      iocp_handle_(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr,
                                          (ULONG_PTR) nullptr, 0)) {
  CHECK(iocp_handle_);
  WSASocketFlagsInit();
}

// Shutdown must be called prior to deletion
IOCP::~IOCP() {}

std::unique_ptr<WinSocket> IOCP::Watch(SOCKET socket) {
  auto wrapped_socket = std::make_unique<WinSocket>(socket, thread_pool_);
  HANDLE ret = CreateIoCompletionPort(
      reinterpret_cast<HANDLE>(socket), iocp_handle_,
      reinterpret_cast<uintptr_t>(wrapped_socket.get()), 0);
  if (!ret) {
    grpc_core::Crash(
        GRPC_WSA_ERROR(WSAGetLastError(), "Unable to add socket to iocp")
            .ToString());
  }
  CHECK(ret == iocp_handle_);
  return wrapped_socket;
}

void IOCP::Shutdown() {
  GRPC_TRACE_LOG(event_engine_poller, INFO)
      << "IOCP::" << this
      << " shutting down. Outstanding kicks: " << outstanding_kicks_.load();
  while (outstanding_kicks_.load() > 0) {
    Work(std::chrono::hours(42), []() {});
  }
  CHECK(CloseHandle(iocp_handle_));
}

Poller::WorkResult IOCP::Work(EventEngine::Duration timeout,
                              absl::FunctionRef<void()> schedule_poll_again) {
  DWORD bytes = 0;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;
  GRPC_TRACE_LOG(event_engine_poller, INFO)
      << "IOCP::" << this << " doing work";
  BOOL success = GetQueuedCompletionStatus(
      iocp_handle_, &bytes, &completion_key, &overlapped,
      static_cast<DWORD>(Milliseconds(timeout)));
  if (success == 0 && overlapped == nullptr) {
    GRPC_TRACE_LOG(event_engine_poller, INFO)
        << "IOCP::" << this << " deadline exceeded";
    return Poller::WorkResult::kDeadlineExceeded;
  }
  CHECK(completion_key);
  CHECK(overlapped);
  if (overlapped == &kick_overlap_) {
    GRPC_TRACE_LOG(event_engine_poller, INFO) << "IOCP::" << this << " kicked";
    outstanding_kicks_.fetch_sub(1);
    if (completion_key == (ULONG_PTR)&kick_token_) {
      return Poller::WorkResult::kKicked;
    }
    grpc_core::Crash(
        absl::StrFormat("Unknown custom completion key: %lu", completion_key));
  }
  GRPC_TRACE_LOG(event_engine_poller, INFO)
      << "IOCP::" << this << " got event on OVERLAPPED::" << overlapped;
  // Safety note: socket is guaranteed to exist when managed by a
  // WindowsEndpoint. If an overlapped event came in, then either a read event
  // handler is registered, which keeps the socket alive, or the WindowsEndpoint
  // (which keeps the socket alive) has done an asynchronous WSARecv and is
  // about to register for notification of an overlapped event.
  auto* socket = reinterpret_cast<WinSocket*>(completion_key);
  WinSocket::OpState* info = socket->GetOpInfoForOverlapped(overlapped);
  CHECK_NE(info, nullptr);
  info->GetOverlappedResult();
  info->SetReady();
  schedule_poll_again();
  return Poller::WorkResult::kOk;
}

void IOCP::Kick() {
  outstanding_kicks_.fetch_add(1);
  CHECK(PostQueuedCompletionStatus(iocp_handle_, 0,
                                   reinterpret_cast<ULONG_PTR>(&kick_token_),
                                   &kick_overlap_));
}

DWORD IOCP::GetDefaultSocketFlags() {
  static DWORD wsa_socket_flags = WSASocketFlagsInit();
  return wsa_socket_flags;
}

DWORD IOCP::WSASocketFlagsInit() {
  DWORD wsa_socket_flags = WSA_FLAG_OVERLAPPED;
  // WSA_FLAG_NO_HANDLE_INHERIT may be not supported on the older Windows
  // versions, see
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms742212(v=vs.85).aspx
  // for details.
  SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                          wsa_socket_flags | WSA_FLAG_NO_HANDLE_INHERIT);
  if (sock != INVALID_SOCKET) {
    // Windows 7, Windows 2008 R2 with SP1 or later
    wsa_socket_flags |= WSA_FLAG_NO_HANDLE_INHERIT;
    closesocket(sock);
  }
  return wsa_socket_flags;
}

}  // namespace grpc_event_engine::experimental

#endif  // GPR_WINDOWS
