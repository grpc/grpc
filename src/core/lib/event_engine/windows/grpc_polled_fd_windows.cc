// Copyright 2023 The gRPC Authors
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

#include "src/core/lib/event_engine/windows/grpc_polled_fd_windows.h"

#include <winsock2.h>

#include <ares.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include "grpc/support/log_windows.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_event_engine {
namespace experimental {

GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::GrpcPolledFdWindows(
    std::unique_ptr<WinSocket> winsocket, grpc_core::Mutex* mu,
    int address_family, int socket_type,
    absl::AnyInvocable<void()> on_shutdown_locked, EventEngine* event_engine)
    : mu_(mu),
      winsocket_(std::move(winsocket)),
      read_buf_(grpc_empty_slice()),
      write_buf_(grpc_empty_slice()),
      outer_read_closure_([this]() { OnIocpReadable(); }),
      outer_write_closure_([this]() { OnIocpWriteable(); }),
      name_(absl::StrFormat("c-ares socket: %" PRIdPTR,
                            winsocket_->raw_socket())),
      address_family_(address_family),
      socket_type_(socket_type),
      on_tcp_connect_locked_([this]() { OnTcpConnect(); }),
      on_shutdown_locked_(std::move(on_shutdown_locked)),
      event_engine_(event_engine) {}

GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::~GrpcPolledFdWindows() {
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| ~GrpcPolledFdWindows shutdown_called_: %d ", GetName(),
      shutdown_called_);
  grpc_core::CSliceUnref(read_buf_);
  grpc_core::CSliceUnref(write_buf_);
  GPR_ASSERT(read_closure_ == nullptr);
  GPR_ASSERT(write_closure_ == nullptr);
  if (!shutdown_called_) {
    // This can happen if the socket was never seen by grpc ares wrapper
    // code, i.e. if we never started I/O polling on it.
    winsocket_->Shutdown(DEBUG_LOCATION, "~GrpcPolledFdWindows");
  }
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::
    ScheduleAndNullReadClosure(absl::Status error) {
  event_engine_->Run([read_closure = std::move(read_closure_),
                      error]() mutable { read_closure(error); });
  read_closure_ = nullptr;
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::
    ScheduleAndNullWriteClosure(absl::Status error) {
  event_engine_->Run([write_closure = std::move(write_closure_),
                      error]() mutable { write_closure(error); });
  write_closure_ = nullptr;
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::
    RegisterForOnReadableLocked(
        absl::AnyInvocable<void(absl::Status)> read_closure) {
  GPR_ASSERT(read_closure_ == nullptr);
  read_closure_ = std::move(read_closure);
  GPR_ASSERT(GRPC_SLICE_LENGTH(read_buf_) == 0);
  grpc_core::CSliceUnref(read_buf_);
  GPR_ASSERT(!read_buf_has_data_);
  read_buf_ = GRPC_SLICE_MALLOC(4192);
  if (connect_done_) {
    ContinueRegisterForOnReadableLocked();
  } else {
    GPR_ASSERT(pending_continue_register_for_on_readable_locked_ == false);
    pending_continue_register_for_on_readable_locked_ = true;
  }
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::
    ContinueRegisterForOnReadableLocked() {
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| ContinueRegisterForOnReadableLocked "
      "wsa_connect_error_:%d",
      GetName(), wsa_connect_error_);
  GPR_ASSERT(connect_done_);
  if (wsa_connect_error_ != 0) {
    ScheduleAndNullReadClosure(GRPC_WSA_ERROR(wsa_connect_error_, "connect"));
    return;
  }
  WSABUF buffer;
  buffer.buf = reinterpret_cast<char*>(GRPC_SLICE_START_PTR(read_buf_));
  buffer.len = GRPC_SLICE_LENGTH(read_buf_);
  memset(winsocket_->read_info()->overlapped(), 0, sizeof(OVERLAPPED));
  recv_from_source_addr_len_ = sizeof(recv_from_source_addr_);
  DWORD flags = 0;
  if (WSARecvFrom(winsocket_->raw_socket(), &buffer, 1, nullptr, &flags,
                  reinterpret_cast<sockaddr*>(recv_from_source_addr_),
                  &recv_from_source_addr_len_,
                  winsocket_->read_info()->overlapped(), nullptr)) {
    int wsa_last_error = WSAGetLastError();
    char* msg = gpr_format_message(wsa_last_error);
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "fd:|%s| RegisterForOnReadableLocked WSARecvFrom error code:|%d| "
        "msg:|%s|",
        GetName(), wsa_last_error, msg);
    gpr_free(msg);
    if (wsa_last_error != WSA_IO_PENDING) {
      ScheduleAndNullReadClosure(GRPC_WSA_ERROR(wsa_last_error, "WSARecvFrom"));
      return;
    }
  }
  winsocket_->NotifyOnRead(&outer_read_closure_);
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::
    RegisterForOnWriteableLocked(
        absl::AnyInvocable<void(absl::Status)> write_closure) {
  if (socket_type_ == SOCK_DGRAM) {
    GRPC_ARES_RESOLVER_TRACE_LOG("fd:|%s| RegisterForOnWriteableLocked called",
                                 GetName());
  } else {
    GPR_ASSERT(socket_type_ == SOCK_STREAM);
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "fd:|%s| RegisterForOnWriteableLocked called tcp_write_state_: %d "
        "connect_done_: %d",
        GetName(), tcp_write_state_, connect_done_);
  }
  GPR_ASSERT(write_closure_ == nullptr);
  write_closure_ = std::move(write_closure);
  if (!connect_done_) {
    GPR_ASSERT(!pending_continue_register_for_on_writeable_locked_);
    pending_continue_register_for_on_writeable_locked_ = true;
    // Register an async OnTcpConnect callback here rather than when the
    // connect was initiated, since we are now guaranteed to hold a ref of the
    // c-ares wrapper before write_closure_ is called.
    winsocket_->NotifyOnWrite(&on_tcp_connect_locked_);
  } else {
    ContinueRegisterForOnWriteableLocked();
  }
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::
    ContinueRegisterForOnWriteableLocked() {
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| ContinueRegisterForOnWriteableLocked "
      "wsa_connect_error_:%d",
      GetName(), wsa_connect_error_);
  GPR_ASSERT(connect_done_);
  if (wsa_connect_error_ != 0) {
    ScheduleAndNullWriteClosure(GRPC_WSA_ERROR(wsa_connect_error_, "connect"));
    return;
  }
  if (socket_type_ == SOCK_DGRAM) {
    ScheduleAndNullWriteClosure(absl::OkStatus());
  } else {
    GPR_ASSERT(socket_type_ == SOCK_STREAM);
    int wsa_error_code = 0;
    switch (tcp_write_state_) {
      case WRITE_IDLE:
        ScheduleAndNullWriteClosure(absl::OkStatus());
        break;
      case WRITE_REQUESTED:
        tcp_write_state_ = WRITE_PENDING;
        if (SendWriteBuf(nullptr, winsocket_->write_info()->overlapped(),
                         &wsa_error_code) != 0) {
          ScheduleAndNullWriteClosure(
              GRPC_WSA_ERROR(wsa_error_code, "WSASend (overlapped)"));
        } else {
          winsocket_->NotifyOnWrite(&outer_write_closure_);
        }
        break;
      case WRITE_PENDING:
      case WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY:
        abort();
    }
  }
}

bool GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::
    IsFdStillReadableLocked() {
  return read_buf_has_data_;
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::ShutdownLocked(
    absl::Status error) {
  GPR_ASSERT(!shutdown_called_);
  shutdown_called_ = true;
  on_shutdown_locked_();
  winsocket_->Shutdown(DEBUG_LOCATION, "GrpcPolledFdWindows::ShutdownLocked");
}

ares_socket_t
GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::GetWrappedAresSocketLocked() {
  return winsocket_->raw_socket();
}

const char* GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::GetName() const {
  return name_.c_str();
}

ares_ssize_t GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::RecvFrom(
    WSAErrorContext* wsa_error_ctx, void* data, ares_socket_t data_len,
    int /* flags */, struct sockaddr* from, ares_socklen_t* from_len) {
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| RecvFrom called read_buf_has_data:%d Current read buf "
      "length:|%d|",
      GetName(), read_buf_has_data_, GRPC_SLICE_LENGTH(read_buf_));
  if (!read_buf_has_data_) {
    wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
    return -1;
  }
  ares_ssize_t bytes_read = 0;
  for (size_t i = 0; i < GRPC_SLICE_LENGTH(read_buf_) && i < data_len; i++) {
    (static_cast<char*>(data))[i] = GRPC_SLICE_START_PTR(read_buf_)[i];
    bytes_read++;
  }
  read_buf_ = grpc_slice_sub_no_ref(read_buf_, bytes_read,
                                    GRPC_SLICE_LENGTH(read_buf_));
  if (GRPC_SLICE_LENGTH(read_buf_) == 0) {
    read_buf_has_data_ = false;
  }
  // c-ares overloads this recv_from virtual socket function to receive
  // data on both UDP and TCP sockets, and from is nullptr for TCP.
  if (from != nullptr) {
    GPR_ASSERT(*from_len <= recv_from_source_addr_len_);
    memcpy(from, &recv_from_source_addr_, recv_from_source_addr_len_);
    *from_len = recv_from_source_addr_len_;
  }
  return bytes_read;
}

int GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::SendWriteBuf(
    LPDWORD bytes_sent_ptr, LPWSAOVERLAPPED overlapped, int* wsa_error_code) {
  WSABUF buf;
  buf.len = GRPC_SLICE_LENGTH(write_buf_);
  buf.buf = reinterpret_cast<char*>(GRPC_SLICE_START_PTR(write_buf_));
  DWORD flags = 0;
  int out = WSASend(winsocket_->raw_socket(), &buf, 1, bytes_sent_ptr, flags,
                    overlapped, nullptr);
  *wsa_error_code = WSAGetLastError();
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| SendWriteBuf WSASend buf.len:%d *bytes_sent_ptr:%d "
      "overlapped:%p "
      "return:%d *wsa_error_code:%d",
      GetName(), buf.len, bytes_sent_ptr != nullptr ? *bytes_sent_ptr : 0,
      overlapped, out, *wsa_error_code);
  return out;
}

ares_ssize_t GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::SendV(
    WSAErrorContext* wsa_error_ctx, const struct iovec* iov, int iov_count) {
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| SendV called connect_done_:%d wsa_connect_error_:%d", GetName(),
      connect_done_, wsa_connect_error_);
  if (!connect_done_) {
    wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
    return -1;
  }
  if (wsa_connect_error_ != 0) {
    wsa_error_ctx->SetWSAError(wsa_connect_error_);
    return -1;
  }
  switch (socket_type_) {
    case SOCK_DGRAM:
      return SendVUDP(wsa_error_ctx, iov, iov_count);
    case SOCK_STREAM:
      return SendVTCP(wsa_error_ctx, iov, iov_count);
    default:
      abort();
  }
}

ares_ssize_t GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::SendVUDP(
    WSAErrorContext* wsa_error_ctx, const struct iovec* iov, int iov_count) {
  // c-ares doesn't handle retryable errors on writes of UDP sockets.
  // Therefore, the sendv handler for UDP sockets must only attempt
  // to write everything inline.
  GRPC_ARES_RESOLVER_TRACE_LOG("fd:|%s| SendVUDP called", GetName());
  GPR_ASSERT(GRPC_SLICE_LENGTH(write_buf_) == 0);
  grpc_core::CSliceUnref(write_buf_);
  write_buf_ = FlattenIovec(iov, iov_count);
  DWORD bytes_sent = 0;
  int wsa_error_code = 0;
  if (SendWriteBuf(&bytes_sent, nullptr, &wsa_error_code) != 0) {
    grpc_core::CSliceUnref(write_buf_);
    write_buf_ = grpc_empty_slice();
    wsa_error_ctx->SetWSAError(wsa_error_code);
    char* msg = gpr_format_message(wsa_error_code);
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "fd:|%s| SendVUDP SendWriteBuf error code:%d msg:|%s|", GetName(),
        wsa_error_code, msg);
    gpr_free(msg);
    return -1;
  }
  write_buf_ = grpc_slice_sub_no_ref(write_buf_, bytes_sent,
                                     GRPC_SLICE_LENGTH(write_buf_));
  return bytes_sent;
}

ares_ssize_t GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::SendVTCP(
    WSAErrorContext* wsa_error_ctx, const struct iovec* iov, int iov_count) {
  // The "sendv" handler on TCP sockets buffers up write
  // requests and returns an artificial WSAEWOULDBLOCK. Writing that buffer
  // out in the background, and making further send progress in general, will
  // happen as long as c-ares continues to show interest in writeability on
  // this fd.
  GRPC_ARES_RESOLVER_TRACE_LOG("fd:|%s| SendVTCP called tcp_write_state_:%d",
                               GetName(), tcp_write_state_);
  switch (tcp_write_state_) {
    case WRITE_IDLE:
      tcp_write_state_ = WRITE_REQUESTED;
      // GPR_ASSERT(GRPC_SLICE_LENGTH(write_buf_) == 0);
      grpc_core::CSliceUnref(write_buf_);
      write_buf_ = FlattenIovec(iov, iov_count);
      wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
      return -1;
    case WRITE_REQUESTED:
    case WRITE_PENDING:
      wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
      return -1;
    case WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY:
      // c-ares is retrying a send on data that we previously returned
      // WSAEWOULDBLOCK for, but then subsequently wrote out in the
      // background. Right now, we assume that c-ares is retrying the same
      // send again. If c-ares still needs to send even more data, we'll get
      // to it eventually.
      grpc_slice currently_attempted = FlattenIovec(iov, iov_count);
      GPR_ASSERT(GRPC_SLICE_LENGTH(currently_attempted) >=
                 GRPC_SLICE_LENGTH(write_buf_));
      ares_ssize_t total_sent = 0;
      for (size_t i = 0; i < GRPC_SLICE_LENGTH(write_buf_); i++) {
        GPR_ASSERT(GRPC_SLICE_START_PTR(currently_attempted)[i] ==
                   GRPC_SLICE_START_PTR(write_buf_)[i]);
        total_sent++;
      }
      grpc_core::CSliceUnref(currently_attempted);
      tcp_write_state_ = WRITE_IDLE;
      return total_sent;
  }
  abort();
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::OnTcpConnect() {
  grpc_core::MutexLock lock(mu_);
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:%s InnerOnTcpConnectLocked "
      "pending_register_for_readable:%d"
      " pending_register_for_writeable:%d",
      GetName(), pending_continue_register_for_on_readable_locked_,
      pending_continue_register_for_on_writeable_locked_);
  GPR_ASSERT(!connect_done_);
  connect_done_ = true;
  GPR_ASSERT(wsa_connect_error_ == 0);
  if (shutdown_called_) {
    wsa_connect_error_ = WSA_OPERATION_ABORTED;
  } else {
    DWORD transferred_bytes = 0;
    DWORD flags;
    BOOL wsa_success = WSAGetOverlappedResult(
        winsocket_->raw_socket(), winsocket_->write_info()->overlapped(),
        &transferred_bytes, FALSE, &flags);
    GPR_ASSERT(transferred_bytes == 0);
    if (!wsa_success) {
      wsa_connect_error_ = WSAGetLastError();
      char* msg = gpr_format_message(wsa_connect_error_);
      GRPC_ARES_RESOLVER_TRACE_LOG(
          "fd:%s InnerOnTcpConnectLocked WSA overlapped result code:%d "
          "msg:|%s|",
          GetName(), wsa_connect_error_, msg);
      gpr_free(msg);
    }
  }
  if (pending_continue_register_for_on_readable_locked_) {
    ContinueRegisterForOnReadableLocked();
  }
  if (pending_continue_register_for_on_writeable_locked_) {
    ContinueRegisterForOnWriteableLocked();
  }
}

int GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::Connect(
    WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
    ares_socklen_t target_len) {
  switch (socket_type_) {
    case SOCK_DGRAM:
      return ConnectUDP(wsa_error_ctx, target, target_len);
    case SOCK_STREAM:
      return ConnectTCP(wsa_error_ctx, target, target_len);
    default:
      abort();
  }
}

int GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::ConnectUDP(
    WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
    ares_socklen_t target_len) {
  GRPC_ARES_RESOLVER_TRACE_LOG("fd:%s ConnectUDP", GetName());
  GPR_ASSERT(!connect_done_);
  GPR_ASSERT(wsa_connect_error_ == 0);
  SOCKET s = winsocket_->raw_socket();
  int out =
      WSAConnect(s, target, target_len, nullptr, nullptr, nullptr, nullptr);
  wsa_connect_error_ = WSAGetLastError();
  wsa_error_ctx->SetWSAError(wsa_connect_error_);
  connect_done_ = true;
  char* msg = gpr_format_message(wsa_connect_error_);
  GRPC_ARES_RESOLVER_TRACE_LOG("fd:%s WSAConnect error code:|%d| msg:|%s|",
                               GetName(), wsa_connect_error_, msg);
  gpr_free(msg);
  // c-ares expects a posix-style connect API
  return out == 0 ? 0 : -1;
}

int GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::ConnectTCP(
    WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
    ares_socklen_t target_len) {
  GRPC_ARES_RESOLVER_TRACE_LOG("fd:%s ConnectTCP", GetName());
  LPFN_CONNECTEX ConnectEx;
  GUID guid = WSAID_CONNECTEX;
  DWORD ioctl_num_bytes;
  SOCKET s = winsocket_->raw_socket();
  if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &ConnectEx, sizeof(ConnectEx), &ioctl_num_bytes, nullptr,
               nullptr) != 0) {
    int wsa_last_error = WSAGetLastError();
    wsa_error_ctx->SetWSAError(wsa_last_error);
    char* msg = gpr_format_message(wsa_last_error);
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "fd:%s WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER) error code:%d "
        "msg:|%s|",
        GetName(), wsa_last_error, msg);
    gpr_free(msg);
    connect_done_ = true;
    wsa_connect_error_ = wsa_last_error;
    return -1;
  }
  grpc_resolved_address wildcard4_addr;
  grpc_resolved_address wildcard6_addr;
  grpc_sockaddr_make_wildcards(0, &wildcard4_addr, &wildcard6_addr);
  grpc_resolved_address* local_address = nullptr;
  if (address_family_ == AF_INET) {
    local_address = &wildcard4_addr;
  } else {
    local_address = &wildcard6_addr;
  }
  if (bind(s, reinterpret_cast<struct sockaddr*>(local_address->addr),
           static_cast<int>(local_address->len)) != 0) {
    int wsa_last_error = WSAGetLastError();
    wsa_error_ctx->SetWSAError(wsa_last_error);
    char* msg = gpr_format_message(wsa_last_error);
    GRPC_ARES_RESOLVER_TRACE_LOG("fd:%s bind error code:%d msg:|%s|", GetName(),
                                 wsa_last_error, msg);
    gpr_free(msg);
    connect_done_ = true;
    wsa_connect_error_ = wsa_last_error;
    return -1;
  }
  int out = 0;
  if (ConnectEx(s, target, target_len, nullptr, 0, nullptr,
                winsocket_->write_info()->overlapped()) == 0) {
    out = -1;
    int wsa_last_error = WSAGetLastError();
    wsa_error_ctx->SetWSAError(wsa_last_error);
    char* msg = gpr_format_message(wsa_last_error);
    GRPC_ARES_RESOLVER_TRACE_LOG("fd:%s ConnectEx error code:%d msg:|%s|",
                                 GetName(), wsa_last_error, msg);
    gpr_free(msg);
    if (wsa_last_error == WSA_IO_PENDING) {
      // c-ares only understands WSAEINPROGRESS and EWOULDBLOCK error codes on
      // connect, but an async connect on IOCP socket will give
      // WSA_IO_PENDING, so we need to convert.
      wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
    } else {
      // By returning a non-retryable error to c-ares at this point,
      // we're aborting the possibility of any future operations on this fd.
      connect_done_ = true;
      wsa_connect_error_ = wsa_last_error;
      return -1;
    }
  }
  // RegisterForOnWriteable will register for an async notification
  return out;
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::OnIocpReadable() {
  grpc_core::MutexLock lock(mu_);
  absl::Status error;
  if (winsocket_->read_info()->result().wsa_error != 0) {
    // WSAEMSGSIZE would be due to receiving more data
    // than our read buffer's fixed capacity. Assume that
    // the connection is TCP and read the leftovers
    // in subsequent c-ares reads.
    if (winsocket_->read_info()->result().wsa_error != WSAEMSGSIZE) {
      error = GRPC_WSA_ERROR(winsocket_->read_info()->result().wsa_error,
                             "OnIocpReadableInner");
      GRPC_ARES_RESOLVER_TRACE_LOG(
          "fd:|%s| OnIocpReadableInner winsocket_->read_info.wsa_error "
          "code:|%d| msg:|%s|",
          GetName(), winsocket_->read_info()->result().wsa_error,
          grpc_core::StatusToString(error).c_str());
    }
  }
  if (error.ok()) {
    read_buf_ = grpc_slice_sub_no_ref(
        read_buf_, 0, winsocket_->read_info()->result().bytes_transferred);
    read_buf_has_data_ = true;
  } else {
    grpc_core::CSliceUnref(read_buf_);
    read_buf_ = grpc_empty_slice();
  }
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| OnIocpReadable finishing. read buf length now:|%d|", GetName(),
      GRPC_SLICE_LENGTH(read_buf_));
  ScheduleAndNullReadClosure(error);
}

void GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::OnIocpWriteable() {
  grpc_core::MutexLock lock(mu_);
  GRPC_ARES_RESOLVER_TRACE_LOG("OnIocpWriteableInner. fd:|%s|", GetName());
  GPR_ASSERT(socket_type_ == SOCK_STREAM);
  absl::Status error;
  if (winsocket_->write_info()->result().wsa_error != 0) {
    error = GRPC_WSA_ERROR(winsocket_->write_info()->result().wsa_error,
                           "OnIocpWriteableInner");
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "fd:|%s| OnIocpWriteableInner. winsocket_->write_info.wsa_error "
        "code:|%d| msg:|%s|",
        GetName(), winsocket_->write_info()->result().wsa_error,
        grpc_core::StatusToString(error).c_str());
  }
  GPR_ASSERT(tcp_write_state_ == WRITE_PENDING);
  if (error.ok()) {
    tcp_write_state_ = WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY;
    write_buf_ = grpc_slice_sub_no_ref(
        write_buf_, 0, winsocket_->write_info()->result().bytes_transferred);
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "fd:|%s| OnIocpWriteableInner. bytes transferred:%d", GetName(),
        winsocket_->write_info()->result().bytes_transferred);
  } else {
    grpc_core::CSliceUnref(write_buf_);
    write_buf_ = grpc_empty_slice();
  }
  ScheduleAndNullWriteClosure(error);
}

grpc_slice GrpcPolledFdFactoryWindows::GrpcPolledFdWindows::FlattenIovec(
    const struct iovec* iov, int iov_count) {
  int total = 0;
  for (int i = 0; i < iov_count; i++) {
    total += iov[i].iov_len;
  }
  grpc_slice out = GRPC_SLICE_MALLOC(total);
  size_t cur = 0;
  for (int i = 0; i < iov_count; i++) {
    for (size_t k = 0; k < iov[i].iov_len; k++) {
      GRPC_SLICE_START_PTR(out)
      [cur++] = (static_cast<char*>(iov[i].iov_base))[k];
    }
  }
  return out;
}

GrpcPolledFdFactoryWindows::GrpcPolledFdFactoryWindows(IOCP* iocp)
    : iocp_(iocp) {}

GrpcPolledFdFactoryWindows::~GrpcPolledFdFactoryWindows() {
  // We might still have a socket -> polled fd mappings if the socket
  // was never seen by the grpc ares wrapper code, i.e. if we never
  // initiated I/O polling for them.
  for (auto& it : sockets_) {
    delete it.second;
  }
}

void GrpcPolledFdFactoryWindows::Initialize(grpc_core::Mutex* mutex,
                                            EventEngine* event_engine) {
  mu_ = mutex;
  event_engine_ = event_engine;
}

GrpcPolledFd* GrpcPolledFdFactoryWindows::NewGrpcPolledFdLocked(
    ares_socket_t as) {
  auto it = sockets_.find(as);
  GPR_ASSERT(it != sockets_.end());
  return it->second;
}

void GrpcPolledFdFactoryWindows::ConfigureAresChannelLocked(
    ares_channel channel) {
  static const struct ares_socket_functions kCustomSockFuncs = {
      &GrpcPolledFdFactoryWindows::Socket /* socket */,
      &GrpcPolledFdFactoryWindows::CloseSocket /* close */,
      &GrpcPolledFdFactoryWindows::Connect /* connect */,
      &GrpcPolledFdFactoryWindows::RecvFrom /* recvfrom */,
      &GrpcPolledFdFactoryWindows::SendV /* sendv */,
  };
  ares_set_socket_functions(channel, &kCustomSockFuncs, this);
}

ares_socket_t GrpcPolledFdFactoryWindows::Socket(int af, int type, int protocol,
                                                 void* user_data) {
  if (type != SOCK_DGRAM && type != SOCK_STREAM) {
    GRPC_ARES_RESOLVER_TRACE_LOG("Socket called with invalid socket type:%d",
                                 type);
    return INVALID_SOCKET;
  }
  GrpcPolledFdFactoryWindows* self =
      static_cast<GrpcPolledFdFactoryWindows*>(user_data);
  SOCKET s =
      WSASocket(af, type, protocol, nullptr, 0, IOCP::GetDefaultSocketFlags());
  if (s == INVALID_SOCKET) {
    GRPC_ARES_RESOLVER_TRACE_LOG(
        "WSASocket failed with params af:%d type:%d protocol:%d", af, type,
        protocol);
    return s;
  }
  if (type == SOCK_STREAM) {
    absl::Status error = PrepareSocket(s);
    if (!error.ok()) {
      GRPC_ARES_RESOLVER_TRACE_LOG("WSAIoctl failed with error: %s",
                                   grpc_core::StatusToString(error).c_str());
      return INVALID_SOCKET;
    }
  }
  auto on_shutdown_locked = [self, s]() {
    // grpc_winsocket_shutdown calls closesocket which invalidates our
    // socket -> polled_fd mapping because the socket handle can be henceforth
    // reused.
    self->sockets_.erase(s);
  };
  auto polled_fd = new GrpcPolledFdWindows(self->iocp_->Watch(s), self->mu_, af,
                                           type, std::move(on_shutdown_locked),
                                           self->event_engine_);
  GRPC_ARES_RESOLVER_TRACE_LOG(
      "fd:|%s| created with params af:%d type:%d protocol:%d",
      polled_fd->GetName(), af, type, protocol);
  GPR_ASSERT(self->sockets_.insert({s, polled_fd}).second);
  return s;
}

int GrpcPolledFdFactoryWindows::Connect(ares_socket_t as,
                                        const struct sockaddr* target,
                                        ares_socklen_t target_len,
                                        void* user_data) {
  WSAErrorContext wsa_error_ctx;
  GrpcPolledFdFactoryWindows* self =
      static_cast<GrpcPolledFdFactoryWindows*>(user_data);
  auto it = self->sockets_.find(as);
  GPR_ASSERT(it != self->sockets_.end());
  return it->second->Connect(&wsa_error_ctx, target, target_len);
}

ares_ssize_t GrpcPolledFdFactoryWindows::SendV(ares_socket_t as,
                                               const struct iovec* iov,
                                               int iovec_count,
                                               void* user_data) {
  WSAErrorContext wsa_error_ctx;
  GrpcPolledFdFactoryWindows* self =
      static_cast<GrpcPolledFdFactoryWindows*>(user_data);
  auto it = self->sockets_.find(as);
  GPR_ASSERT(it != self->sockets_.end());
  return it->second->SendV(&wsa_error_ctx, iov, iovec_count);
}

ares_ssize_t GrpcPolledFdFactoryWindows::RecvFrom(ares_socket_t as, void* data,
                                                  size_t data_len, int flags,
                                                  struct sockaddr* from,
                                                  ares_socklen_t* from_len,
                                                  void* user_data) {
  WSAErrorContext wsa_error_ctx;
  GrpcPolledFdFactoryWindows* self =
      static_cast<GrpcPolledFdFactoryWindows*>(user_data);
  auto it = self->sockets_.find(as);
  GPR_ASSERT(it != self->sockets_.end());
  return it->second->RecvFrom(&wsa_error_ctx, data, data_len, flags, from,
                              from_len);
}

int GrpcPolledFdFactoryWindows::CloseSocket(SOCKET s, void* /* user_data */) {
  GRPC_ARES_RESOLVER_TRACE_LOG("c-ares socket: %d CloseSocket", s);
  return 0;
}

}  // namespace experimental
}  // namespace grpc_event_engine
