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

#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/windows/socket.h"
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

WinWrappedSocket::WinWrappedSocket(SOCKET socket,
                                   EventEngine* event_engine) noexcept
    : socket_(socket),
      event_engine_(event_engine),
      read_info_(OpInfo(this)),
      write_info_(OpInfo(this)) {}

WinWrappedSocket::~WinWrappedSocket() {}

SOCKET WinWrappedSocket::Socket() { return socket_; }

void WinWrappedSocket::MaybeShutdown(absl::Status why) {
  grpc_core::MutexLock lock(&mu_);
  // if already shutdown, return early. Otherwise, set the shutdown flag.
  if (is_shutdown_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
      gpr_log(GPR_DEBUG, "WinWrappedSocket::%p already shutting down", this);
    }
    return;
  }
  is_shutdown_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    gpr_log(GPR_DEBUG, "WinWrappedSocket::%p shutting down now. Reason: %s",
            this, why.ToString().c_str());
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

void WinWrappedSocket::NotifyOnReady(OpInfo& info,
                                     absl::AnyInvocable<void()> callback) {
  grpc_core::MutexLock lock(&mu_);
  if (info.has_pending_iocp_) {
    info.has_pending_iocp_ = false;
    event_engine_->Run(std::move(callback));
  } else {
    info.callback = std::move(callback);
  }
}

void WinWrappedSocket::NotifyOnRead(absl::AnyInvocable<void()> on_read) {
  NotifyOnReady(read_info_, std::move(on_read));
}

void WinWrappedSocket::NotifyOnWrite(absl::AnyInvocable<void()> on_write) {
  NotifyOnReady(write_info_, std::move(on_write));
}

WinWrappedSocket::OpInfo::OpInfo(WinWrappedSocket* win_socket) noexcept
    : win_socket_(win_socket) {}

void WinWrappedSocket::OpInfo::SetReady() {
  grpc_core::MutexLock lock(&win_socket_->mu_);
  GPR_ASSERT(!has_pending_iocp_);
  if (callback) {
    win_socket_->event_engine_->Run(std::move(callback));
    callback = nullptr;
  } else {
    has_pending_iocp_ = true;
  }
}

void WinWrappedSocket::OpInfo::SetError() {
  bytes_transferred_ = 0;
  wsa_error_ = WSA_OPERATION_ABORTED;
}

void WinWrappedSocket::OpInfo::GetOverlappedResult() {
  DWORD flags = 0;
  DWORD bytes;
  BOOL success = WSAGetOverlappedResult(win_socket_->Socket(), &overlapped_,
                                        &bytes, FALSE, &flags);
  bytes_transferred_ = bytes;
  wsa_error_ = success ? 0 : WSAGetLastError();
}

void WinWrappedSocket::SetReadable() { read_info_.SetReady(); }

void WinWrappedSocket::SetWritable() { write_info_.SetReady(); }

bool WinWrappedSocket::IsShutdown() { return is_shutdown_; }

WinWrappedSocket::OpInfo* WinWrappedSocket::GetOpInfoForOverlapped(
    OVERLAPPED* overlapped) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_event_engine_trace)) {
    gpr_log(GPR_DEBUG,
            "WinWrappedSocket::%p looking for matching OVERLAPPED::%p. "
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
  // DO NOT SUBMIT(hork): WinServer does not support dual stack. How is iomgr
  // working?
  //   err = set_dualstack(sock);
  //   if (!GRPC_ERROR_IS_NONE(err)) return err;
  err = enable_socket_low_latency(sock);
  if (!GRPC_ERROR_IS_NONE(err)) return err;
  return GRPC_ERROR_NONE;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
