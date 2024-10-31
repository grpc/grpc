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

#include "src/core/lib/iomgr/port.h"  // IWYU pragma: keep

#if GRPC_ARES == 1 && defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)

#include <ares.h>
#include <grpc/support/log_windows.h>
#include <winsock2.h>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/windows/grpc_polled_fd_windows.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/sync.h"

// TODO(apolcyn): remove this hack after fixing upstream.
// Our grpc/c-ares code on Windows uses the ares_set_socket_functions API,
// which uses "struct iovec" type, which on Windows is defined inside of
// a c-ares header that is not public.
// See https://github.com/c-ares/c-ares/issues/206.
struct iovec {
  void* iov_base;
  size_t iov_len;
};

namespace grpc_event_engine {
namespace experimental {
namespace {

constexpr int kRecvFromSourceAddrSize = 200;
constexpr int kReadBufferSize = 4192;

grpc_slice FlattenIovec(const struct iovec* iov, int iov_count) {
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

}  // namespace

// c-ares reads and takes action on the error codes of the
// "virtual socket operations" in this file, via the WSAGetLastError
// APIs. If code in this file wants to set a specific WSA error that
// c-ares should read, it must do so by calling SetWSAError() on the
// WSAErrorContext instance passed to it. A WSAErrorContext must only be
// instantiated at the top of the virtual socket function callstack.
class WSAErrorContext {
 public:
  explicit WSAErrorContext() {};

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
// windows poller (which gives notifications when attempted reads and writes
// are actually fulfilled rather than possible), this GrpcPolledFdWindows
// class takes advantage of the ares_set_socket_functions API and acts as a
// virtual socket. It holds its own read and write buffers which are written
// to and read from c-ares and are used with the grpc windows poller, and it,
// e.g., manufactures virtual socket error codes when it e.g. needs to tell
// the c-ares library to wait for an async read.
class GrpcPolledFdWindows : public GrpcPolledFd {
 public:
  GrpcPolledFdWindows(std::unique_ptr<WinSocket> winsocket,
                      grpc_core::Mutex* mu, int address_family, int socket_type,
                      EventEngine* event_engine)
      : name_(absl::StrFormat("c-ares socket: %" PRIdPTR,
                              winsocket->raw_socket())),
        address_family_(address_family),
        socket_type_(socket_type),
        mu_(mu),
        winsocket_(std::move(winsocket)),
        read_buf_(grpc_empty_slice()),
        write_buf_(grpc_empty_slice()),
        outer_read_closure_([this]() { OnIocpReadable(); }),
        outer_write_closure_([this]() { OnIocpWriteable(); }),
        on_tcp_connect_locked_([this]() { OnTcpConnect(); }),
        event_engine_(event_engine) {}

  ~GrpcPolledFdWindows() override {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:|" << GetName()
        << "| ~GrpcPolledFdWindows shutdown_called_: " << shutdown_called_;
    grpc_core::CSliceUnref(read_buf_);
    grpc_core::CSliceUnref(write_buf_);
    CHECK(read_closure_ == nullptr);
    CHECK(write_closure_ == nullptr);
    if (!shutdown_called_) {
      winsocket_->Shutdown(DEBUG_LOCATION, "~GrpcPolledFdWindows");
    }
  }

  void RegisterForOnReadableLocked(
      absl::AnyInvocable<void(absl::Status)> read_closure) override {
    CHECK(read_closure_ == nullptr);
    read_closure_ = std::move(read_closure);
    grpc_core::CSliceUnref(read_buf_);
    CHECK(!read_buf_has_data_);
    read_buf_ = GRPC_SLICE_MALLOC(kReadBufferSize);
    if (connect_done_) {
      ContinueRegisterForOnReadableLocked();
    } else {
      CHECK(pending_continue_register_for_on_readable_locked_ == false);
      pending_continue_register_for_on_readable_locked_ = true;
    }
  }

  void RegisterForOnWriteableLocked(
      absl::AnyInvocable<void(absl::Status)> write_closure) override {
    if (socket_type_ == SOCK_DGRAM) {
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:|" << GetName()
          << "| RegisterForOnWriteableLocked called";
    } else {
      CHECK(socket_type_ == SOCK_STREAM);
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:|" << GetName()
          << "| RegisterForOnWriteableLocked called tcp_write_state_: "
          << static_cast<int>(tcp_write_state_)
          << " connect_done_: " << connect_done_;
    }
    CHECK(write_closure_ == nullptr);
    write_closure_ = std::move(write_closure);
    if (!connect_done_) {
      CHECK(!pending_continue_register_for_on_writeable_locked_);
      pending_continue_register_for_on_writeable_locked_ = true;
    } else {
      ContinueRegisterForOnWriteableLocked();
    }
  }

  bool IsFdStillReadableLocked() override { return read_buf_has_data_; }

  bool ShutdownLocked(absl::Status error) override {
    CHECK(!shutdown_called_);
    if (!absl::IsCancelled(error)) {
      return false;
    }
    GRPC_TRACE_LOG(cares_resolver, INFO) << "(EventEngine c-ares resolver) fd:|"
                                         << GetName() << "| ShutdownLocked";
    shutdown_called_ = true;
    // The socket is disconnected and closed here since this is an external
    // cancel request, e.g. a timeout. c-ares shouldn't do anything on the
    // socket after this point except calling close which should then destroy
    // the GrpcPolledFdWindows object.
    winsocket_->Shutdown(DEBUG_LOCATION, "GrpcPolledFdWindows::ShutdownLocked");
    return true;
  }

  ares_socket_t GetWrappedAresSocketLocked() override {
    return winsocket_->raw_socket();
  }

  const char* GetName() const override { return name_.c_str(); }

  ares_ssize_t RecvFrom(WSAErrorContext* wsa_error_ctx, void* data,
                        ares_socket_t data_len, int /* flags */,
                        struct sockaddr* from, ares_socklen_t* from_len) {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:" << GetName()
        << " RecvFrom called read_buf_has_data:" << read_buf_has_data_
        << " Current read buf length:" << GRPC_SLICE_LENGTH(read_buf_);
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
      CHECK(*from_len <= recv_from_source_addr_len_);
      memcpy(from, &recv_from_source_addr_, recv_from_source_addr_len_);
      *from_len = recv_from_source_addr_len_;
    }
    return bytes_read;
  }

  ares_ssize_t SendV(WSAErrorContext* wsa_error_ctx, const struct iovec* iov,
                     int iov_count) {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:|" << GetName()
        << "| SendV called connect_done_:" << connect_done_
        << " wsa_connect_error_:" << wsa_connect_error_;
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

  int Connect(WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
              ares_socklen_t target_len) {
    switch (socket_type_) {
      case SOCK_DGRAM:
        return ConnectUDP(wsa_error_ctx, target, target_len);
      case SOCK_STREAM:
        return ConnectTCP(wsa_error_ctx, target, target_len);
      default:
        grpc_core::Crash(
            absl::StrFormat("Unknown socket_type_: %d", socket_type_));
    }
  }

 private:
  enum WriteState {
    WRITE_IDLE,
    WRITE_REQUESTED,
    WRITE_PENDING,
    WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY,
  };

  void ScheduleAndNullReadClosure(absl::Status error) {
    event_engine_->Run([read_closure = std::move(read_closure_),
                        error]() mutable { read_closure(error); });
    read_closure_ = nullptr;
  }

  void ScheduleAndNullWriteClosure(absl::Status error) {
    event_engine_->Run([write_closure = std::move(write_closure_),
                        error]() mutable { write_closure(error); });
    write_closure_ = nullptr;
  }

  void ContinueRegisterForOnReadableLocked() {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:|" << GetName()
        << "| ContinueRegisterForOnReadableLocked wsa_connect_error_:"
        << wsa_connect_error_;
    CHECK(connect_done_);
    if (wsa_connect_error_ != 0) {
      ScheduleAndNullReadClosure(GRPC_WSA_ERROR(wsa_connect_error_, "connect"));
      return;
    }
    WSABUF buffer;
    buffer.buf = reinterpret_cast<char*>(GRPC_SLICE_START_PTR(read_buf_));
    buffer.len = GRPC_SLICE_LENGTH(read_buf_);
    recv_from_source_addr_len_ = sizeof(recv_from_source_addr_);
    DWORD flags = 0;
    winsocket_->NotifyOnRead(&outer_read_closure_);
    if (WSARecvFrom(winsocket_->raw_socket(), &buffer, 1, nullptr, &flags,
                    reinterpret_cast<sockaddr*>(recv_from_source_addr_),
                    &recv_from_source_addr_len_,
                    winsocket_->read_info()->overlapped(), nullptr) != 0) {
      int wsa_last_error = WSAGetLastError();
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:" << GetName()
          << " ContinueRegisterForOnReadableLocked WSARecvFrom error "
             "code:"
          << wsa_last_error << " msg:" << msg;
      gpr_free(msg);
      if (wsa_last_error != WSA_IO_PENDING) {
        winsocket_->UnregisterReadCallback();
        ScheduleAndNullReadClosure(
            GRPC_WSA_ERROR(wsa_last_error, "WSARecvFrom"));
        return;
      }
    }
  }

  void ContinueRegisterForOnWriteableLocked() {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:|" << GetName()
        << "| ContinueRegisterForOnWriteableLocked wsa_connect_error_:"
        << wsa_connect_error_;
    CHECK(connect_done_);
    if (wsa_connect_error_ != 0) {
      ScheduleAndNullWriteClosure(
          GRPC_WSA_ERROR(wsa_connect_error_, "connect"));
      return;
    }
    if (socket_type_ == SOCK_DGRAM) {
      ScheduleAndNullWriteClosure(absl::OkStatus());
      return;
    }
    CHECK(socket_type_ == SOCK_STREAM);
    int wsa_error_code = 0;
    switch (tcp_write_state_) {
      case WRITE_IDLE:
        ScheduleAndNullWriteClosure(absl::OkStatus());
        break;
      case WRITE_REQUESTED:
        tcp_write_state_ = WRITE_PENDING;
        winsocket_->NotifyOnWrite(&outer_write_closure_);
        if (SendWriteBuf(nullptr, winsocket_->write_info()->overlapped(),
                         &wsa_error_code) != 0) {
          winsocket_->UnregisterWriteCallback();
          ScheduleAndNullWriteClosure(
              GRPC_WSA_ERROR(wsa_error_code, "WSASend (overlapped)"));
          return;
        }
        break;
      case WRITE_PENDING:
      case WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY:
        grpc_core::Crash(
            absl::StrFormat("Invalid tcp_write_state_: %d", tcp_write_state_));
    }
  }

  int SendWriteBuf(LPDWORD bytes_sent_ptr, LPWSAOVERLAPPED overlapped,
                   int* wsa_error_code) {
    WSABUF buf;
    buf.len = GRPC_SLICE_LENGTH(write_buf_);
    buf.buf = reinterpret_cast<char*>(GRPC_SLICE_START_PTR(write_buf_));
    DWORD flags = 0;
    int out = WSASend(winsocket_->raw_socket(), &buf, 1, bytes_sent_ptr, flags,
                      overlapped, nullptr);
    *wsa_error_code = WSAGetLastError();
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:" << GetName()
        << " SendWriteBuf WSASend buf.len:" << buf.len << " *bytes_sent_ptr:"
        << (bytes_sent_ptr != nullptr ? *bytes_sent_ptr : 0)
        << " overlapped:" << overlapped << " return:" << out
        << " *wsa_error_code:" << *wsa_error_code;
    return out;
  }

  ares_ssize_t SendVUDP(WSAErrorContext* wsa_error_ctx, const struct iovec* iov,
                        int iov_count) {
    // c-ares doesn't handle retryable errors on writes of UDP sockets.
    // Therefore, the sendv handler for UDP sockets must only attempt
    // to write everything inline.
    GRPC_TRACE_LOG(cares_resolver, INFO) << "(EventEngine c-ares resolver) fd:|"
                                         << GetName() << "| SendVUDP called";
    CHECK_EQ(GRPC_SLICE_LENGTH(write_buf_), 0);
    grpc_core::CSliceUnref(write_buf_);
    write_buf_ = FlattenIovec(iov, iov_count);
    DWORD bytes_sent = 0;
    int wsa_error_code = 0;
    if (SendWriteBuf(&bytes_sent, nullptr, &wsa_error_code) != 0) {
      grpc_core::CSliceUnref(write_buf_);
      write_buf_ = grpc_empty_slice();
      wsa_error_ctx->SetWSAError(wsa_error_code);
      char* msg = gpr_format_message(wsa_error_code);
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:|" << GetName()
          << "| SendVUDP SendWriteBuf error code:" << wsa_error_code << " msg:|"
          << msg << "|";
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
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:|" << GetName()
        << "| SendVTCP called tcp_write_state_:"
        << static_cast<int>(tcp_write_state_);
    switch (tcp_write_state_) {
      case WRITE_IDLE:
        tcp_write_state_ = WRITE_REQUESTED;
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
        CHECK(GRPC_SLICE_LENGTH(currently_attempted) >=
              GRPC_SLICE_LENGTH(write_buf_));
        ares_ssize_t total_sent = 0;
        for (size_t i = 0; i < GRPC_SLICE_LENGTH(write_buf_); i++) {
          CHECK(GRPC_SLICE_START_PTR(currently_attempted)[i] ==
                GRPC_SLICE_START_PTR(write_buf_)[i]);
          total_sent++;
        }
        grpc_core::CSliceUnref(currently_attempted);
        tcp_write_state_ = WRITE_IDLE;
        return total_sent;
    }
    grpc_core::Crash(
        absl::StrFormat("Unknown tcp_write_state_: %d", tcp_write_state_));
  }

  void OnTcpConnect() {
    grpc_core::MutexLock lock(mu_);
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:" << GetName()
        << " InnerOnTcpConnectLocked pending_register_for_readable:"
        << pending_continue_register_for_on_readable_locked_
        << " pending_register_for_writeable:"
        << pending_continue_register_for_on_writeable_locked_;
    CHECK(!connect_done_);
    connect_done_ = true;
    CHECK_EQ(wsa_connect_error_, 0);
    if (shutdown_called_) {
      wsa_connect_error_ = WSA_OPERATION_ABORTED;
    } else {
      DWORD transferred_bytes = 0;
      DWORD flags;
      BOOL wsa_success = WSAGetOverlappedResult(
          winsocket_->raw_socket(), winsocket_->write_info()->overlapped(),
          &transferred_bytes, FALSE, &flags);
      CHECK_EQ(transferred_bytes, 0);
      if (!wsa_success) {
        wsa_connect_error_ = WSAGetLastError();
        char* msg = gpr_format_message(wsa_connect_error_);
        GRPC_TRACE_LOG(cares_resolver, INFO)
            << "(EventEngine c-ares resolver) fd:" << GetName()
            << " InnerOnTcpConnectLocked WSA overlapped result code:"
            << wsa_connect_error_ << " msg:|" << msg << "|";
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

  int ConnectUDP(WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
                 ares_socklen_t target_len) {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:" << GetName() << " ConnectUDP";
    CHECK(!connect_done_);
    CHECK_EQ(wsa_connect_error_, 0);
    SOCKET s = winsocket_->raw_socket();
    int out =
        WSAConnect(s, target, target_len, nullptr, nullptr, nullptr, nullptr);
    wsa_connect_error_ = WSAGetLastError();
    wsa_error_ctx->SetWSAError(wsa_connect_error_);
    connect_done_ = true;
    char* msg = gpr_format_message(wsa_connect_error_);
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:" << GetName()
        << " WSAConnect error code:|" << wsa_connect_error_ << "| msg:|" << msg
        << "|";
    gpr_free(msg);
    // c-ares expects a posix-style connect API
    return out == 0 ? 0 : -1;
  }

  int ConnectTCP(WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
                 ares_socklen_t target_len) {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:" << GetName() << " ConnectTCP";
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
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:" << GetName()
          << " WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER) error code:"
          << wsa_last_error << " msg:|" << msg << "|";
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
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:" << GetName()
          << " bind error code:" << wsa_last_error << " msg:|" << msg << "|";
      gpr_free(msg);
      connect_done_ = true;
      wsa_connect_error_ = wsa_last_error;
      return -1;
    }
    int out = 0;
    // Register an async OnTcpConnect callback here since it is required by the
    // WinSocket API.
    winsocket_->NotifyOnWrite(&on_tcp_connect_locked_);
    if (ConnectEx(s, target, target_len, nullptr, 0, nullptr,
                  winsocket_->write_info()->overlapped()) == 0) {
      out = -1;
      int wsa_last_error = WSAGetLastError();
      wsa_error_ctx->SetWSAError(wsa_last_error);
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:" << GetName()
          << " ConnectEx error code:" << wsa_last_error << " msg:|" << msg
          << "|";
      gpr_free(msg);
      if (wsa_last_error == WSA_IO_PENDING) {
        // c-ares only understands WSAEINPROGRESS and EWOULDBLOCK error codes on
        // connect, but an async connect on IOCP socket will give
        // WSA_IO_PENDING, so we need to convert.
        wsa_error_ctx->SetWSAError(WSAEWOULDBLOCK);
      } else {
        winsocket_->UnregisterWriteCallback();
        // By returning a non-retryable error to c-ares at this point,
        // we're aborting the possibility of any future operations on this fd.
        connect_done_ = true;
        wsa_connect_error_ = wsa_last_error;
        return -1;
      }
    }
    return out;
  }

  // TODO(apolcyn): improve this error handling to be less conversative.
  // An e.g. ECONNRESET error here should result in errors when
  // c-ares reads from this socket later, but it shouldn't necessarily cancel
  // the entire resolution attempt. Doing so will allow the "inject broken
  // nameserver list" test to pass on Windows.
  void OnIocpReadable() {
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
        GRPC_TRACE_LOG(cares_resolver, INFO)
            << "(EventEngine c-ares resolver) fd:|" << GetName()
            << "| OnIocpReadableInner winsocket_->read_info.wsa_error "
               "code:|"
            << winsocket_->read_info()->result().wsa_error << "| msg:|"
            << grpc_core::StatusToString(error) << "|";
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
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:|" << GetName()
        << "| OnIocpReadable finishing. read buf length now:|"
        << GRPC_SLICE_LENGTH(read_buf_) << "|";
    ScheduleAndNullReadClosure(error);
  }

  void OnIocpWriteable() {
    grpc_core::MutexLock lock(mu_);
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) OnIocpWriteableInner. fd:|"
        << GetName() << "|";
    CHECK(socket_type_ == SOCK_STREAM);
    absl::Status error;
    if (winsocket_->write_info()->result().wsa_error != 0) {
      error = GRPC_WSA_ERROR(winsocket_->write_info()->result().wsa_error,
                             "OnIocpWriteableInner");
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:|" << GetName()
          << "| OnIocpWriteableInner. winsocket_->write_info.wsa_error "
             "code:|"
          << winsocket_->write_info()->result().wsa_error << "| msg:|"
          << grpc_core::StatusToString(error) << "|";
    }
    CHECK(tcp_write_state_ == WRITE_PENDING);
    if (error.ok()) {
      tcp_write_state_ = WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY;
      write_buf_ = grpc_slice_sub_no_ref(
          write_buf_, 0, winsocket_->write_info()->result().bytes_transferred);
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) fd:|" << GetName()
          << "| OnIocpWriteableInner. bytes transferred:"
          << winsocket_->write_info()->result().bytes_transferred;

    } else {
      grpc_core::CSliceUnref(write_buf_);
      write_buf_ = grpc_empty_slice();
    }
    ScheduleAndNullWriteClosure(error);
  }

  const std::string name_;
  const int address_family_;
  const int socket_type_;
  grpc_core::Mutex* mu_;
  std::unique_ptr<WinSocket> winsocket_;
  char recv_from_source_addr_[kRecvFromSourceAddrSize];
  ares_socklen_t recv_from_source_addr_len_;
  grpc_slice read_buf_;
  bool read_buf_has_data_ = false;
  grpc_slice write_buf_;
  absl::AnyInvocable<void(absl::Status)> read_closure_;
  absl::AnyInvocable<void(absl::Status)> write_closure_;
  AnyInvocableClosure outer_read_closure_;
  AnyInvocableClosure outer_write_closure_;
  bool shutdown_called_ = false;
  // State related to TCP sockets
  AnyInvocableClosure on_tcp_connect_locked_;
  bool connect_done_ = false;
  int wsa_connect_error_ = 0;
  WriteState tcp_write_state_ = WRITE_IDLE;
  // We don't run register_for_{readable,writeable} logic until
  // a socket is connected. In the interim, we queue readable/writeable
  // registrations with the following state.
  bool pending_continue_register_for_on_readable_locked_ = false;
  bool pending_continue_register_for_on_writeable_locked_ = false;
  // This pointer is initialized from the stored pointer inside the shared
  // pointer owned by the AresResolver and should be valid at the time of use.
  EventEngine* event_engine_;
};

// These virtual socket functions are called from within the c-ares
// library. These methods generally dispatch those socket calls to the
// appropriate methods. The virtual "socket" and "close" methods are
// special and instead create/add and remove/destroy GrpcPolledFdWindows
// objects.
class CustomSockFuncs {
 public:
  static ares_socket_t Socket(int af, int type, int protocol, void* user_data) {
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) Socket called with invalid socket "
             "type:"
          << type;
      return INVALID_SOCKET;
    }
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    SOCKET s = WSASocket(af, type, protocol, nullptr, 0,
                         IOCP::GetDefaultSocketFlags());
    if (s == INVALID_SOCKET) {
      GRPC_TRACE_LOG(cares_resolver, INFO)
          << "(EventEngine c-ares resolver) WSASocket failed with params af:"
          << af << " type:" << type << " protocol:" << protocol;
      return INVALID_SOCKET;
    }
    if (type == SOCK_STREAM) {
      absl::Status error = PrepareSocket(s);
      if (!error.ok()) {
        GRPC_TRACE_LOG(cares_resolver, INFO)
            << "(EventEngine c-ares resolver) WSAIoctl failed with error: "
            << grpc_core::StatusToString(error);
        return INVALID_SOCKET;
      }
    }
    auto polled_fd = std::make_unique<GrpcPolledFdWindows>(
        self->iocp_->Watch(s), self->mu_, af, type, self->event_engine_);
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) fd:" << polled_fd->GetName()
        << " created with params af:" << af << " type:" << type
        << " protocol:" << protocol;
    CHECK(self->sockets_.insert({s, std::move(polled_fd)}).second);
    return s;
  }

  static int Connect(ares_socket_t as, const struct sockaddr* target,
                     ares_socklen_t target_len, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    auto it = self->sockets_.find(as);
    CHECK(it != self->sockets_.end());
    return it->second->Connect(&wsa_error_ctx, target, target_len);
  }

  static ares_ssize_t SendV(ares_socket_t as, const struct iovec* iov,
                            int iovec_count, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    auto it = self->sockets_.find(as);
    CHECK(it != self->sockets_.end());
    return it->second->SendV(&wsa_error_ctx, iov, iovec_count);
  }

  static ares_ssize_t RecvFrom(ares_socket_t as, void* data, size_t data_len,
                               int flags, struct sockaddr* from,
                               ares_socklen_t* from_len, void* user_data) {
    WSAErrorContext wsa_error_ctx;
    GrpcPolledFdFactoryWindows* self =
        static_cast<GrpcPolledFdFactoryWindows*>(user_data);
    auto it = self->sockets_.find(as);
    CHECK(it != self->sockets_.end());
    return it->second->RecvFrom(&wsa_error_ctx, data, data_len, flags, from,
                                from_len);
  }

  static int CloseSocket(SOCKET s, void*) {
    GRPC_TRACE_LOG(cares_resolver, INFO)
        << "(EventEngine c-ares resolver) c-ares socket: " << s
        << " CloseSocket";
    return 0;
  }
};

// Adapter to hold the ownership of GrpcPolledFdWindows internally.
class GrpcPolledFdWrapper : public GrpcPolledFd {
 public:
  explicit GrpcPolledFdWrapper(GrpcPolledFdWindows* polled_fd)
      : polled_fd_(polled_fd) {}

  void RegisterForOnReadableLocked(
      absl::AnyInvocable<void(absl::Status)> read_closure) override {
    polled_fd_->RegisterForOnReadableLocked(std::move(read_closure));
  }

  void RegisterForOnWriteableLocked(
      absl::AnyInvocable<void(absl::Status)> write_closure) override {
    polled_fd_->RegisterForOnWriteableLocked(std::move(write_closure));
  }

  bool IsFdStillReadableLocked() override {
    return polled_fd_->IsFdStillReadableLocked();
  }

  bool ShutdownLocked(absl::Status error) override {
    return polled_fd_->ShutdownLocked(error);
  }

  ares_socket_t GetWrappedAresSocketLocked() override {
    return polled_fd_->GetWrappedAresSocketLocked();
  }

  const char* GetName() const override { return polled_fd_->GetName(); }

 private:
  GrpcPolledFdWindows* polled_fd_;
};

GrpcPolledFdFactoryWindows::GrpcPolledFdFactoryWindows(IOCP* iocp)
    : iocp_(iocp) {}

GrpcPolledFdFactoryWindows::~GrpcPolledFdFactoryWindows() {}

void GrpcPolledFdFactoryWindows::Initialize(grpc_core::Mutex* mutex,
                                            EventEngine* event_engine) {
  mu_ = mutex;
  event_engine_ = event_engine;
}

std::unique_ptr<GrpcPolledFd> GrpcPolledFdFactoryWindows::NewGrpcPolledFdLocked(
    ares_socket_t as) {
  auto it = sockets_.find(as);
  CHECK(it != sockets_.end());
  return std::make_unique<GrpcPolledFdWrapper>(it->second.get());
}

void GrpcPolledFdFactoryWindows::ConfigureAresChannelLocked(
    ares_channel channel) {
  static const struct ares_socket_functions kCustomSockFuncs = {
      /*asocket=*/&CustomSockFuncs::Socket,
      /*aclose=*/&CustomSockFuncs::CloseSocket,
      /*aconnect=*/&CustomSockFuncs::Connect,
      /*arecvfrom=*/&CustomSockFuncs::RecvFrom,
      /*asendv=*/&CustomSockFuncs::SendV,
  };
  ares_set_socket_functions(channel, &kCustomSockFuncs, this);
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_ARES == 1 && defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)
