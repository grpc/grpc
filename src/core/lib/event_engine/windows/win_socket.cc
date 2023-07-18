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

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"

#if defined(__MSYS__) && defined(GPR_ARCH_64)
// Nasty workaround for nasty bug when using the 64 bits msys compiler
// in conjunction with Microsoft Windows headers.
#define GRPC_FIONBIO _IOW('f', 126, uint32_t)
#else
#define GRPC_FIONBIO FIONBIO
#endif

namespace grpc_event_engine {
namespace experimental {

// ---- WinSocket ----

WinSocket::WinSocket(SOCKET socket, ThreadPool* thread_pool) noexcept
    : socket_(socket),
      thread_pool_(thread_pool),
      read_info_(this),
      write_info_(this) {}

WinSocket::~WinSocket() {
  GPR_ASSERT(is_shutdown_.load());
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WinSocket::%p destroyed", this);
}

SOCKET WinSocket::raw_socket() { return socket_; }

void WinSocket::Shutdown() {
  // if already shutdown, return early. Otherwise, set the shutdown flag.
  if (is_shutdown_.exchange(true)) {
    GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WinSocket::%p already shutting down",
                                     this);
    return;
  }
  // Grab the function pointer for DisconnectEx for that specific socket.
  // It may change depending on the interface.
  GUID guid = WSAID_DISCONNECTEX;
  LPFN_DISCONNECTEX DisconnectEx;
  DWORD ioctl_num_bytes;
  int status = WSAIoctl(socket_, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid,
                        sizeof(guid), &DisconnectEx, sizeof(DisconnectEx),
                        &ioctl_num_bytes, NULL, NULL);

  if (status == 0) {
    DisconnectEx(socket_, NULL, 0, 0);
  } else {
    char* utf8_message = gpr_format_message(WSAGetLastError());
    gpr_log(GPR_INFO, "Unable to retrieve DisconnectEx pointer : %s",
            utf8_message);
    gpr_free(utf8_message);
  }
  closesocket(socket_);
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE("WinSocket::%p socket closed", this);
}

void WinSocket::Shutdown(const grpc_core::DebugLocation& location,
                         absl::string_view reason) {
  GRPC_EVENT_ENGINE_ENDPOINT_TRACE(
      "WinSocket::%p Shut down from %s:%d. Reason: %s", this, location.file(),
      location.line(), reason.data());
  Shutdown();
}

void WinSocket::NotifyOnReady(OpState& info, EventEngine::Closure* closure) {
  if (IsShutdown()) {
    info.SetError(WSAESHUTDOWN);
    thread_pool_->Run(closure);
    return;
  };
  if (std::exchange(info.has_pending_iocp_, false)) {
    thread_pool_->Run(closure);
  } else {
    EventEngine::Closure* prev = nullptr;
    GPR_ASSERT(info.closure_.compare_exchange_strong(prev, closure));
  }
}

void WinSocket::NotifyOnRead(EventEngine::Closure* on_read) {
  NotifyOnReady(read_info_, on_read);
}

void WinSocket::NotifyOnWrite(EventEngine::Closure* on_write) {
  NotifyOnReady(write_info_, on_write);
}

// ---- WinSocket::OpState ----

WinSocket::OpState::OpState(WinSocket* win_socket) noexcept
    : win_socket_(win_socket) {
  memset(&overlapped_, 0, sizeof(OVERLAPPED));
}

void WinSocket::OpState::SetReady() {
  GPR_ASSERT(!has_pending_iocp_);
  auto* closure = closure_.exchange(nullptr);
  if (closure) {
    win_socket_->thread_pool_->Run(closure);
  } else {
    has_pending_iocp_ = true;
  }
}

void WinSocket::OpState::SetError(int wsa_error) {
  result_ = OverlappedResult{/*wsa_error=*/wsa_error, /*bytes_transferred=*/0};
}

void WinSocket::OpState::SetResult(OverlappedResult result) {
  result_ = result;
}

void WinSocket::OpState::GetOverlappedResult() {
  GetOverlappedResult(win_socket_->raw_socket());
}

void WinSocket::OpState::GetOverlappedResult(SOCKET sock) {
  if (win_socket_->IsShutdown()) {
    result_ = OverlappedResult{/*wsa_error=*/WSA_OPERATION_ABORTED,
                               /*bytes_transferred=*/0};
    return;
  }
  DWORD flags = 0;
  DWORD bytes;
  BOOL success =
      WSAGetOverlappedResult(sock, &overlapped_, &bytes, FALSE, &flags);
  result_ = OverlappedResult{/*wsa_error=*/success ? 0 : WSAGetLastError(),
                             /*bytes_transferred=*/bytes};
}

bool WinSocket::IsShutdown() { return is_shutdown_.load(); }

WinSocket::OpState* WinSocket::GetOpInfoForOverlapped(OVERLAPPED* overlapped) {
  GRPC_EVENT_ENGINE_POLLER_TRACE(
      "WinSocket::%p looking for matching OVERLAPPED::%p. "
      "read(%p) write(%p)",
      this, overlapped, &read_info_.overlapped_, &write_info_.overlapped_);
  if (overlapped == &read_info_.overlapped_) return &read_info_;
  if (overlapped == &write_info_.overlapped_) return &write_info_;
  return nullptr;
}

namespace {

grpc_error_handle grpc_tcp_set_non_block(SOCKET sock) {
  int status;
  uint32_t param = 1;
  DWORD ret;
  status = WSAIoctl(sock, GRPC_FIONBIO, &param, sizeof(param), NULL, 0, &ret,
                    NULL, NULL);
  return status == 0
             ? absl::OkStatus()
             : GRPC_WSA_ERROR(WSAGetLastError(), "WSAIoctl(GRPC_FIONBIO)");
}

static grpc_error_handle set_dualstack(SOCKET sock) {
  int status;
  DWORD param = 0;
  status = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&param,
                      sizeof(param));
  return status == 0
             ? absl::OkStatus()
             : GRPC_WSA_ERROR(WSAGetLastError(), "setsockopt(IPV6_V6ONLY)");
}

static grpc_error_handle enable_socket_low_latency(SOCKET sock) {
  int status;
  BOOL param = TRUE;
  status = ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                        reinterpret_cast<char*>(&param), sizeof(param));
  if (status == SOCKET_ERROR) {
    status = WSAGetLastError();
  }
  return status == 0 ? absl::OkStatus()
                     : GRPC_WSA_ERROR(status, "setsockopt(TCP_NODELAY)");
}

}  // namespace

absl::Status PrepareSocket(SOCKET sock) {
  absl::Status err;
  err = grpc_tcp_set_non_block(sock);
  if (!err.ok()) return err;
  err = enable_socket_low_latency(sock);
  if (!err.ok()) return err;
  err = set_dualstack(sock);
  if (!err.ok()) return err;
  return absl::OkStatus();
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
