//
//
// Copyright 2016 gRPC authors.
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
//
//
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"  // IWYU pragma: keep
#if GRPC_ARES == 1 && defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)

#include <string.h>

#include <functional>
#include <map>
#include <memory>
#include <unordered_set>

#include <ares.h>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"

// TODO(apolcyn): remove this hack after fixing upstream.
// Our grpc/c-ares code on Windows uses the ares_set_socket_functions API,
// which uses "struct iovec" type, which on Windows is defined inside of
// a c-ares header that is not public.
// See https://github.com/c-ares/c-ares/issues/206.
struct iovec {
  void* iov_base;
  size_t iov_len;
};

namespace grpc_core {

namespace {

// c-ares reads and takes action on the error codes of the
// "virtual socket operations" in this file, via the WSAGetLastError
// APIs. If code in this file wants to set a specific WSA error that
// c-ares should read, it must do so by calling SetWSAError() on the
// WSAErrorContext instance passed to it. A WSAErrorContext must only be
// instantiated at the top of the virtual socket function callstack.
class WSAErrorContext {
 public:
  explicit WSAErrorContext(){};

  ~WSAErrorContext() {
    if (error_ != 0) {
      WSASetLastError(error_);
    }
  }

  // Disallow copy and assignment operators
  WSAErrorContext(const WSAErrorContext&) = delete;
  WSAErrorContext& operator=(const WSAErrorContext&) = delete;

  void SetWSAError(int error) { error_ = error; }

 private:
  int error_ = 0;
};

// c-ares creates its own sockets and is meant to read them when readable and
// write them when writeable. To fit this socket usage model into the grpc
// windows poller (which gives notifications when attempted reads and writes are
// actually fulfilled rather than possible), this GrpcPolledFdWindows class
// takes advantage of the ares_set_socket_functions API and acts as a virtual
// socket. It holds its own read and write buffers which are written to and read
// from c-ares and are used with the grpc windows poller, and it, e.g.,
// manufactures virtual socket error codes when it e.g. needs to tell the c-ares
// library to wait for an async read.
class GrpcPolledFdWindows {
 public:
  enum ConnectState {
    STARTING,
    AWAITING_ASYNC_NOTIFICATION,
    DONE,
  };

  GrpcPolledFdWindows(ares_socket_t as, Mutex* mu, int address_family,
                      int socket_type, std::function<void()> on_shutdown_locked)
      : mu_(mu),
        read_buf_(grpc_empty_slice()),
        name_(absl::StrFormat("c-ares socket: %" PRIdPTR, as)),
        address_family_(address_family),
        socket_type_(socket_type),
        on_shutdown_locked_(std::move(on_shutdown_locked)) {
    // Closure Initialization
    GRPC_CLOSURE_INIT(&outer_read_closure_,
                      &GrpcPolledFdWindows::OnIocpReadable, this,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_tcp_connect_locked_,
                      &GrpcPolledFdWindows::OnTcpConnect, this,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_schedule_write_closure_after_delay_,
                      &GrpcPolledFdWindows::OnScheduleWriteClosureAfterDelay,
                      this, grpc_schedule_on_exec_ctx);
    winsocket_ = grpc_winsocket_create(as, name_.c_str());
  }

  ~GrpcPolledFdWindows() {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| ~GrpcPolledFdWindows shutdown_called_: %d ", GetName(),
        shutdown_called_);
    CSliceUnref(read_buf_);
    GPR_ASSERT(read_closure_ == nullptr);
    GPR_ASSERT(write_closure_ == nullptr);
    GPR_ASSERT(!have_schedule_write_closure_after_delay_);
    if (!shutdown_called_) {
      // This can happen if the socket was never seen by grpc ares wrapper
      // code, i.e. if we never started I/O polling on it.
      grpc_winsocket_shutdown(winsocket_);
    }
    grpc_winsocket_destroy(winsocket_);
  }

  void ScheduleAndNullReadClosure(grpc_error_handle error) {
    ExecCtx::Run(DEBUG_LOCATION, read_closure_, error);
    read_closure_ = nullptr;
  }

  void ScheduleAndNullWriteClosure(grpc_error_handle error) {
    ExecCtx::Run(DEBUG_LOCATION, write_closure_, error);
    write_closure_ = nullptr;
  }

  void RegisterForOnReadableLocked(grpc_closure* read_closure) {
    GPR_ASSERT(read_closure_ == nullptr);
    read_closure_ = read_closure;
    GPR_ASSERT(GRPC_SLICE_LENGTH(read_buf_) == 0);
    CSliceUnref(read_buf_);
    GPR_ASSERT(!read_buf_has_data_);
    read_buf_ = GRPC_SLICE_MALLOC(4192);
    if (connect_state_ == DONE) {
      ContinueRegisterForOnReadableLocked();
    } else {
      GPR_ASSERT(pending_continue_register_for_on_readable_locked_ == false);
      pending_continue_register_for_on_readable_locked_ = true;
    }
  }

  void ContinueRegisterForOnReadableLocked() {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| ContinueRegisterForOnReadableLocked "
        "wsa_connect_error_:%d",
        GetName(), wsa_connect_error_);
    GPR_ASSERT(connect_state_ == DONE);
    if (wsa_connect_error_ != 0) {
      ScheduleAndNullReadClosure(GRPC_WSA_ERROR(wsa_connect_error_, "connect"));
      return;
    }
    WSABUF buffer;
    buffer.buf = (char*)GRPC_SLICE_START_PTR(read_buf_);
    buffer.len = GRPC_SLICE_LENGTH(read_buf_);
    memset(&winsocket_->read_info.overlapped, 0, sizeof(OVERLAPPED));
    recv_from_source_addr_len_ = sizeof(recv_from_source_addr_);
    DWORD flags = 0;
    if (WSARecvFrom(grpc_winsocket_wrapped_socket(winsocket_), &buffer, 1,
                    nullptr, &flags, (sockaddr*)recv_from_source_addr_,
                    &recv_from_source_addr_len_,
                    &winsocket_->read_info.overlapped, nullptr)) {
      int wsa_last_error = WSAGetLastError();
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_CARES_TRACE_LOG(
          "fd:|%s| RegisterForOnReadableLocked WSARecvFrom error code:|%d| "
          "msg:|%s|",
          GetName(), wsa_last_error, msg);
      gpr_free(msg);
      if (wsa_last_error != WSA_IO_PENDING) {
        ScheduleAndNullReadClosure(
            GRPC_WSA_ERROR(wsa_last_error, "WSARecvFrom"));
        return;
      }
    }
    grpc_socket_notify_on_read(winsocket_, &outer_read_closure_);
  }

  void RegisterForOnWriteableLocked(grpc_closure* write_closure) {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| RegisterForOnWriteableLocked called connect_state_: %d "
        "last_wsa_send_result_: %d",
        GetName(), connect_state_, last_wsa_send_result_);
    GPR_ASSERT(write_closure_ == nullptr);
    write_closure_ = write_closure;
    if (connect_state_ == STARTING) {
      connect_state_ = AWAITING_ASYNC_NOTIFICATION;
      GPR_ASSERT(!pending_continue_register_for_on_writeable_locked_);
      pending_continue_register_for_on_writeable_locked_ = true;
      grpc_socket_notify_on_write(winsocket_, &on_tcp_connect_locked_);
    } else if (connect_state_ == DONE) {
      ContinueRegisterForOnWriteableLocked();
    } else {
      GPR_ASSERT(0);
    }
  }

  static void OnScheduleWriteClosureAfterDelay(void* arg,
                                               grpc_error_handle /*error*/) {
    GrpcPolledFdWindows* self = static_cast<GrpcPolledFdWindows*>(arg);
    MutexLock lock(self->mu_);
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| OnScheduleWriteClosureAfterDelay"
        "last_wsa_send_result_:%d",
        self->GetName(), self->last_wsa_send_result_);
    self->have_schedule_write_closure_after_delay_ = false;
    self->ScheduleAndNullWriteClosure(absl::OkStatus());
  }

  void ContinueRegisterForOnWriteableLocked() {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| ContinueRegisterForOnWriteableLocked "
        "wsa_connect_error_:%d last_wsa_send_result_:%d",
        GetName(), wsa_connect_error_, last_wsa_send_result_);
    GPR_ASSERT(connect_state_ == DONE);
    if (wsa_connect_error_ != 0) {
      ScheduleAndNullWriteClosure(
          GRPC_WSA_ERROR(wsa_connect_error_, "connect"));
      return;
    }
    if (last_wsa_send_result_ == 0) {
      ScheduleAndNullWriteClosure(absl::OkStatus());
    } else {
      // If the last write attempt on this socket failed, that means either of
      // two things: 1) C-ares considers the error non-retryable:
      //      In this case, c-ares will not try to use this socket anymore and
      //      close it etc.
      // 2) C-ares considers the error retryable (e.g. WSAEWOULDBLOCK on a TCP
      // socket):
      //      In this case, we simply spoof a "writable" notification 1 second
      //      from now. c-ares will retry a synchronous / non-blocking write in
      //      the subsequent call to ares_process_fd. Note that ideally, we'd
      //      use an async WSA send operation in this case, but the machinery
      //      involved in that would be much more complex and is probably not
      //      worth having. Instead just take a busy-poll approach on the write,
      //      but pace ourselves to not burn CPU.
      GPR_ASSERT(!have_schedule_write_closure_after_delay_);
      have_schedule_write_closure_after_delay_ = true;
      grpc_timer_init(&schedule_write_closure_after_delay_,
                      Timestamp::Now() + Duration::Seconds(1),
                      &on_schedule_write_closure_after_delay_);
    }
  }

  bool IsFdStillReadableLocked() { return read_buf_has_data_; }

  void ShutdownLocked(grpc_error_handle /* error */) {
    GPR_ASSERT(!shutdown_called_);
    shutdown_called_ = true;
    if (have_schedule_write_closure_after_delay_) {
      grpc_timer_cancel(&schedule_write_closure_after_delay_);
    }
    on_shutdown_locked_();
    grpc_winsocket_shutdown(winsocket_);
  }

  ares_socket_t GetWrappedAresSocketLocked() {
    return grpc_winsocket_wrapped_socket(winsocket_);
  }

  const char* GetName() const { return name_.c_str(); }

  ares_ssize_t RecvFrom(WSAErrorContext* wsa_error_ctx, void* data,
                        ares_socket_t data_len, int /* flags */,
                        struct sockaddr* from, ares_socklen_t* from_len) {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| RecvFrom called read_buf_has_data:%d Current read buf "
        "length:|%d|",
        GetName(), read_buf_has_data_, GRPC_SLICE_LENGTH(read_buf_));
    if (!read_buf_has_data_) {
      wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
      return -1;
    }
    ares_ssize_t bytes_read = 0;
    for (size_t i = 0; i < GRPC_SLICE_LENGTH(read_buf_) && i < data_len; i++) {
      ((char*)data)[i] = GRPC_SLICE_START_PTR(read_buf_)[i];
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

  grpc_slice FlattenIovec(const struct iovec* iov, int iov_count) {
    int total = 0;
    for (int i = 0; i < iov_count; i++) {
      total += iov[i].iov_len;
    }
    grpc_slice out = GRPC_SLICE_MALLOC(total);
    size_t cur = 0;
    for (int i = 0; i < iov_count; i++) {
      for (size_t k = 0; k < iov[i].iov_len; k++) {
        GRPC_SLICE_START_PTR(out)[cur++] = ((char*)iov[i].iov_base)[k];
      }
    }
    return out;
  }

  int SendWriteBuf(grpc_slice write_buf, LPDWORD bytes_sent_ptr,
                   int* wsa_error_code) {
    WSABUF buf;
    buf.len = GRPC_SLICE_LENGTH(write_buf);
    buf.buf = (char*)GRPC_SLICE_START_PTR(write_buf);
    DWORD flags = 0;
    int ret = WSASend(grpc_winsocket_wrapped_socket(winsocket_), &buf, 1,
                      bytes_sent_ptr, flags, nullptr, nullptr);
    *wsa_error_code = WSAGetLastError();
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| SendWriteBuf WSASend buf.len:%d *bytes_sent_ptr:%d "
        "return:%d *wsa_error_code:%d",
        GetName(), buf.len, bytes_sent_ptr != nullptr ? *bytes_sent_ptr : 0,
        last_wsa_send_result_, *wsa_error_code);
    return ret;
  }

  ares_ssize_t SendV(WSAErrorContext* wsa_error_ctx, const struct iovec* iov,
                     int iov_count) {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| SendV called connect_state_:%d wsa_connect_error_:%d",
        GetName(), connect_state_, wsa_connect_error_);
    if (connect_state_ != DONE) {
      wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
      return -1;
    }
    if (wsa_connect_error_ != 0) {
      wsa_error_ctx->SetWSAError(wsa_connect_error_);
      return -1;
    }
    grpc_slice write_buf = FlattenIovec(iov, iov_count);
    DWORD bytes_sent = 0;
    int wsa_error_code = 0;
    last_wsa_send_result_ =
        SendWriteBuf(write_buf, &bytes_sent, &wsa_error_code);
    CSliceUnref(write_buf);
    if (last_wsa_send_result_ != 0) {
      wsa_error_ctx->SetWSAError(wsa_error_code);
      char* msg = gpr_format_message(wsa_error_code);
      GRPC_CARES_TRACE_LOG("fd:|%s| SendV SendWriteBuf error code:%d msg:|%s|",
                           GetName(), wsa_error_code, msg);
      gpr_free(msg);
      return -1;
    }
    return bytes_sent;
  }

  static void OnTcpConnect(void* arg, grpc_error_handle error) {
    GrpcPolledFdWindows* grpc_polled_fd =
        static_cast<GrpcPolledFdWindows*>(arg);
    MutexLock lock(grpc_polled_fd->mu_);
    grpc_polled_fd->OnTcpConnectLocked(error);
  }

  void OnTcpConnectLocked(grpc_error_handle error) {
    GRPC_CARES_TRACE_LOG(
        "fd:%s InnerOnTcpConnectLocked error:|%s| "
        "pending_register_for_readable:%d"
        " pending_register_for_writeable:%d",
        GetName(), StatusToString(error).c_str(),
        pending_continue_register_for_on_readable_locked_,
        pending_continue_register_for_on_writeable_locked_);
    GPR_ASSERT(connect_state_ == AWAITING_ASYNC_NOTIFICATION);
    connect_state_ = DONE;
    GPR_ASSERT(wsa_connect_error_ == 0);
    if (error.ok()) {
      DWORD transferred_bytes = 0;
      DWORD flags;
      BOOL wsa_success =
          WSAGetOverlappedResult(grpc_winsocket_wrapped_socket(winsocket_),
                                 &winsocket_->write_info.overlapped,
                                 &transferred_bytes, FALSE, &flags);
      GPR_ASSERT(transferred_bytes == 0);
      if (!wsa_success) {
        wsa_connect_error_ = WSAGetLastError();
        char* msg = gpr_format_message(wsa_connect_error_);
        GRPC_CARES_TRACE_LOG(
            "fd:%s InnerOnTcpConnectLocked WSA overlapped result code:%d "
            "msg:|%s|",
            GetName(), wsa_connect_error_, msg);
        gpr_free(msg);
      }
    } else {
      // Spoof up an error code that will cause any future c-ares operations on
      // this fd to abort.
      wsa_connect_error_ = WSA_OPERATION_ABORTED;
    }
    if (pending_continue_register_for_on_readable_locked_) {
      ContinueRegisterForOnReadableLocked();
    }
    if (pending_continue_register_for_on_writeable_locked_) {
      ContinueRegisterForOnWriteableLocked();
    }
  }

  int Connect(WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
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

  int ConnectUDP(WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
                 ares_socklen_t target_len) {
    GRPC_CARES_TRACE_LOG("fd:%s ConnectUDP", GetName());
    GPR_ASSERT(connect_state_ == STARTING);
    GPR_ASSERT(wsa_connect_error_ == 0);
    SOCKET s = grpc_winsocket_wrapped_socket(winsocket_);
    int out =
        WSAConnect(s, target, target_len, nullptr, nullptr, nullptr, nullptr);
    wsa_connect_error_ = WSAGetLastError();
    wsa_error_ctx->SetWSAError(wsa_connect_error_);
    connect_state_ = DONE;
    char* msg = gpr_format_message(wsa_connect_error_);
    GRPC_CARES_TRACE_LOG("fd:%s WSAConnect error code:|%d| msg:|%s|", GetName(),
                         wsa_connect_error_, msg);
    gpr_free(msg);
    // c-ares expects a posix-style connect API
    return out == 0 ? 0 : -1;
  }

  int ConnectTCP(WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
                 ares_socklen_t target_len) {
    GRPC_CARES_TRACE_LOG("fd:%s ConnectTCP", GetName());
    LPFN_CONNECTEX ConnectEx;
    GUID guid = WSAID_CONNECTEX;
    DWORD ioctl_num_bytes;
    SOCKET s = grpc_winsocket_wrapped_socket(winsocket_);
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                 &ConnectEx, sizeof(ConnectEx), &ioctl_num_bytes, nullptr,
                 nullptr) != 0) {
      int wsa_last_error = WSAGetLastError();
      wsa_error_ctx->SetWSAError(wsa_last_error);
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_CARES_TRACE_LOG(
          "fd:%s WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER) error code:%d "
          "msg:|%s|",
          GetName(), wsa_last_error, msg);
      gpr_free(msg);
      connect_state_ = DONE;
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
    if (bind(s, (struct sockaddr*)local_address->addr,
             (int)local_address->len) != 0) {
      int wsa_last_error = WSAGetLastError();
      wsa_error_ctx->SetWSAError(wsa_last_error);
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_CARES_TRACE_LOG("fd:%s bind error code:%d msg:|%s|", GetName(),
                           wsa_last_error, msg);
      gpr_free(msg);
      connect_state_ = DONE;
      wsa_connect_error_ = wsa_last_error;
      return -1;
    }
    int out = 0;
    if (ConnectEx(s, target, target_len, nullptr, 0, nullptr,
                  &winsocket_->write_info.overlapped) == 0) {
      out = -1;
      int wsa_last_error = WSAGetLastError();
      wsa_error_ctx->SetWSAError(wsa_last_error);
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_CARES_TRACE_LOG("fd:%s ConnectEx error code:%d msg:|%s|", GetName(),
                           wsa_last_error, msg);
      gpr_free(msg);
      if (wsa_last_error == WSA_IO_PENDING) {
        // c-ares only understands WSAEINPROGRESS and EWOULDBLOCK error codes on
        // connect, but an async connect on IOCP socket will give
        // WSA_IO_PENDING, so we need to convert.
        wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
      } else {
        // By returning a non-retryable error to c-ares at this point,
        // we're aborting the possibility of any future operations on this fd.
        connect_state_ = DONE;
        wsa_connect_error_ = wsa_last_error;
        return -1;
      }
    }
    GPR_ASSERT(!need_async_connect_notification_);
    need_async_connect_notification_ = true;
    return out;
  }

  static void OnIocpReadable(void* arg, grpc_error_handle error) {
    GrpcPolledFdWindows* polled_fd = static_cast<GrpcPolledFdWindows*>(arg);
    MutexLock lock(polled_fd->mu_);
    polled_fd->OnIocpReadableLocked(error);
  }

  // TODO(apolcyn): improve this error handling to be less conversative.
  // An e.g. ECONNRESET error here should result in errors when
  // c-ares reads from this socket later, but it shouldn't necessarily cancel
  // the entire resolution attempt. Doing so will allow the "inject broken
  // nameserver list" test to pass on Windows.
  void OnIocpReadableLocked(grpc_error_handle error) {
    if (error.ok()) {
      if (winsocket_->read_info.wsa_error != 0) {
        // WSAEMSGSIZE would be due to receiving more data
        // than our read buffer's fixed capacity. Assume that
        // the connection is TCP and read the leftovers
        // in subsequent c-ares reads.
        if (winsocket_->read_info.wsa_error != WSAEMSGSIZE) {
          error = GRPC_WSA_ERROR(winsocket_->read_info.wsa_error,
                                 "OnIocpReadableInner");
          GRPC_CARES_TRACE_LOG(
              "fd:|%s| OnIocpReadableInner winsocket_->read_info.wsa_error "
              "code:|%d| msg:|%s|",
              GetName(), winsocket_->read_info.wsa_error,
              StatusToString(error).c_str());
        }
      }
    }
    if (error.ok()) {
      read_buf_ = grpc_slice_sub_no_ref(
          read_buf_, 0, winsocket_->read_info.bytes_transferred);
      read_buf_has_data_ = true;
    } else {
      CSliceUnref(read_buf_);
      read_buf_ = grpc_empty_slice();
    }
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| OnIocpReadable finishing. read buf length now:|%d|", GetName(),
        GRPC_SLICE_LENGTH(read_buf_));
    ScheduleAndNullReadClosure(error);
  }

 private:
  Mutex* mu_;
  char recv_from_source_addr_[200];
  ares_socklen_t recv_from_source_addr_len_;
  grpc_slice read_buf_;
  bool read_buf_has_data_ = false;
  grpc_closure* read_closure_ = nullptr;
  grpc_closure* write_closure_ = nullptr;
  grpc_closure outer_read_closure_;
  grpc_winsocket* winsocket_;
  int last_wsa_send_result_ = 0;
  grpc_timer schedule_write_closure_after_delay_;
  grpc_closure on_schedule_write_closure_after_delay_;
  bool have_schedule_write_closure_after_delay_ = false;
  const std::string name_;
  bool shutdown_called_ = false;
  int address_family_;
  int socket_type_;
  // State related to TCP connection setup:
  grpc_closure on_tcp_connect_locked_;
  ConnectState connect_state_ = STARTING;
  int wsa_connect_error_ = 0;
  // We don't run register_for_{readable,writeable} logic until
  // a socket is connected. In the interim, we queue readable/writeable
  // registrations with the following state.
  bool pending_continue_register_for_on_readable_locked_ = false;
  bool pending_continue_register_for_on_writeable_locked_ = false;
  std::function<void()> on_shutdown_locked_;
};

class GrpcPolledFdFactoryWindows : public GrpcPolledFdFactory {
 public:
  explicit GrpcPolledFdFactoryWindows(Mutex* mu) : mu_(mu) {}

  ~GrpcPolledFdFactoryWindows() override {
    // We might still have a socket -> polled fd mappings if the socket
    // was never seen by the grpc ares wrapper code, i.e. if we never
    // initiated I/O polling for them.
    for (auto& it : sockets_) {
      delete it->second;
    }
  }

  GrpcPolledFd* NewGrpcPolledFdLocked(
      ares_socket_t as, grpc_pollset_set* /* driver_pollset_set */) override {
    auto it = sockets_.find(as);
    GPR_ASSERT(it != sockets_.end());
    return it->second;
  }

  void ConfigureAresChannelLocked(ares_channel channel) override {
    ares_set_socket_functions(channel, &kCustomSockFuncs, this);
  }

 private:
  // These virtual socket functions are called from within the c-ares
  // library. These methods generally dispatch those socket calls to the
  // appropriate methods. The virtual "socket" and "close" methods are
  // special and instead create/add and remove/destroy GrpcPolledFdWindows
  // objects.
  //
  static ares_socket_t Socket(int af, int type, int protocol, void* user_data) {
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
      GRPC_CARES_TRACE_LOG("Socket called with invalid socket type:%d", type);
      return INVALID_SOCKET;
    }
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    SOCKET s = WSASocket(af, type, protocol, nullptr, 0,
                         grpc_get_default_wsa_socket_flags());
    if (s == INVALID_SOCKET) {
      GRPC_CARES_TRACE_LOG(
          "WSASocket failed with params af:%d type:%d protocol:%d", af, type,
          protocol);
      return s;
    }
    grpc_error_handle error = grpc_tcp_set_non_block(s);
    if (!error.ok()) {
      GRPC_CARES_TRACE_LOG("WSAIoctl failed with error: %s",
                           StatusToString(error).c_str());
      return INVALID_SOCKET;
    }
    auto on_shutdown_locked = [self, s]() {
      // grpc_winsocket_shutdown calls closesocket which invalidates our
      // socket -> polled_fd mapping because the socket handle can be henceforth
      // reused.
      self->sockets_.erase(s);
    };
    auto polled_fd = new GrpcPolledFdWindows >
                     (s, self->mu_, af, type, std::move(on_shutdown_locked));
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| created with params af:%d type:%d protocol:%d",
        polled_fd->GetName(), af, type, protocol);
    auto insert_result = self->sockets_.insert({s, polled_fd});
    GPR_ASSERT(insert_result.second);
    return s;
  }

  static int Connect(ares_socket_t as, const struct sockaddr* target,
                     ares_socklen_t target_len, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    auto it = self->sockets_.find(as);
    GPR_ASSERT(it != self->sockets_.end());
    return it->second->Connect(&wsa_error_ctx, target, target_len);
  }

  static ares_ssize_t SendV(ares_socket_t as, const struct iovec* iov,
                            int iovec_count, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    auto it = self->sockets_.find(as);
    GPR_ASSERT(it != self->sockets_.end());
    return it->second->SendV(&wsa_error_ctx, iov, iovec_count);
  }

  static ares_ssize_t RecvFrom(ares_socket_t as, void* data, size_t data_len,
                               int flags, struct sockaddr* from,
                               ares_socklen_t* from_len, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    auto it = self->sockets_.find(as);
    GPR_ASSERT(it != self->sockets_.end());
    return it->second->RecvFrom(&wsa_error_ctx, data, data_len, flags, from,
                                from_len);
  }

  static int CloseSocket(SOCKET s, void* user_data) { return 0; }

  const struct ares_socket_functions kCustomSockFuncs = {
      &GrpcPolledFdFactoryWindows::Socket /* socket */,
      &GrpcPolledFdFactoryWindows::CloseSocket /* close */,
      &GrpcPolledFdFactoryWindows::Connect /* connect */,
      &GrpcPolledFdFactoryWindows::RecvFrom /* recvfrom */,
      &GrpcPolledFdFactoryWindows::SendV /* sendv */,
  };

  Mutex* mu_;
  std::map<SOCKET, GrpcPolledFdWindows*> sockets_;
};

} // namespace

std::unique_ptr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(Mutex* mu) {
  return std::make_unique<GrpcPolledFdFactoryWindows>(mu);
}

}  // namespace grpc_core

#endif  // GRPC_ARES == 1 && defined(GPR_WINDOWS)
