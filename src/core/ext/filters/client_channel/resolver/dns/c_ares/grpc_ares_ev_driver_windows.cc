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
#if GRPC_ARES == 1 && defined(GPR_WINDOWS)

#include <ares.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <string.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#include "src/core/lib/slice/slice_internal.h"

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"

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

/* c-ares creates its own sockets and is meant to read them when readable and
 * write them when writeable. To fit this socket usage model into the grpc
 * windows poller (which gives notifications when attempted reads and writes are
 * actually fulfilled rather than possible), this GrpcPolledFdWindows class
 * takes advantage of the ares_set_socket_functions API and acts as a virtual
 * socket. It holds its own read and write buffers which are written to and read
 * from c-ares and are used with the grpc windows poller, and it, e.g.,
 * manufactures virtual socket error codes when it e.g. needs to tell the c-ares
 * library to wait for an async read. */
class GrpcPolledFdWindows : public GrpcPolledFd {
 public:
  enum WriteState {
    WRITE_IDLE,
    WRITE_REQUESTED,
    WRITE_PENDING,
    WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY,
  };

  GrpcPolledFdWindows(ares_socket_t as, grpc_combiner* combiner)
      : read_buf_(grpc_empty_slice()),
        write_buf_(grpc_empty_slice()),
        write_state_(WRITE_IDLE),
        gotten_into_driver_list_(false) {
    gpr_asprintf(&name_, "c-ares socket: %" PRIdPTR, as);
    winsocket_ = grpc_winsocket_create(as, name_);
    combiner_ = GRPC_COMBINER_REF(combiner, name_);
    GRPC_CLOSURE_INIT(&outer_read_closure_,
                      &GrpcPolledFdWindows::OnIocpReadable, this,
                      grpc_combiner_scheduler(combiner_));
    GRPC_CLOSURE_INIT(&outer_write_closure_,
                      &GrpcPolledFdWindows::OnIocpWriteable, this,
                      grpc_combiner_scheduler(combiner_));
  }

  ~GrpcPolledFdWindows() {
    GRPC_COMBINER_UNREF(combiner_, name_);
    grpc_slice_unref_internal(read_buf_);
    grpc_slice_unref_internal(write_buf_);
    GPR_ASSERT(read_closure_ == nullptr);
    GPR_ASSERT(write_closure_ == nullptr);
    grpc_winsocket_destroy(winsocket_);
    gpr_free(name_);
  }

  void ScheduleAndNullReadClosure(grpc_error* error) {
    GRPC_CLOSURE_SCHED(read_closure_, error);
    read_closure_ = nullptr;
  }

  void ScheduleAndNullWriteClosure(grpc_error* error) {
    GRPC_CLOSURE_SCHED(write_closure_, error);
    write_closure_ = nullptr;
  }

  void RegisterForOnReadableLocked(grpc_closure* read_closure) override {
    GPR_ASSERT(read_closure_ == nullptr);
    read_closure_ = read_closure;
    GPR_ASSERT(GRPC_SLICE_LENGTH(read_buf_) == 0);
    grpc_slice_unref_internal(read_buf_);
    read_buf_ = GRPC_SLICE_MALLOC(4192);
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
      grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      GRPC_CARES_TRACE_LOG(
          "RegisterForOnReadableLocked: WSARecvFrom error:|%s|. fd:|%s|", msg,
          GetName());
      gpr_free(msg);
      if (wsa_last_error != WSA_IO_PENDING) {
        ScheduleAndNullReadClosure(error);
        return;
      }
    }
    grpc_socket_notify_on_read(winsocket_, &outer_read_closure_);
  }

  void RegisterForOnWriteableLocked(grpc_closure* write_closure) override {
    GRPC_CARES_TRACE_LOG(
        "RegisterForOnWriteableLocked. fd:|%s|. Current write state: %d",
        GetName(), write_state_);
    GPR_ASSERT(write_closure_ == nullptr);
    write_closure_ = write_closure;
    switch (write_state_) {
      case WRITE_IDLE:
        ScheduleAndNullWriteClosure(GRPC_ERROR_NONE);
        break;
      case WRITE_REQUESTED:
        write_state_ = WRITE_PENDING;
        SendWriteBuf(nullptr, &winsocket_->write_info.overlapped);
        grpc_socket_notify_on_write(winsocket_, &outer_write_closure_);
        break;
      case WRITE_PENDING:
      case WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY:
        abort();
    }
  }

  bool IsFdStillReadableLocked() override {
    return GRPC_SLICE_LENGTH(read_buf_) > 0;
  }

  void ShutdownLocked(grpc_error* error) override {
    grpc_winsocket_shutdown(winsocket_);
  }

  ares_socket_t GetWrappedAresSocketLocked() override {
    return grpc_winsocket_wrapped_socket(winsocket_);
  }

  const char* GetName() override { return name_; }

  ares_ssize_t RecvFrom(void* data, ares_socket_t data_len, int flags,
                        struct sockaddr* from, ares_socklen_t* from_len) {
    GRPC_CARES_TRACE_LOG(
        "RecvFrom called on fd:|%s|. Current read buf length:|%d|", GetName(),
        GRPC_SLICE_LENGTH(read_buf_));
    if (GRPC_SLICE_LENGTH(read_buf_) == 0) {
      WSASetLastError(WSAEWOULDBLOCK);
      return -1;
    }
    ares_ssize_t bytes_read = 0;
    for (size_t i = 0; i < GRPC_SLICE_LENGTH(read_buf_) && i < data_len; i++) {
      ((char*)data)[i] = GRPC_SLICE_START_PTR(read_buf_)[i];
      bytes_read++;
    }
    read_buf_ = grpc_slice_sub_no_ref(read_buf_, bytes_read,
                                      GRPC_SLICE_LENGTH(read_buf_));
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

  int SendWriteBuf(LPDWORD bytes_sent_ptr, LPWSAOVERLAPPED overlapped) {
    WSABUF buf;
    buf.len = GRPC_SLICE_LENGTH(write_buf_);
    buf.buf = (char*)GRPC_SLICE_START_PTR(write_buf_);
    DWORD flags = 0;
    int out = WSASend(grpc_winsocket_wrapped_socket(winsocket_), &buf, 1,
                      bytes_sent_ptr, flags, overlapped, nullptr);
    GRPC_CARES_TRACE_LOG(
        "WSASend: name:%s. buf len:%d. bytes sent: %d. overlapped %p. return "
        "val: %d",
        GetName(), buf.len, *bytes_sent_ptr, overlapped, out);
    return out;
  }

  ares_ssize_t TrySendWriteBufSyncNonBlocking() {
    GPR_ASSERT(write_state_ == WRITE_IDLE);
    ares_ssize_t total_sent;
    DWORD bytes_sent = 0;
    if (SendWriteBuf(&bytes_sent, nullptr) != 0) {
      int wsa_last_error = WSAGetLastError();
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_CARES_TRACE_LOG(
          "TrySendWriteBufSyncNonBlocking: SendWriteBuf error:|%s|. fd:|%s|",
          msg, GetName());
      gpr_free(msg);
      if (wsa_last_error == WSA_IO_PENDING) {
        WSASetLastError(WSAEWOULDBLOCK);
        write_state_ = WRITE_REQUESTED;
      }
    }
    write_buf_ = grpc_slice_sub_no_ref(write_buf_, bytes_sent,
                                       GRPC_SLICE_LENGTH(write_buf_));
    return bytes_sent;
  }

  ares_ssize_t SendV(const struct iovec* iov, int iov_count) {
    GRPC_CARES_TRACE_LOG("SendV called on fd:|%s|. Current write state: %d",
                         GetName(), write_state_);
    switch (write_state_) {
      case WRITE_IDLE:
        GPR_ASSERT(GRPC_SLICE_LENGTH(write_buf_) == 0);
        grpc_slice_unref_internal(write_buf_);
        write_buf_ = FlattenIovec(iov, iov_count);
        return TrySendWriteBufSyncNonBlocking();
      case WRITE_REQUESTED:
      case WRITE_PENDING:
        WSASetLastError(WSAEWOULDBLOCK);
        return -1;
      case WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY:
        grpc_slice currently_attempted = FlattenIovec(iov, iov_count);
        GPR_ASSERT(GRPC_SLICE_LENGTH(currently_attempted) >=
                   GRPC_SLICE_LENGTH(write_buf_));
        ares_ssize_t total_sent = 0;
        for (size_t i = 0; i < GRPC_SLICE_LENGTH(write_buf_); i++) {
          GPR_ASSERT(GRPC_SLICE_START_PTR(currently_attempted)[i] ==
                     GRPC_SLICE_START_PTR(write_buf_)[i]);
          total_sent++;
        }
        grpc_slice_unref_internal(write_buf_);
        write_buf_ =
            grpc_slice_sub_no_ref(currently_attempted, total_sent,
                                  GRPC_SLICE_LENGTH(currently_attempted));
        write_state_ = WRITE_IDLE;
        total_sent += TrySendWriteBufSyncNonBlocking();
        return total_sent;
    }
    abort();
  }

  int Connect(const struct sockaddr* target, ares_socklen_t target_len) {
    SOCKET s = grpc_winsocket_wrapped_socket(winsocket_);
    GRPC_CARES_TRACE_LOG("Connect: fd:|%s|", GetName());
    int out =
        WSAConnect(s, target, target_len, nullptr, nullptr, nullptr, nullptr);
    if (out != 0) {
      int wsa_last_error = WSAGetLastError();
      char* msg = gpr_format_message(wsa_last_error);
      GRPC_CARES_TRACE_LOG("Connect error code:|%d|, msg:|%s|. fd:|%s|",
                           wsa_last_error, msg, GetName());
      gpr_free(msg);
      // c-ares expects a posix-style connect API
      out = -1;
    }
    return out;
  }

  static void OnIocpReadable(void* arg, grpc_error* error) {
    GrpcPolledFdWindows* polled_fd = static_cast<GrpcPolledFdWindows*>(arg);
    polled_fd->OnIocpReadableInner(error);
  }

  void OnIocpReadableInner(grpc_error* error) {
    if (error == GRPC_ERROR_NONE) {
      if (winsocket_->read_info.wsa_error != 0) {
        /* WSAEMSGSIZE would be due to receiving more data
         * than our read buffer's fixed capacity. Assume that
         * the connection is TCP and read the leftovers
         * in subsequent c-ares reads. */
        if (winsocket_->read_info.wsa_error != WSAEMSGSIZE) {
          GRPC_ERROR_UNREF(error);
          char* msg = gpr_format_message(winsocket_->read_info.wsa_error);
          GRPC_CARES_TRACE_LOG(
              "OnIocpReadableInner. winsocket error:|%s|. fd:|%s|", msg,
              GetName());
          error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
          gpr_free(msg);
        }
      }
    }
    if (error == GRPC_ERROR_NONE) {
      read_buf_ = grpc_slice_sub_no_ref(read_buf_, 0,
                                        winsocket_->read_info.bytes_transfered);
    } else {
      grpc_slice_unref_internal(read_buf_);
      read_buf_ = grpc_empty_slice();
    }
    GRPC_CARES_TRACE_LOG(
        "OnIocpReadable finishing. read buf length now:|%d|. :fd:|%s|",
        GRPC_SLICE_LENGTH(read_buf_), GetName());
    ScheduleAndNullReadClosure(error);
  }

  static void OnIocpWriteable(void* arg, grpc_error* error) {
    GrpcPolledFdWindows* polled_fd = static_cast<GrpcPolledFdWindows*>(arg);
    polled_fd->OnIocpWriteableInner(error);
  }

  void OnIocpWriteableInner(grpc_error* error) {
    GRPC_CARES_TRACE_LOG("OnIocpWriteableInner. fd:|%s|", GetName());
    if (error == GRPC_ERROR_NONE) {
      if (winsocket_->write_info.wsa_error != 0) {
        char* msg = gpr_format_message(winsocket_->write_info.wsa_error);
        GRPC_CARES_TRACE_LOG(
            "OnIocpWriteableInner. winsocket error:|%s|. fd:|%s|", msg,
            GetName());
        GRPC_ERROR_UNREF(error);
        error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
      }
    }
    GPR_ASSERT(write_state_ == WRITE_PENDING);
    if (error == GRPC_ERROR_NONE) {
      write_state_ = WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY;
      write_buf_ = grpc_slice_sub_no_ref(
          write_buf_, 0, winsocket_->write_info.bytes_transfered);
    } else {
      grpc_slice_unref_internal(write_buf_);
      write_buf_ = grpc_empty_slice();
    }
    ScheduleAndNullWriteClosure(error);
  }

  bool gotten_into_driver_list() const { return gotten_into_driver_list_; }
  void set_gotten_into_driver_list() { gotten_into_driver_list_ = true; }

  grpc_combiner* combiner_;
  char recv_from_source_addr_[200];
  ares_socklen_t recv_from_source_addr_len_;
  grpc_slice read_buf_;
  grpc_slice write_buf_;
  grpc_closure* read_closure_ = nullptr;
  grpc_closure* write_closure_ = nullptr;
  grpc_closure outer_read_closure_;
  grpc_closure outer_write_closure_;
  grpc_winsocket* winsocket_;
  WriteState write_state_;
  char* name_ = nullptr;
  bool gotten_into_driver_list_;
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
  SockToPolledFdMap(grpc_combiner* combiner) {
    combiner_ = GRPC_COMBINER_REF(combiner, "sock to polled fd map");
  }

  ~SockToPolledFdMap() {
    GPR_ASSERT(head_ == nullptr);
    GRPC_COMBINER_UNREF(combiner_, "sock to polled fd map");
  }

  void AddNewSocket(SOCKET s, GrpcPolledFdWindows* polled_fd) {
    SockToPolledFdEntry* new_node = New<SockToPolledFdEntry>(s, polled_fd);
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
        Delete(node);
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
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    SOCKET s = WSASocket(af, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
      return s;
    }
    grpc_tcp_set_non_block(s);
    GrpcPolledFdWindows* polled_fd =
        New<GrpcPolledFdWindows>(s, map->combiner_);
    map->AddNewSocket(s, polled_fd);
    return s;
  }

  static int Connect(ares_socket_t as, const struct sockaddr* target,
                     ares_socklen_t target_len, void* user_data) {
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(as);
    return polled_fd->Connect(target, target_len);
  }

  static ares_ssize_t SendV(ares_socket_t as, const struct iovec* iov,
                            int iovec_count, void* user_data) {
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(as);
    return polled_fd->SendV(iov, iovec_count);
  }

  static ares_ssize_t RecvFrom(ares_socket_t as, void* data, size_t data_len,
                               int flags, struct sockaddr* from,
                               ares_socklen_t* from_len, void* user_data) {
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(as);
    return polled_fd->RecvFrom(data, data_len, flags, from, from_len);
  }

  static int CloseSocket(SOCKET s, void* user_data) {
    SockToPolledFdMap* map = static_cast<SockToPolledFdMap*>(user_data);
    GrpcPolledFdWindows* polled_fd = map->LookupPolledFd(s);
    map->RemoveEntry(s);
    // If a gRPC polled fd has not made it in to the driver's list yet, then
    // the driver has not and will never see this socket.
    if (!polled_fd->gotten_into_driver_list()) {
      polled_fd->ShutdownLocked(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Shut down c-ares fd before without it ever having made it into the "
          "driver's list"));
      return 0;
    }
    return 0;
  }

 private:
  SockToPolledFdEntry* head_ = nullptr;
  grpc_combiner* combiner_;
};

const struct ares_socket_functions custom_ares_sock_funcs = {
    &SockToPolledFdMap::Socket /* socket */,
    &SockToPolledFdMap::CloseSocket /* close */,
    &SockToPolledFdMap::Connect /* connect */,
    &SockToPolledFdMap::RecvFrom /* recvfrom */,
    &SockToPolledFdMap::SendV /* sendv */,
};

class GrpcPolledFdFactoryWindows : public GrpcPolledFdFactory {
 public:
  GrpcPolledFdFactoryWindows(grpc_combiner* combiner)
      : sock_to_polled_fd_map_(combiner) {}

  GrpcPolledFd* NewGrpcPolledFdLocked(ares_socket_t as,
                                      grpc_pollset_set* driver_pollset_set,
                                      grpc_combiner* combiner) override {
    GrpcPolledFdWindows* polled_fd = sock_to_polled_fd_map_.LookupPolledFd(as);
    // Set a flag so that the virtual socket "close" method knows it
    // doesn't need to call ShutdownLocked, since now the driver will.
    polled_fd->set_gotten_into_driver_list();
    return polled_fd;
  }

  void ConfigureAresChannelLocked(ares_channel channel) override {
    ares_set_socket_functions(channel, &custom_ares_sock_funcs,
                              &sock_to_polled_fd_map_);
  }

 private:
  SockToPolledFdMap sock_to_polled_fd_map_;
};

UniquePtr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(grpc_combiner* combiner) {
  return UniquePtr<GrpcPolledFdFactory>(
      New<GrpcPolledFdFactoryWindows>(combiner));
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GPR_WINDOWS) */
