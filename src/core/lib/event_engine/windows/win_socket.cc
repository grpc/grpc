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

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/sync.h"

#if defined(__MSYS__) && defined(GPR_ARCH_64)
// Nasty workaround for nasty bug when using the 64 bits msys compiler
// in conjunction with Microsoft Windows headers.
#define GRPC_FIONBIO _IOW('f', 126, uint32_t)
#else
#define GRPC_FIONBIO FIONBIO
#endif

namespace grpc_event_engine::experimental {

// ---- WinSocket ----

WinSocket::WinSocket(SOCKET socket, ThreadPool* thread_pool) noexcept
    : socket_(socket),
      thread_pool_(thread_pool),
      read_info_(this),
      write_info_(this) {}

WinSocket::~WinSocket() {
  CHECK(is_shutdown_.load());
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "WinSocket::" << this << " destroyed";
}

SOCKET WinSocket::raw_socket() { return socket_; }

void WinSocket::Shutdown() {
  // if already shutdown, return early. Otherwise, set the shutdown flag.
  if (is_shutdown_.exchange(true)) {
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << "WinSocket::" << this << " already shutting down";
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
  if (status != 0) {
    char* utf8_message = gpr_format_message(WSAGetLastError());
    GRPC_TRACE_LOG(event_engine_endpoint, INFO)
        << "Unable to retrieve DisconnectEx pointer : " << utf8_message;
    gpr_free(utf8_message);
  } else if (DisconnectEx(socket_, NULL, 0, 0) == FALSE) {
    auto last_error = WSAGetLastError();
    // DisconnectEx may be called when the socket is not connected. Ignore that
    // error, and log all others.
    if (last_error != WSAENOTCONN) {
      char* utf8_message = gpr_format_message(last_error);
      GRPC_TRACE_LOG(event_engine_endpoint, INFO)
          << "DisconnectEx failed: " << utf8_message;
      gpr_free(utf8_message);
    }
  }
  closesocket(socket_);
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "WinSocket::" << this << " socket closed";
}

void WinSocket::Shutdown(const grpc_core::DebugLocation& location,
                         absl::string_view reason) {
  GRPC_TRACE_LOG(event_engine_endpoint, INFO)
      << "WinSocket::" << this << " Shut down from " << location.file() << ":"
      << location.line() << ". Reason: " << reason.data();
  Shutdown();
}

void WinSocket::NotifyOnReady(OpState& info, EventEngine::Closure* closure) {
  if (IsShutdown()) {
    info.SetResult(WSAESHUTDOWN, 0, "NotifyOnReady");
    thread_pool_->Run(closure);
    return;
  };
  // It is an error if any notification is already registered for this socket.
  CHECK_EQ(std::exchange(info.closure_, closure), nullptr);
}

void WinSocket::NotifyOnRead(EventEngine::Closure* on_read) {
  NotifyOnReady(read_info_, on_read);
}

void WinSocket::NotifyOnWrite(EventEngine::Closure* on_write) {
  NotifyOnReady(write_info_, on_write);
}

void WinSocket::UnregisterReadCallback() {
  CHECK_NE(std::exchange(read_info_.closure_, nullptr), nullptr);
}

void WinSocket::UnregisterWriteCallback() {
  CHECK_NE(std::exchange(write_info_.closure_, nullptr), nullptr);
}

// ---- WinSocket::OpState ----

WinSocket::OpState::OpState(WinSocket* win_socket) noexcept
    : win_socket_(win_socket) {
  memset(&overlapped_, 0, sizeof(OVERLAPPED));
}

void WinSocket::OpState::SetReady() {
  auto* closure = std::exchange(closure_, nullptr);
  // If an IOCP event is returned for a socket, and no callback has been
  // registered for notification, this is invalid usage.
  CHECK_NE(closure, nullptr);
  win_socket_->thread_pool_->Run(closure);
}

void WinSocket::OpState::SetResult(int wsa_error, DWORD bytes,
                                   absl::string_view context) {
  bytes = wsa_error == 0 ? bytes : 0;
  result_ = OverlappedResult{
      /*wsa_error=*/wsa_error, /*bytes_transferred=*/bytes,
      /*error_status=*/wsa_error == 0 ? absl::OkStatus()
                                      : GRPC_WSA_ERROR(wsa_error, context)};
}

void WinSocket::OpState::SetErrorStatus(absl::Status error_status) {
  result_ = OverlappedResult{/*wsa_error=*/0, /*bytes_transferred=*/0,
                             /*error_status=*/error_status};
}

void WinSocket::OpState::GetOverlappedResult() {
  GetOverlappedResult(win_socket_->raw_socket());
}

void WinSocket::OpState::GetOverlappedResult(SOCKET sock) {
  if (win_socket_->IsShutdown()) {
    SetResult(WSA_OPERATION_ABORTED, 0, "GetOverlappedResult");
    return;
  }
  DWORD flags = 0;
  DWORD bytes;
  BOOL success =
      WSAGetOverlappedResult(sock, &overlapped_, &bytes, FALSE, &flags);
  auto wsa_error = success ? 0 : WSAGetLastError();
  SetResult(wsa_error, bytes, "WSAGetOverlappedResult");
}

bool WinSocket::IsShutdown() { return is_shutdown_.load(); }

WinSocket::OpState* WinSocket::GetOpInfoForOverlapped(OVERLAPPED* overlapped) {
  GRPC_TRACE_LOG(event_engine_poller, INFO)
      << "WinSocket::" << this
      << " looking for matching OVERLAPPED::" << overlapped << ". read("
      << &read_info_.overlapped_ << ") write(" << &write_info_.overlapped_
      << ")";
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

absl::Status SetSocketNonBlock(SOCKET sock) {
  return grpc_tcp_set_non_block(sock);
}

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

absl::StatusOr<EventEngine::ResolvedAddress> SocketToAddress(SOCKET socket) {
  char addr[EventEngine::ResolvedAddress::MAX_SIZE_BYTES];
  int addr_len = sizeof(addr);
  if (getsockname(socket, reinterpret_cast<sockaddr*>(addr), &addr_len) < 0) {
    return GRPC_WSA_ERROR(WSAGetLastError(),
                          "Failed to get local socket name using getsockname");
  }
  return EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(addr),
                                      addr_len);
}

}  // namespace grpc_event_engine::experimental

#endif  // GPR_WINDOWS
