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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_ENGINE_GRPC_POLLED_FD_WINDOWS_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_ENGINE_GRPC_POLLED_FD_WINDOWS_H

#include <grpc/support/port_platform.h>

#include <memory>

#include <ares.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include "grpc/event_engine/event_engine.h"

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/gprpp/sync.h"

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

class GrpcPolledFdFactoryWindows : public GrpcPolledFdFactory {
 public:
  GrpcPolledFdFactoryWindows(IOCP* iocp);
  ~GrpcPolledFdFactoryWindows() override;

  void Initialize(grpc_core::Mutex* mutex, EventEngine* event_engine) override;
  GrpcPolledFd* NewGrpcPolledFdLocked(ares_socket_t as) override;
  void ConfigureAresChannelLocked(ares_channel channel) override;

 private:
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
                        grpc_core::Mutex* mu, int address_family,
                        int socket_type,
                        absl::AnyInvocable<void()> on_shutdown_locked,
                        EventEngine* event_engine);
    ~GrpcPolledFdWindows() override;

    void RegisterForOnReadableLocked(
        absl::AnyInvocable<void(absl::Status)> read_closure) override;
    void RegisterForOnWriteableLocked(
        absl::AnyInvocable<void(absl::Status)> write_closure) override;
    bool IsFdStillReadableLocked() override;
    void ShutdownLocked(absl::Status error) override;
    ares_socket_t GetWrappedAresSocketLocked() override;
    const char* GetName() const override;

    ares_ssize_t RecvFrom(WSAErrorContext* wsa_error_ctx, void* data,
                          ares_socket_t data_len, int /* flags */,
                          struct sockaddr* from, ares_socklen_t* from_len);
    ares_ssize_t SendV(WSAErrorContext* wsa_error_ctx, const struct iovec* iov,
                       int iov_count);
    int Connect(WSAErrorContext* wsa_error_ctx, const struct sockaddr* target,
                ares_socklen_t target_len);

   private:
    enum WriteState {
      WRITE_IDLE,
      WRITE_REQUESTED,
      WRITE_PENDING,
      WRITE_WAITING_FOR_VERIFICATION_UPON_RETRY,
    };

    void ScheduleAndNullReadClosure(absl::Status error);
    void ScheduleAndNullWriteClosure(absl::Status error);
    void ContinueRegisterForOnReadableLocked();
    void ContinueRegisterForOnWriteableLocked();
    int SendWriteBuf(LPDWORD bytes_sent_ptr, LPWSAOVERLAPPED overlapped,
                     int* wsa_error_code);
    ares_ssize_t SendVUDP(WSAErrorContext* wsa_error_ctx,
                          const struct iovec* iov, int iov_count);
    ares_ssize_t SendVTCP(WSAErrorContext* wsa_error_ctx,
                          const struct iovec* iov, int iov_count);
    void OnTcpConnect();
    int ConnectUDP(WSAErrorContext* wsa_error_ctx,
                   const struct sockaddr* target, ares_socklen_t target_len);
    int ConnectTCP(WSAErrorContext* wsa_error_ctx,
                   const struct sockaddr* target, ares_socklen_t target_len);
    // TODO(apolcyn): improve this error handling to be less conversative.
    // An e.g. ECONNRESET error here should result in errors when
    // c-ares reads from this socket later, but it shouldn't necessarily cancel
    // the entire resolution attempt. Doing so will allow the "inject broken
    // nameserver list" test to pass on Windows.
    void OnIocpReadable();
    void OnIocpWriteable();

    static grpc_slice FlattenIovec(const struct iovec* iov, int iov_count);

    grpc_core::Mutex* mu_;
    std::unique_ptr<WinSocket> winsocket_;
    char recv_from_source_addr_[200];
    ares_socklen_t recv_from_source_addr_len_;
    grpc_slice read_buf_;
    bool read_buf_has_data_ = false;
    grpc_slice write_buf_;
    absl::AnyInvocable<void(absl::Status)> read_closure_;
    absl::AnyInvocable<void(absl::Status)> write_closure_;
    AnyInvocableClosure outer_read_closure_;
    AnyInvocableClosure outer_write_closure_;
    const std::string name_;
    bool shutdown_called_ = false;
    int address_family_;
    int socket_type_;
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
    absl::AnyInvocable<void()> on_shutdown_locked_;
    EventEngine* event_engine_;
  };

  // These virtual socket functions are called from within the c-ares
  // library. These methods generally dispatch those socket calls to the
  // appropriate methods. The virtual "socket" and "close" methods are
  // special and instead create/add and remove/destroy GrpcPolledFdWindows
  // objects.
  static ares_socket_t Socket(int af, int type, int protocol, void* user_data);
  static int Connect(ares_socket_t as, const struct sockaddr* target,
                     ares_socklen_t target_len, void* user_data);
  static ares_ssize_t SendV(ares_socket_t as, const struct iovec* iov,
                            int iovec_count, void* user_data);
  static ares_ssize_t RecvFrom(ares_socket_t as, void* data, size_t data_len,
                               int flags, struct sockaddr* from,
                               ares_socklen_t* from_len, void* user_data);
  static int CloseSocket(SOCKET /* s */, void* /* user_data */);

  grpc_core::Mutex* mu_;
  IOCP* iocp_;
  EventEngine* event_engine_;
  std::map<SOCKET, GrpcPolledFdWindows*> sockets_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_WINDOWS_ENGINE_GRPC_POLLED_FD_WINDOWS_H
