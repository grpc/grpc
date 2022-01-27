/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"
#if GRPC_ARES == 1 && defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)

#include <string.h>

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
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/iocp_windows.h"
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/slice/slice_internal.h"

/* TODO(apolcyn): remove this hack after fixing upstream.
 * Our grpc/c-ares code on Windows uses the ares_set_socket_functions API,
 * which uses "struct iovec" type, which on Windows is defined inside of
 * a c-ares header that is not public.
 * See https://github.com/c-ares/c-ares/issues/206. */
struct iovec {
  void* iov_base;
  size_t iov_len;
};

namespace grpc_core {

/* c-ares reads and takes action on the error codes of the
 * "virtual socket operations" in this file, via the WSAGetLastError
 * APIs. If code in this file wants to set a specific WSA error that
 * c-ares should read, it must do so by calling SetWSAError() on the
 * WSAErrorContext instance passed to it. A WSAErrorContext must only be
 * instantiated at the top of the virtual socket function callstack. */
class WSAErrorContext {
 public:
  explicit WSAErrorContext(){};

  ~WSAErrorContext() {
    if (error_ != 0) {
      WSASetLastError(error_);
    }
  }

  /* Disallow copy and assignment operators */
  WSAErrorContext(const WSAErrorContext&) = delete;
  WSAErrorContext& operator=(const WSAErrorContext&) = delete;

  void SetWSAError(int error) { error_ = error; }

 private:
  int error_ = 0;
};

/* c-ares creates its own sockets and is meant to read them when readable and
 * write them when writeable. To fit this socket usage model into the grpc
 * windows poller (which gives notifications when attempted reads and writes are
 * actually fulfilled rather than possible), this GrpcPolledFdWindows class
 * takes advantage of the ares_set_socket_functions API and acts as a virtual
 * socket. It holds its own read and write buffers which are written to and read
 * from c-ares and are used with the grpc windows poller, and it, e.g.,
 * manufactures virtual socket error codes when it e.g. needs to tell the c-ares
 * library to wait for an async read. */
class GrpcPolledFdWindows {
 public:
  enum WriteState {
    WRITE_IDLE,
    WRITE_REQUESTED,
    WRITE_PENDING,
    WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY,
  };

  GrpcPolledFdWindows(ares_socket_t as, Mutex* mu, int address_family,
                      int socket_type)
      : mu_(mu),
        read_buf_(grpc_empty_slice()),
        write_buf_(grpc_empty_slice()),
        tcp_write_state_(WRITE_IDLE),
        name_(absl::StrFormat("c-ares socket: %" PRIdPTR, as)),
        gotten_into_driver_list_(false),
        address_family_(address_family),
        socket_type_(socket_type) {
    // Closure Initialization
    GRPC_CLOSURE_INIT(&outer_read_closure_,
                      &GrpcPolledFdWindows::OnIocpReadable, this,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&outer_write_closure_,
                      &GrpcPolledFdWindows::OnIocpWriteable, this,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&on_tcp_connect_locked_,
                      &GrpcPolledFdWindows::OnTcpConnect, this,
                      grpc_schedule_on_exec_ctx);
    winsocket_ = grpc_winsocket_create(as, name_.c_str());
  }

  ~GrpcPolledFdWindows() {
    grpc_slice_unref_internal(read_buf_);
    grpc_slice_unref_internal(write_buf_);
    GPR_ASSERT(read_closure_ == nullptr);
    GPR_ASSERT(write_closure_ == nullptr);
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
    grpc_slice_unref_internal(read_buf_);
    GPR_ASSERT(!read_buf_has_data_);
    read_buf_ = GRPC_SLICE_MALLOC(4192);
    if (connect_done_) {
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
    GPR_ASSERT(connect_done_);
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
    if (socket_type_ == SOCK_DGRAM) {
      GRPC_CARES_TRACE_LOG("fd:|%s| RegisterForOnWriteableLocked called",
                           GetName());
    } else {
      GPR_ASSERT(socket_type_ == SOCK_STREAM);
      GRPC_CARES_TRACE_LOG(
          "fd:|%s| RegisterForOnWriteableLocked called tcp_write_state_: %d",
          GetName(), tcp_write_state_);
    }
    GPR_ASSERT(write_closure_ == nullptr);
    write_closure_ = write_closure;
    if (connect_done_) {
      ContinueRegisterForOnWriteableLocked();
    } else {
      GPR_ASSERT(pending_continue_register_for_on_writeable_locked_ == false);
      pending_continue_register_for_on_writeable_locked_ = true;
    }
  }

  void ContinueRegisterForOnWriteableLocked() {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| ContinueRegisterForOnWriteableLocked "
        "wsa_connect_error_:%d",
        GetName(), wsa_connect_error_);
    GPR_ASSERT(connect_done_);
    if (wsa_connect_error_ != 0) {
      ScheduleAndNullWriteClosure(
          GRPC_WSA_ERROR(wsa_connect_error_, "connect"));
      return;
    }
    if (socket_type_ == SOCK_DGRAM) {
      ScheduleAndNullWriteClosure(GRPC_ERROR_NONE);
    } else {
      GPR_ASSERT(socket_type_ == SOCK_STREAM);
      int wsa_error_code = 0;
      switch (tcp_write_state_) {
        case WRITE_IDLE:
          ScheduleAndNullWriteClosure(GRPC_ERROR_NONE);
          break;
        case WRITE_REQUESTED:
          tcp_write_state_ = WRITE_PENDING;
          if (SendWriteBuf(nullptr, &winsocket_->write_info.overlapped,
                           &wsa_error_code) != 0) {
            ScheduleAndNullWriteClosure(
                GRPC_WSA_ERROR(wsa_error_code, "WSASend (overlapped)"));
          } else {
            grpc_socket_notify_on_write(winsocket_, &outer_write_closure_);
          }
          break;
        case WRITE_PENDING:
        case WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY:
          abort();
      }
    }
  }

  bool IsFdStillReadableLocked() { return read_buf_has_data_; }

  void ShutdownLocked(grpc_error_handle error) {
    grpc_winsocket_shutdown(winsocket_);
  }

  ares_socket_t GetWrappedAresSocketLocked() {
    return grpc_winsocket_wrapped_socket(winsocket_);
  }

  const char* GetName() const { return name_.c_str(); }

  ares_ssize_t RecvFrom(WSAErrorContext* wsa_error_ctx, void* data,
                        ares_socket_t data_len, int flags,
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
    /* c-ares overloads this recv_from virtual socket function to receive
     * data on both UDP and TCP sockets, and from is nullptr for TCP. */
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
      for (int k = 0; k < iov[i].iov_len; k++) {
        GRPC_SLICE_START_PTR(out)[cur++] = ((char*)iov[i].iov_base)[k];
      }
    }
    return out;
  }

  int SendWriteBuf(LPDWORD bytes_sent_ptr, LPWSAOVERLAPPED overlapped,
                   int* wsa_error_code) {
    WSABUF buf;
    buf.len = GRPC_SLICE_LENGTH(write_buf_);
    buf.buf = (char*)GRPC_SLICE_START_PTR(write_buf_);
    DWORD flags = 0;
    int out = WSASend(grpc_winsocket_wrapped_socket(winsocket_), &buf, 1,
                      bytes_sent_ptr, flags, overlapped, nullptr);
    *wsa_error_code = WSAGetLastError();
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| SendWriteBuf WSASend buf.len:%d *bytes_sent_ptr:%d "
        "overlapped:%p "
        "return:%d *wsa_error_code:%d",
        GetName(), buf.len, bytes_sent_ptr != nullptr ? *bytes_sent_ptr : 0,
        overlapped, out, *wsa_error_code);
    return out;
  }

  ares_ssize_t SendV(WSAErrorContext* wsa_error_ctx, const struct iovec* iov,
                     int iov_count) {
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| SendV called connect_done_:%d wsa_connect_error_:%d",
        GetName(), connect_done_, wsa_connect_error_);
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

  ares_ssize_t SendVUDP(WSAErrorContext* wsa_error_ctx, const struct iovec* iov,
                        int iov_count) {
    // c-ares doesn't handle retryable errors on writes of UDP sockets.
    // Therefore, the sendv handler for UDP sockets must only attempt
    // to write everything inline.
    GRPC_CARES_TRACE_LOG("fd:|%s| SendVUDP called", GetName());
    GPR_ASSERT(GRPC_SLICE_LENGTH(write_buf_) == 0);
    grpc_slice_unref_internal(write_buf_);
    write_buf_ = FlattenIovec(iov, iov_count);
    DWORD bytes_sent = 0;
    int wsa_error_code = 0;
    if (SendWriteBuf(&bytes_sent, nullptr, &wsa_error_code) != 0) {
      grpc_slice_unref_internal(write_buf_);
      write_buf_ = grpc_empty_slice();
      wsa_error_ctx->SetWSAError(wsa_error_code);
      char* msg = gpr_format_message(wsa_error_code);
      GRPC_CARES_TRACE_LOG(
          "fd:|%s| SendVUDP SendWriteBuf error code:%d msg:|%s|", GetName(),
          wsa_error_code, msg);
      gpr_free(msg);
      return -1;
    }
    write_buf_ = grpc_slice_sub_no_ref(write_buf_, bytes_sent,
                                       GRPC_SLICE_LENGTH(write_buf_));
    return bytes_sent;
  }

  ares_ssize_t SendVTCP(WSAErrorContext* wsa_error_ctx, const struct iovec* iov,
                        int iov_count) {
    // The "sendv" handler on TCP sockets buffers up write
    // requests and returns an artificial WSAEWOULDBLOCK. Writing that buffer
    // out in the background, and making further send progress in general, will
    // happen as long as c-ares continues to show interest in writeability on
    // this fd.
    GRPC_CARES_TRACE_LOG("fd:|%s| SendVTCP called tcp_write_state_:%d",
                         GetName(), tcp_write_state_);
    switch (tcp_write_state_) {
      case WRITE_IDLE:
        tcp_write_state_ = WRITE_REQUESTED;
        GPR_ASSERT(GRPC_SLICE_LENGTH(write_buf_) == 0);
        grpc_slice_unref_internal(write_buf_);
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
        grpc_slice_unref_internal(currently_attempted);
        tcp_write_state_ = WRITE_IDLE;
        return total_sent;
    }
    abort();
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
        GetName(), grpc_error_std_string(error).c_str(),
        pending_continue_register_for_on_readable_locked_,
        pending_continue_register_for_on_writeable_locked_);
    GPR_ASSERT(!connect_done_);
    connect_done_ = true;
    GPR_ASSERT(wsa_connect_error_ == 0);
    if (error == GRPC_ERROR_NONE) {
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
    GPR_ASSERT(!connect_done_);
    GPR_ASSERT(wsa_connect_error_ == 0);
    SOCKET s = grpc_winsocket_wrapped_socket(winsocket_);
    int out =
        WSAConnect(s, target, target_len, nullptr, nullptr, nullptr, nullptr);
    wsa_connect_error_ = WSAGetLastError();
    wsa_error_ctx->SetWSAError(wsa_connect_error_);
    connect_done_ = true;
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
    if (bind(s, (struct sockaddr*)local_address->addr,
             (int)local_address->len) != 0) {
      int wsa_last_error = WSAGetLastError();
      wsa_error_ctx->SetWSAError(wsa_last_error);
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_CARES_TRACE_LOG("fd:%s bind error code:%d msg:|%s|", GetName(),
                           wsa_last_error, msg);
      gpr_free(msg);
      connect_done_ = true;
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
        connect_done_ = true;
        wsa_connect_error_ = wsa_last_error;
        return -1;
      }
    }
    grpc_socket_notify_on_write(winsocket_, &on_tcp_connect_locked_);
    return out;
  }

  static void OnIocpReadable(void* arg, grpc_error_handle error) {
    GrpcPolledFdWindows* polled_fd = static_cast<GrpcPolledFdWindows*>(arg);
    (void)GRPC_ERROR_REF(error);
    MutexLock lock(polled_fd->mu_);
    polled_fd->OnIocpReadableLocked(error);
  }

  // TODO(apolcyn): improve this error handling to be less conversative.
  // An e.g. ECONNRESET error here should result in errors when
  // c-ares reads from this socket later, but it shouldn't necessarily cancel
  // the entire resolution attempt. Doing so will allow the "inject broken
  // nameserver list" test to pass on Windows.
  void OnIocpReadableLocked(grpc_error_handle error) {
    if (error == GRPC_ERROR_NONE) {
      if (winsocket_->read_info.wsa_error != 0) {
        /* WSAEMSGSIZE would be due to receiving more data
         * than our read buffer's fixed capacity. Assume that
         * the connection is TCP and read the leftovers
         * in subsequent c-ares reads. */
        if (winsocket_->read_info.wsa_error != WSAEMSGSIZE) {
          error = GRPC_WSA_ERROR(winsocket_->read_info.wsa_error,
                                 "OnIocpReadableInner");
          GRPC_CARES_TRACE_LOG(
              "fd:|%s| OnIocpReadableInner winsocket_->read_info.wsa_error "
              "code:|%d| msg:|%s|",
              GetName(), winsocket_->read_info.wsa_error,
              grpc_error_std_string(error).c_str());
        }
      }
    }
    if (error == GRPC_ERROR_NONE) {
      read_buf_ = grpc_slice_sub_no_ref(
          read_buf_, 0, winsocket_->read_info.bytes_transferred);
      read_buf_has_data_ = true;
    } else {
      grpc_slice_unref_internal(read_buf_);
      read_buf_ = grpc_empty_slice();
    }
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| OnIocpReadable finishing. read buf length now:|%d|", GetName(),
        GRPC_SLICE_LENGTH(read_buf_));
    ScheduleAndNullReadClosure(error);
  }

  static void OnIocpWriteable(void* arg, grpc_error_handle error) {
    GrpcPolledFdWindows* polled_fd = static_cast<GrpcPolledFdWindows*>(arg);
    (void)GRPC_ERROR_REF(error);
    MutexLock lock(polled_fd->mu_);
    polled_fd->OnIocpWriteableLocked(error);
  }

  void OnIocpWriteableLocked(grpc_error_handle error) {
    GRPC_CARES_TRACE_LOG("OnIocpWriteableInner. fd:|%s|", GetName());
    GPR_ASSERT(socket_type_ == SOCK_STREAM);
    if (error == GRPC_ERROR_NONE) {
      if (winsocket_->write_info.wsa_error != 0) {
        error = GRPC_WSA_ERROR(winsocket_->write_info.wsa_error,
                               "OnIocpWriteableInner");
        GRPC_CARES_TRACE_LOG(
            "fd:|%s| OnIocpWriteableInner. winsocket_->write_info.wsa_error "
            "code:|%d| msg:|%s|",
            GetName(), winsocket_->write_info.wsa_error,
            grpc_error_std_string(error).c_str());
      }
    }
    GPR_ASSERT(tcp_write_state_ == WRITE_PENDING);
    if (error == GRPC_ERROR_NONE) {
      tcp_write_state_ = WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY;
      write_buf_ = grpc_slice_sub_no_ref(
          write_buf_, 0, winsocket_->write_info.bytes_transferred);
      GRPC_CARES_TRACE_LOG("fd:|%s| OnIocpWriteableInner. bytes transferred:%d",
                           GetName(), winsocket_->write_info.bytes_transferred);
    } else {
      grpc_slice_unref_internal(write_buf_);
      write_buf_ = grpc_empty_slice();
    }
    ScheduleAndNullWriteClosure(error);
  }

  bool gotten_into_driver_list() const { return gotten_into_driver_list_; }
  void set_gotten_into_driver_list() { gotten_into_driver_list_ = true; }

 private:
  Mutex* mu_;
  char recv_from_source_addr_[200];
  ares_socklen_t recv_from_source_addr_len_;
  grpc_slice read_buf_;
  bool read_buf_has_data_ = false;
  grpc_slice write_buf_;
  grpc_closure* read_closure_ = nullptr;
  grpc_closure* write_closure_ = nullptr;
  grpc_closure outer_read_closure_;
  grpc_closure outer_write_closure_;
  grpc_winsocket* winsocket_;
  // tcp_write_state_ is only used on TCP GrpcPolledFds
  WriteState tcp_write_state_;
  const std::string name_;
  bool gotten_into_driver_list_;
  int address_family_;
  int socket_type_;
  grpc_closure on_tcp_connect_locked_;
  bool connect_done_ = false;
  int wsa_connect_error_ = 0;
  // We don't run register_for_{readable,writeable} logic until
  // a socket is connected. In the interim, we queue readable/writeable
  // registrations with the following state.
  bool pending_continue_register_for_on_readable_locked_ = false;
  bool pending_continue_register_for_on_writeable_locked_ = false;
};

struct SockToPolledFdEntry {
  SockToPolledFdEntry(SOCKET s, GrpcPolledFdWindows* fd)
      : socket(s), polled_fd(fd) {}
  SOCKET socket;
  GrpcPolledFdWindows* polled_fd;
  SockToPolledFdEntry* next = nullptr;
};

/* A SockToPolledFdMap can make ares_socket_t types (SOCKET's on windows)
 * to GrpcPolledFdWindow's, and is used to find the appropriate
 * GrpcPolledFdWindows to handle a virtual socket call when c-ares makes that
 * socket call on the ares_socket_t type. Instances are owned by and one-to-one
 * with a GrpcPolledFdWindows factory and event driver */
class SockToPolledFdMap {
 public:
  explicit SockToPolledFdMap(Mutex* mu) : mu_(mu) {}

  ~SockToPolledFdMap() { GPR_ASSERT(head_ == nullptr); }

  void AddNewSocket(SOCKET s, GrpcPolledFdWindows* polled_fd) {
    SockToPolledFdEntry* new_node = new SockToPolledFdEntry(s, polled_fd);
    new_node->next = head_;
    head_ = new_node;
  }

  GrpcPolledFdWindows* LookupPolledFd(SOCKET s) {
    for (SockToPolledFdEntry* node = head_; node != nullptr;
         node = node->next) {
      if (node->socket == s) {
        GPR_ASSERT(node->polled_fd != nullptr);
        return node->polled_fd;
      }
    }
    abort();
  }

  void RemoveEntry(SOCKET s) {
    GPR_ASSERT(head_ != nullptr);
    SockToPolledFdEntry** prev = &head_;
    for (SockToPolledFdEntry* node = head_; node != nullptr;
         node = node->next) {
      if (node->socket == s) {
        *prev = node->next;
        delete node;
        return;
      }
      prev = &node->next;
    }
    abort();
  }

  /* These virtual socket functions are called from within the c-ares
   * library. These methods generally dispatch those socket calls to the
   * appropriate methods. The virtual "socket" and "close" methods are
   * special and instead create/add and remove/destroy GrpcPolledFdWindows
   * objects.
   */
  static ares_socket_t Socket(int af, int type, int protocol, void* user_data) {
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
      GRPC_CARES_TRACE_LOG("Socket called with invalid socket type:%d", type);
      return INVALID_SOCKET;
    }
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    SOCKET s = WSASocket(af, type, protocol, nullptr, 0,
                         grpc_get_default_wsa_socket_flags());
    if (s == INVALID_SOCKET) {
      GRPC_CARES_TRACE_LOG(
          "WSASocket failed with params af:%d type:%d protocol:%d", af, type,
          protocol);
      return s;
    }
    grpc_tcp_set_non_block(s);
    GrpcPolledFdWindows* polled_fd =
        new GrpcPolledFdWindows(s, map->mu_, af, type);
    GRPC_CARES_TRACE_LOG(
        "fd:|%s| created with params af:%d type:%d protocol:%d",
        polled_fd->GetName(), af, type, protocol);
    map->AddNewSocket(s, polled_fd);
    return s;
  }

  static int Connect(ares_socket_t as, const struct sockaddr* target,
                     ares_socklen_t target_len, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(as);
    return polled_fd->Connect(&wsa_error_ctx, target, target_len);
  }

  static ares_ssize_t SendV(ares_socket_t as, const struct iovec* iov,
                            int iovec_count, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(as);
    return polled_fd->SendV(&wsa_error_ctx, iov, iovec_count);
  }

  static ares_ssize_t RecvFrom(ares_socket_t as, void* data, size_t data_len,
                               int flags, struct sockaddr* from,
                               ares_socklen_t* from_len, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(as);
    return polled_fd->RecvFrom(&wsa_error_ctx, data, data_len, flags, from,
                               from_len);
  }

  static int CloseSocket(SOCKET s, void* user_data) {
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(s);
    map->RemoveEntry(s);
    // See https://github.com/grpc/grpc/pull/20284, this trace log is
    // intentionally placed to attempt to trigger a crash in case of a
    // use after free on polled_fd.
    GRPC_CARES_TRACE_LOG("CloseSocket called for socket: %s",
                         polled_fd->GetName());
    // If a gRPC polled fd has not made it in to the driver's list yet, then
    // the driver has not and will never see this socket.
    if (!polled_fd->gotten_into_driver_list()) {
      polled_fd->ShutdownLocked(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Shut down c-ares fd before without it ever having made it into the "
          "driver's list"));
    }
    delete polled_fd;
    return 0;
  }

 private:
  Mutex* mu_;
  SockToPolledFdEntry* head_ = nullptr;
};

const struct ares_socket_functions custom_ares_sock_funcs = {
    &SockToPolledFdMap::Socket /* socket */,
    &SockToPolledFdMap::CloseSocket /* close */,
    &SockToPolledFdMap::Connect /* connect */,
    &SockToPolledFdMap::RecvFrom /* recvfrom */,
    &SockToPolledFdMap::SendV /* sendv */,
};

/* A thin wrapper over a GrpcPolledFdWindows object but with a shorter
   lifetime. This object releases it's GrpcPolledFdWindows upon destruction,
   so that c-ares can close it via usual socket teardown. */
class GrpcPolledFdWindowsWrapper : public GrpcPolledFd {
 public:
  explicit GrpcPolledFdWindowsWrapper(GrpcPolledFdWindows* wrapped)
      : wrapped_(wrapped) {}

  ~GrpcPolledFdWindowsWrapper() {}

  void RegisterForOnReadableLocked(grpc_closure* read_closure) override {
    wrapped_->RegisterForOnReadableLocked(read_closure);
  }

  void RegisterForOnWriteableLocked(grpc_closure* write_closure) override {
    wrapped_->RegisterForOnWriteableLocked(write_closure);
  }

  bool IsFdStillReadableLocked() override {
    return wrapped_->IsFdStillReadableLocked();
  }

  void ShutdownLocked(grpc_error_handle error) override {
    wrapped_->ShutdownLocked(error);
  }

  ares_socket_t GetWrappedAresSocketLocked() override {
    return wrapped_->GetWrappedAresSocketLocked();
  }

  const char* GetName() const override { return wrapped_->GetName(); }

 private:
  GrpcPolledFdWindows* const wrapped_;
};

class GrpcPolledFdFactoryWindows : public GrpcPolledFdFactory {
 public:
  explicit GrpcPolledFdFactoryWindows(Mutex* mu) : sock_to_polled_fd_map_(mu) {}

  GrpcPolledFd* NewGrpcPolledFdLocked(
      ares_socket_t as, grpc_pollset_set* driver_pollset_set) override {
    GrpcPolledFdWindows* polled_fd = sock_to_polled_fd_map_.LookupPolledFd(as);
    // Set a flag so that the virtual socket "close" method knows it
    // doesn't need to call ShutdownLocked, since now the driver will.
    polled_fd->set_gotten_into_driver_list();
    return new GrpcPolledFdWindowsWrapper(polled_fd);
  }

  void ConfigureAresChannelLocked(ares_channel channel) override {
    ares_set_socket_functions(channel, &custom_ares_sock_funcs,
                              &sock_to_polled_fd_map_);
  }

 private:
  SockToPolledFdMap sock_to_polled_fd_map_;
};

std::unique_ptr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(Mutex* mu) {
  return absl::make_unique<GrpcPolledFdFactoryWindows>(mu);
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GPR_WINDOWS) */
