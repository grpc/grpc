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

#ifdef GPR_WINDOWS

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/event_engine/windows/windows_endpoint.h"
#include "src/core/lib/event_engine/windows/windows_listener.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine {
namespace experimental {

// ---- SinglePortSocketListener::AsyncIOState ----

WindowsEventEngineListener::SinglePortSocketListener::AsyncIOState::
    AsyncIOState(SinglePortSocketListener* port_listener,
                 std::unique_ptr<WinSocket> listener_socket)
    : port_listener(port_listener),
      listener_socket(std::move(listener_socket)) {}

WindowsEventEngineListener::SinglePortSocketListener::AsyncIOState::
    ~AsyncIOState() {
  closesocket(accept_socket);
}

void WindowsEventEngineListener::SinglePortSocketListener::
    OnAcceptCallbackWrapper::Run() {
  CHECK_NE(io_state_, nullptr);
  grpc_core::ReleasableMutexLock lock(&io_state_->mu);
  if (io_state_->listener_socket->IsShutdown()) {
    GRPC_TRACE_LOG(event_engine, INFO)
        << "SinglePortSocketListener::" << io_state_->port_listener
        << " listener socket is shut down. Shutting down listener.";
    lock.Release();
    io_state_.reset();
    return;
  }
  io_state_->port_listener->OnAcceptCallbackLocked();
}

void WindowsEventEngineListener::SinglePortSocketListener::
    OnAcceptCallbackWrapper::Prime(std::shared_ptr<AsyncIOState> io_state) {
  io_state_ = std::move(io_state);
}

// ---- SinglePortSocketListener ----

// TODO(hork): This may be refactored to share with posix engine.
void UnlinkIfUnixDomainSocket(
    const EventEngine::ResolvedAddress& resolved_addr) {
#ifdef GRPC_HAVE_UNIX_SOCKET
  if (resolved_addr.address()->sa_family != AF_UNIX) {
    return;
  }
  struct sockaddr_un* un = reinterpret_cast<struct sockaddr_un*>(
      const_cast<sockaddr*>(resolved_addr.address()));
  // There is nothing to unlink for an abstract unix socket.
  if (un->sun_path[0] == '\0' && un->sun_path[1] != '\0') {
    return;
  }
  // For windows we need to remove the file instead of unlink.
  DWORD attr = ::GetFileAttributesA(un->sun_path);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    return;
  }
  if (attr & FILE_ATTRIBUTE_DIRECTORY || attr & FILE_ATTRIBUTE_READONLY) {
    return;
  }
  ::DeleteFileA(un->sun_path);
#else
  (void)resolved_addr;
#endif
}

WindowsEventEngineListener::SinglePortSocketListener::
    ~SinglePortSocketListener() {
  grpc_core::MutexLock lock(&io_state_->mu);
  io_state_->listener_socket->Shutdown(DEBUG_LOCATION,
                                       "~SinglePortSocketListener");
  UnlinkIfUnixDomainSocket(listener_sockname());
  GRPC_TRACE_LOG(event_engine, INFO) << "~SinglePortSocketListener::" << this;
}

absl::StatusOr<
    std::unique_ptr<WindowsEventEngineListener::SinglePortSocketListener>>
WindowsEventEngineListener::SinglePortSocketListener::Create(
    WindowsEventEngineListener* listener, SOCKET sock,
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
  auto result = SinglePortSocketListener::PrepareListenerSocket(sock, addr);
  GRPC_RETURN_IF_ERROR(result.status());
  CHECK_GE(result->port, 0);
  // Using `new` to access non-public constructor
  return absl::WrapUnique(new SinglePortSocketListener(
      listener, AcceptEx, /*win_socket=*/listener->iocp_->Watch(sock),
      result->port, result->hostbyname));
}

absl::Status WindowsEventEngineListener::SinglePortSocketListener::Start() {
  grpc_core::MutexLock lock(&io_state_->mu);
  return StartLocked();
}

absl::Status
WindowsEventEngineListener::SinglePortSocketListener::StartLocked() {
  const EventEngine::ResolvedAddress addr = listener_sockname();
  const int addr_family =
      (addr.address()->sa_family == AF_UNIX) ? AF_UNIX : AF_INET6;
  const int protocol = addr_family == AF_UNIX ? 0 : IPPROTO_TCP;
  SOCKET accept_socket = WSASocket(addr_family, SOCK_STREAM, protocol, NULL, 0,
                                   IOCP::GetDefaultSocketFlags());
  if (accept_socket == INVALID_SOCKET) {
    return GRPC_WSA_ERROR(WSAGetLastError(), "WSASocket");
  }
  auto fail = [&](absl::Status error) -> absl::Status {
    if (accept_socket != INVALID_SOCKET) closesocket(accept_socket);
    return error;
  };
  absl::Status error;
  if (addr_family == AF_UNIX) {
    error = SetSocketNonBlock(accept_socket);
  } else {
    error = PrepareSocket(accept_socket);
  }
  if (!error.ok()) return fail(error);
  // Start the "accept" asynchronously.
  io_state_->listener_socket->NotifyOnRead(&io_state_->on_accept_cb);
  DWORD addrlen =
      sizeof(addresses_) / 2;  // half of the buffer is for remote addr.
  DWORD bytes_received = 0;
  int success =
      AcceptEx(io_state_->listener_socket->raw_socket(), accept_socket,
               addresses_, 0, addrlen, addrlen, &bytes_received,
               io_state_->listener_socket->read_info()->overlapped());
  // It is possible to get an accept immediately without delay. However, we
  // will still get an IOCP notification for it. So let's just ignore it.
  if (success != 0) {
    int last_error = WSAGetLastError();
    if (last_error != ERROR_IO_PENDING) {
      io_state_->listener_socket->UnregisterReadCallback();
      return fail(GRPC_WSA_ERROR(last_error, "AcceptEx"));
    }
  }
  io_state_->accept_socket = accept_socket;
  GRPC_TRACE_LOG(event_engine, INFO)
      << "SinglePortSocketListener::" << this
      << " listening. listener_socket::" << io_state_->listener_socket.get();
  return absl::OkStatus();
}

void WindowsEventEngineListener::SinglePortSocketListener::
    OnAcceptCallbackLocked() {
  auto close_socket_and_restart =
      [&](bool do_close_socket = true)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(io_state_->mu) {
            if (do_close_socket) closesocket(io_state_->accept_socket);
            io_state_->accept_socket = INVALID_SOCKET;
            CHECK(GRPC_LOG_IF_ERROR("SinglePortSocketListener::Start",
                                    StartLocked()));
          };
  const auto& overlapped_result =
      io_state_->listener_socket->read_info()->result();
  if (overlapped_result.wsa_error != 0) {
    LOG(ERROR) << GRPC_WSA_ERROR(overlapped_result.wsa_error,
                                 "Skipping on_accept due to error");
    return close_socket_and_restart();
  }
  SOCKET tmp_listener_socket = io_state_->listener_socket->raw_socket();
  int err =
      setsockopt(io_state_->accept_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                 reinterpret_cast<char*>(&tmp_listener_socket),
                 sizeof(tmp_listener_socket));
  if (err != 0) {
    LOG(ERROR) << GRPC_WSA_ERROR(WSAGetLastError(), "setsockopt");
    return close_socket_and_restart();
  }
  EventEngine::ResolvedAddress peer_address;
  int peer_name_len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  err = getpeername(io_state_->accept_socket,
                    const_cast<sockaddr*>(peer_address.address()),
                    &peer_name_len);
  if (err != 0) {
    LOG(ERROR) << GRPC_WSA_ERROR(WSAGetLastError(), "getpeername");
    return close_socket_and_restart();
  }
  peer_address =
      EventEngine::ResolvedAddress(peer_address.address(), peer_name_len);
  auto addr_uri = ResolvedAddressToURI(peer_address);
  std::string peer_name = "unknown";
  if (!addr_uri.ok()) {
    // TODO(hork): test an early exit/restart here with end2end tests
    LOG(ERROR) << "invalid peer name: " << addr_uri.status();
  } else {
    peer_name = *addr_uri;
  }
  auto endpoint = std::make_unique<WindowsEndpoint>(
      peer_address, listener_->iocp_->Watch(io_state_->accept_socket),
      listener_->memory_allocator_factory_->CreateMemoryAllocator(
          absl::StrFormat("listener endpoint %s", peer_name)),
      listener_->config_, listener_->thread_pool_, listener_->engine_);
  listener_->accept_cb_(
      std::move(endpoint),
      listener_->memory_allocator_factory_->CreateMemoryAllocator(
          absl::StrFormat("listener accept cb for %s", peer_name)));
  close_socket_and_restart(/*do_close_socket=*/false);
}

WindowsEventEngineListener::SinglePortSocketListener::SinglePortSocketListener(
    WindowsEventEngineListener* listener, LPFN_ACCEPTEX AcceptEx,
    std::unique_ptr<WinSocket> listener_socket, int port,
    EventEngine::ResolvedAddress hostbyname)
    : AcceptEx(AcceptEx),
      listener_(listener),
      io_state_(
          std::make_shared<AsyncIOState>(this, std::move(listener_socket))),
      port_(port),
      listener_sockname_(hostbyname) {
  io_state_->on_accept_cb.Prime(io_state_);
}

absl::StatusOr<WindowsEventEngineListener::SinglePortSocketListener::
                   PrepareListenerSocketResult>
WindowsEventEngineListener::SinglePortSocketListener::PrepareListenerSocket(
    SOCKET sock, const EventEngine::ResolvedAddress& addr) {
  auto fail = [&](absl::Status error) -> absl::Status {
    CHECK(!error.ok());
    error = grpc_error_set_int(
        GRPC_ERROR_CREATE_REFERENCING("Failed to prepare server socket", &error,
                                      1),
        grpc_core::StatusIntProperty::kFd, static_cast<intptr_t>(sock));
    if (sock != INVALID_SOCKET) closesocket(sock);
    return error;
  };
  absl::Status error;
  if (addr.address()->sa_family == AF_UNIX) {
    error = SetSocketNonBlock(sock);
  } else {
    error = PrepareSocket(sock);
  }
  if (!error.ok()) return fail(error);
  UnlinkIfUnixDomainSocket(addr);
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
  return PrepareListenerSocketResult{ResolvedAddressGetPort(sockname_temp),
                                     sockname_temp};
}

// ---- WindowsEventEngineListener ----

WindowsEventEngineListener::WindowsEventEngineListener(
    IOCP* iocp, AcceptCallback accept_cb,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory,
    std::shared_ptr<EventEngine> engine, ThreadPool* thread_pool,
    const EndpointConfig& config)
    : iocp_(iocp),
      config_(config),
      engine_(std::move(engine)),
      thread_pool_(thread_pool),
      memory_allocator_factory_(std::move(memory_allocator_factory)),
      accept_cb_(std::move(accept_cb)),
      on_shutdown_(std::move(on_shutdown)) {}

WindowsEventEngineListener::~WindowsEventEngineListener() {
  GRPC_TRACE_LOG(event_engine, INFO) << "~WindowsEventEngineListener::" << this;
  ShutdownListeners();
  on_shutdown_(absl::OkStatus());
}

absl::StatusOr<int> WindowsEventEngineListener::Bind(
    const EventEngine::ResolvedAddress& addr) {
  if (started_.load()) {
    return absl::FailedPreconditionError(
        absl::StrFormat("WindowsEventEngineListener::%p is already started, "
                        "ports can no longer be bound",
                        this));
  }
  int out_port = ResolvedAddressGetPort(addr);
  EventEngine::ResolvedAddress out_addr(addr);
  EventEngine::ResolvedAddress tmp_addr;
  // Check if this is a  wildcard port, and if so, try to keep the port the same
  // as some previously created listener.
  if (out_port == 0) {
    grpc_core::MutexLock lock(&port_listeners_mu_);
    for (const auto& port_listener : port_listeners_) {
      tmp_addr = port_listener->listener_sockname();
      out_port = ResolvedAddressGetPort(tmp_addr);
      if (out_port > 0) {
        ResolvedAddressSetPort(out_addr, out_port);
        break;
      }
    }
  }
  if (ResolvedAddressToV4Mapped(out_addr, &tmp_addr)) {
    out_addr = tmp_addr;
  }
  // Treat :: or 0.0.0.0 as a family-agnostic wildcard.
  if (MaybeGetWildcardPortFromAddress(out_addr).has_value()) {
    out_addr = ResolvedAddressMakeWild6(out_port);
  }
  // open the socket
  const int addr_family =
      (out_addr.address()->sa_family == AF_UNIX) ? AF_UNIX : AF_INET6;
  const int protocol = addr_family == AF_UNIX ? 0 : IPPROTO_TCP;
  SOCKET sock = WSASocket(addr_family, SOCK_STREAM, protocol, nullptr, 0,
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
  CHECK(!started_.exchange(true));
  grpc_core::MutexLock lock(&port_listeners_mu_);
  for (auto& port_listener : port_listeners_) {
    GRPC_RETURN_IF_ERROR(port_listener->Start());
  }
  return absl::OkStatus();
}

void WindowsEventEngineListener::ShutdownListeners() {
  grpc_core::MutexLock lock(&port_listeners_mu_);
  if (std::exchange(listeners_shutdown_, true)) return;
  // Shut down each port listener before destroying this EventEngine::Listener
  for (auto& port_listener : port_listeners_) {
    port_listener.reset();
  }
}

absl::StatusOr<WindowsEventEngineListener::SinglePortSocketListener*>
WindowsEventEngineListener::AddSinglePortSocketListener(
    SOCKET sock, EventEngine::ResolvedAddress addr) {
  auto single_port_listener =
      SinglePortSocketListener::Create(this, sock, addr);
  GRPC_RETURN_IF_ERROR(single_port_listener.status());
  auto* single_port_listener_ptr = single_port_listener->get();
  grpc_core::MutexLock lock(&port_listeners_mu_);
  port_listeners_.emplace_back(std::move(*single_port_listener));
  if (started_.load()) {
    LOG(ERROR) << "WindowsEventEngineListener::" << this
               << " Bind was called concurrently while the Listener was "
                  "starting. This is invalid usage, all ports must be bound "
                  "before the Listener is started.";
    GRPC_RETURN_IF_ERROR(single_port_listener_ptr->Start());
  }
  return single_port_listener_ptr;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
