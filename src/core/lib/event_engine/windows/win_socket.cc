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

#include "src/core/lib/event_engine/executor/executor.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"

#if defined(__MSYS__) && defined(GPR_ARCH_64)
/* Nasty workaround for nasty bug when using the 64 bits msys compiler
   in conjunction with Microsoft Windows headers. */
#define GRPC_FIONBIO _IOW('f', 126, uint32_t)
#else
#define GRPC_FIONBIO FIONBIO
#endif

namespace grpc_event_engine {
namespace experimental {

WinSocket::WinSocket(SOCKET socket, Executor* executor) noexcept
    : socket_(socket),
      executor_(executor),
      read_info_(OpState(this)),
      write_info_(OpState(this)) {}

WinSocket::~WinSocket() { GPR_ASSERT(is_shutdown_.load()); }

SOCKET WinSocket::socket() { return socket_; }

void WinSocket::MaybeShutdown(absl::Status why) {
  // if already shutdown, return early. Otherwise, set the shutdown flag.
  if (is_shutdown_.exchange(true)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
      gpr_log(GPR_DEBUG, "WinSocket::%p already shutting down", this);
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    gpr_log(GPR_DEBUG, "WinSocket::%p shutting down now. Reason: %s", this,
            why.ToString().c_str());
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
}

void WinSocket::NotifyOnReady(OpState& info, EventEngine::Closure* closure) {
  if (IsShutdown()) {
    info.SetError(WSAESHUTDOWN);
    executor_->Run(closure);
    return;
  };
  if (absl::exchange(info.has_pending_iocp_, false)) {
    executor_->Run(closure);
  } else {
    info.closure_ = closure;
  }
}

void WinSocket::NotifyOnRead(EventEngine::Closure* on_read) {
  NotifyOnReady(read_info_, on_read);
}

void WinSocket::NotifyOnWrite(EventEngine::Closure* on_write) {
  NotifyOnReady(write_info_, on_write);
}

WinSocket::OpState::OpState(WinSocket* win_socket) noexcept
    : win_socket_(win_socket), closure_(nullptr) {}

void WinSocket::OpState::SetReady() {
  GPR_ASSERT(!has_pending_iocp_);
  if (closure_) {
    win_socket_->executor_->Run(closure_);
  } else {
    has_pending_iocp_ = true;
  }
}

void WinSocket::OpState::SetError(int wsa_error) {
  bytes_transferred_ = 0;
  wsa_error_ = wsa_error;
}

void WinSocket::OpState::GetOverlappedResult() {
  DWORD flags = 0;
  DWORD bytes;
  BOOL success = WSAGetOverlappedResult(win_socket_->socket(), &overlapped_,
                                        &bytes, FALSE, &flags);
  bytes_transferred_ = bytes;
  wsa_error_ = success ? 0 : WSAGetLastError();
}

void WinSocket::SetReadable() { read_info_.SetReady(); }

void WinSocket::SetWritable() { write_info_.SetReady(); }

bool WinSocket::IsShutdown() { return is_shutdown_.load(); }

WinSocket::OpState* WinSocket::GetOpInfoForOverlapped(OVERLAPPED* overlapped) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    gpr_log(GPR_DEBUG,
            "WinSocket::%p looking for matching OVERLAPPED::%p. "
            "read(%p) write(%p)",
            this, overlapped, &read_info_.overlapped_,
            &write_info_.overlapped_);
  }
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
             ? GRPC_ERROR_NONE
             : GRPC_WSA_ERROR(WSAGetLastError(), "WSAIoctl(GRPC_FIONBIO)");
}

static grpc_error_handle set_dualstack(SOCKET sock) {
  int status;
  DWORD param = 0;
  status = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&param,
                      sizeof(param));
  return status == 0
             ? GRPC_ERROR_NONE
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
  return status == 0 ? GRPC_ERROR_NONE
                     : GRPC_WSA_ERROR(status, "setsockopt(TCP_NODELAY)");
}

}  // namespace

absl::Status PrepareSocket(SOCKET sock) {
  absl::Status err;
  err = grpc_tcp_set_non_block(sock);
  if (!GRPC_ERROR_IS_NONE(err)) return err;
  err = enable_socket_low_latency(sock);
  if (!GRPC_ERROR_IS_NONE(err)) return err;
  err = set_dualstack(sock);
  if (!GRPC_ERROR_IS_NONE(err)) return err;
  return GRPC_ERROR_NONE;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
