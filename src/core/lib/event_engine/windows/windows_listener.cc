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

#include "src/core/lib/event_engine/windows/windows_listener.h"

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

class SinglePortSocketListener {
 public:
  // This factory will create a bound, listening WinSocket, registered with the
  // listener's IOCP poller.
  static absl::StatusOr<std::unique_ptr<SinglePortSocketListener>> Create(
      WindowsEventEngineListener* listener, SOCKET sock,
      EventEngine::ResolvedAddress addr);

  // Two-stage initialization, allows creation of all bound sockets before the
  // listener is started.
  absl::Status Start();
  void Shutdown();

  // Accessor methods
  int port() { return port_; }
  WinSocket* win_socket() { return win_socket_.get(); }

 private:
  // Accepts a new connection and calls the EventEngine::AcceptCallback.
  class OnAcceptClosure : public EventEngine::Closure {
   public:
    explicit OnAcceptClosure(SinglePortSocketListener* self) : self_(self) {}
    void Run() override {
      // DO NOT SUBMIT(hork): needs a lock on the SinglePortSocketListener???
      // What's the race risk?

      auto restart = [&]() {
        GPR_ASSERT(GRPC_LOG_IF_ERROR("SinglePortSocketListener::Start",
                                     self_->Start()));
      };

      auto read_info = self_->win_socket()->read_info();
      // The general mechanism for shutting down is to queue abortion calls.
      // While this is necessary in the read/write case, it's useless for the
      // accept case. We only need to adjust the pending callback count
      if (read_info->wsa_error() != 0) {
        gpr_log(GPR_INFO, "%s",
                grpc_core::StatusToString(
                    GRPC_WSA_ERROR(read_info->wsa_error(),
                                   "Skipping on_accept due to error"))
                    .c_str());
        return;
      }
      // The IOCP notified us of a completed operation. Let's grab the results,
      // and act on them accordingly.
      read_info->GetOverlappedResult(self_->new_socket_);
      if (read_info->wsa_error() != 0 || self_->shutting_down_) {
        if (!self_->shutting_down_) {
          auto error = GRPC_WSA_ERROR(WSAGetLastError(),
                                      "OnAccept - GetOverlappedResult");
          gpr_log(GPR_ERROR, "%s", grpc_core::StatusToString(error).c_str());
        }
        closesocket(self_->new_socket_);
        restart();
        return;
      }
      int err =
          setsockopt(self_->new_socket_, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                     reinterpret_cast<char*>(self_->win_socket()->socket()),
                     sizeof(self_->win_socket()->socket()));
      if (err != 0) {
        gpr_log(
            GPR_ERROR, "%s",
            GRPC_WSA_ERROR(WSAGetLastError(), "setsockopt").ToString().c_str());
        // DO NOT SUBMIT(hork): iomgr version does not exit here. Why?
      }
      EventEngine::ResolvedAddress peer_name;
      int peer_name_len;
      err = getpeername(self_->new_socket_,
                        const_cast<sockaddr*>(peer_name.address()),
                        &peer_name_len);
      peer_name =
          EventEngine::ResolvedAddress(peer_name.address(), peer_name_len);
      std::string peer_name_string;
      if (err != 0) {
        gpr_log(GPR_ERROR, "%s",
                GRPC_WSA_ERROR(WSAGetLastError(), "getpeername")
                    .ToString()
                    .c_str());
        // DO NOT SUBMIT(hork): iomgr version does not exit here. Why?
      } else {
        auto addr_uri = ResolvedAddressToURI(peer_name);
        if (!addr_uri.ok()) {
          gpr_log(GPR_ERROR, "invalid peer name: %s",
                  addr_uri.status().ToString().c_str());
        } else {
            peer_name_string = *addr_uri;
        }
      }

      // DO NOT SUBMIT(hork): implement from tcp_server_windows.cc:302
      grpc_core::Crash("unimplemented");
    }

   private:
    SinglePortSocketListener* self_;
  };

  explicit SinglePortSocketListener(WindowsEventEngineListener* listener,
                                    LPFN_ACCEPTEX AcceptEx,
                                    std::unique_ptr<WinSocket> win_socket,
                                    int port)
      : listener_(listener),
        AcceptEx(AcceptEx),
        win_socket_(std::move(win_socket)),
        port_(port),
        on_accept_(this) {}

  // Bind a recently-created socket for listening
  static absl::StatusOr<int> PrepareListenerSocket(
      SOCKET sock, const EventEngine::ResolvedAddress& addr);

  void DecrementActivePortsAndNotifyLocked() {
    shutting_down_ = true;
    GPR_ASSERT(listener_->active_ports_ > 0);
    if (--listener_->active_ports_ == 0) {
      listener_->Shutdown();
    }
  }

  // This seemingly magic number comes from AcceptEx's documentation. each
  // address buffer needs to have at least 16 more bytes at their end.
  uint8_t addresses_[(sizeof(sockaddr_in6) + 16) * 2] = {};
  // This will hold the socket for the next accept.
  SOCKET new_socket_ = INVALID_SOCKET;
  WindowsEventEngineListener* listener_;
  // The cached AcceptEx for that port.
  LPFN_ACCEPTEX AcceptEx;
  // The listener winsocket.
  std::unique_ptr<WinSocket> win_socket_;
  // The actual TCP port number.
  int port_;
  bool shutting_down_ = false;
  int outstanding_calls_ = 0;
  // closure for socket notification of accept being ready
  OnAcceptClosure on_accept_;
};

absl::StatusOr<std::unique_ptr<SinglePortSocketListener>>
SinglePortSocketListener::Create(WindowsEventEngineListener* listener,
                                 SOCKET sock,
                                 EventEngine::ResolvedAddress addr) {
  // We need to grab the AcceptEx pointer for that port, as it may be
  // interface-dependent. We'll cache it to avoid doing that again.
  GUID guid = WSAID_ACCEPTEX;
  DWORD ioctl_num_bytes;
  LPFN_ACCEPTEX AcceptEx;
  int status =
      WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
               &AcceptEx, sizeof(AcceptEx), &ioctl_num_bytes, NULL, NULL);
  if (status != 0) {
    auto error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
    closesocket(sock);
    return error;
  }
  auto port = SinglePortSocketListener::PrepareListenerSocket(sock, addr);
  GRPC_RETURN_IF_ERROR(port.status());
  GPR_ASSERT(*port >= 0);
  return std::make_unique<SinglePortSocketListener>(
      listener, AcceptEx, /*win_socket=*/listener->iocp_->Watch(sock), *port);
}

absl::Status SinglePortSocketListener::Start() {
  if (shutting_down_) return absl::AbortedError("shutting down");
  SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                          IOCP::GetDefaultSocketFlags());
  if (sock == INVALID_SOCKET) {
    return GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
  }
  auto fail = [&](absl::Status error) -> absl::Status {
    if (sock != INVALID_SOCKET) closesocket(sock);
    return error;
  };
  auto error = PrepareSocket(sock);
  if (!error.ok()) return fail(error);
  // Start the "accept" asynchronously.
  DWORD addrlen = sizeof(sockaddr_in6) + 16;
  DWORD bytes_received = 0;
  int success =
      AcceptEx(win_socket_->socket(), sock, addresses_, 0, addrlen, addrlen,
               &bytes_received, win_socket_->read_info()->overlapped());
  // It is possible to get an accept immediately without delay. However, we
  // will still get an IOCP notification for it. So let's just ignore it.
  if (success != 0) {
    int last_error = WSAGetLastError();
    if (last_error != ERROR_IO_PENDING) {
      return fail(GRPC_WSA_ERROR(last_error, "AcceptEx"));
    }
  }
  // We're ready to do the accept. Calling grpc_socket_notify_on_read may
  // immediately process an accept that happened in the meantime.
  new_socket_ = sock;
  win_socket_->NotifyOnRead(&on_accept_);
  ++outstanding_calls_;
  return absl::OkStatus();
}

absl::StatusOr<int> SinglePortSocketListener::PrepareListenerSocket(
    SOCKET sock, const EventEngine::ResolvedAddress& addr) {
  auto fail = [&](absl::Status error) -> absl::Status {
    GPR_ASSERT(!error.ok());
    auto addr_uri = ResolvedAddressToURI(addr);
    grpc_error_set_int(
        grpc_error_set_str(
            GRPC_ERROR_CREATE_REFERENCING("Failed to prepare server socket",
                                          &error, 1),
            grpc_core::StatusStrProperty::kTargetAddress,
            addr_uri.ok() ? *addr_uri : addr_uri.status().ToString()),
        grpc_core::StatusIntProperty::kFd, static_cast<intptr_t>(sock));
    if (sock != INVALID_SOCKET) closesocket(sock);
    return error;
  };

  auto error = PrepareSocket(sock);
  if (!error.ok()) return fail(error);
  if (bind(sock, addr.address(), addr.size()) == SOCKET_ERROR) {
    return fail(GRPC_WSA_ERROR(WSAGetLastError(), "bind"));
  }
  if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
    return fail(GRPC_WSA_ERROR(WSAGetLastError(), "listen"));
  }
  int sockname_temp_len = sizeof(struct sockaddr_storage);
  EventEngine::ResolvedAddress sockname_temp;
  if (getsockname(sock, const_cast<sockaddr*>(sockname_temp.address()),
                  &sockname_temp_len) == SOCKET_ERROR) {
    return fail(GRPC_WSA_ERROR(WSAGetLastError(), "getsockname"));
  }
  sockname_temp =
      EventEngine::ResolvedAddress(sockname_temp.address(), sockname_temp_len);
  return ResolvedAddressGetPort(sockname_temp);
}

}  // namespace

WindowsEventEngineListener::WindowsEventEngineListener(
    IOCP* iocp, AcceptCallback accept_cb,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
    : iocp_(iocp),
      accept_cb_(std::move(accept_cb)),
      on_shutdown_(std::move(on_shutdown)),
      memory_allocator_factory_(std::move(memory_allocator_factory)) {}

WindowsEventEngineListener::~WindowsEventEngineListener() {
  auto shutdown_msg =
      absl::StrFormat("WindowsEventEngineListener::%p shutting down", this);
  GRPC_EVENT_ENGINE_TRACE("%s", shutdown_msg.c_str());
  on_shutdown_(absl::CancelledError(shutdown_msg));
}

absl::StatusOr<int> WindowsEventEngineListener::Bind(
    const EventEngine::ResolvedAddress& addr) {
  int out_port = ResolvedAddressGetPort(addr);
  EventEngine::ResolvedAddress out_addr(addr);
  EventEngine::ResolvedAddress tmp_addr;
  // Check if this is a  wildcard port, and if so, try to keep the port the same
  // as some previously created listener.
  if (out_port == 0) {
    grpc_core::MutexLock lock(&socket_listeners_mu_);
    for (const auto& socket_listener : socket_listeners_) {
      int sockname_temp_len = sizeof(struct sockaddr_storage);
      if (getsockname(socket_listener->win_socket()->socket(),
                      const_cast<sockaddr*>(tmp_addr.address()),
                      &sockname_temp_len) == 0) {
        tmp_addr =
            EventEngine::ResolvedAddress(tmp_addr.address(), sockname_temp_len);
        out_port = ResolvedAddressGetPort(tmp_addr);
        if (out_port > 0) {
          ResolvedAddressSetPort(out_addr, out_port);
          break;
        }
      }
    }
  }
  if (ResolvedAddressToV4Mapped(out_addr, &tmp_addr)) {
    out_addr = tmp_addr;
  }
  // Treat :: or 0.0.0.0 as a family-agnostic wildcard.
  if (ResolvedAddressIsWildcard(out_addr)) {
    out_addr = ResolvedAddressMakeWild6(out_port);
  }
  // open the socket
  SOCKET sock = WSASocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
                          IOCP::GetDefaultSocketFlags());
  if (sock == INVALID_SOCKET) {
    auto error = GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
    return GRPC_ERROR_CREATE_REFERENCING("Failed to add port to server", &error,
                                         1);
  }
  auto port_listener = AddSinglePortSocketListener(sock, out_addr);
  GRPC_RETURN_IF_ERROR(port_listener.status());
  return (*port_listener)->port();
}

absl::Status WindowsEventEngineListener::Start() {
  grpc_core::MutexLock lock(&socket_listeners_mu_);
  GPR_ASSERT(active_ports_ == 0);
  for (auto& socket_listener : socket_listeners_) {
    GRPC_RETURN_IF_ERROR(socket_listener->Start());
    ++active_ports_;
  }
}

absl::StatusOr<SinglePortSocketListener*>
WindowsEventEngineListener::AddSinglePortSocketListener(
    SOCKET sock, EventEngine::ResolvedAddress addr) {
  auto single_port_listener =
      SinglePortSocketListener::Create(this, sock, addr);
  GRPC_RETURN_IF_ERROR(single_port_listener.status());
  auto* single_port_listener_ptr = single_port_listener->get();
  grpc_core::MutexLock lock(&socket_listeners_mu_);
  socket_listeners_.emplace_back(std::move(*single_port_listener));
  return single_port_listener_ptr;
}

}  // namespace experimental
}  // namespace grpc_event_engine