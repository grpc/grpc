// Copyright 2022 gRPC Authors
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP

#include <errno.h>       // IWYU pragma: keep
#include <sys/socket.h>  // IWYU pragma: keep
#include <unistd.h>      // IWYU pragma: keep

#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_endpoint.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_listener.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/socket_mutator.h"

namespace grpc_event_engine {
namespace experimental {

PosixEngineListenerImpl::PosixEngineListenerImpl(
    EventEngine::Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const grpc_event_engine::experimental::EndpointConfig& config,
    std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory>
        memory_allocator_factory,
    PosixEventPoller* poller, std::shared_ptr<EventEngine> engine)
    : poller_(poller),
      options_(TcpOptionsFromEndpointConfig(config)),
      engine_(std::move(engine)),
      acceptors_(this),
      on_accept_(std::move(on_accept)),
      on_shutdown_(std::move(on_shutdown)),
      memory_allocator_factory_(std::move(memory_allocator_factory)) {}

absl::StatusOr<int> PosixEngineListenerImpl::Bind(
    const EventEngine::ResolvedAddress& addr) {
  EventEngine::ResolvedAddress res_addr = addr;
  EventEngine::ResolvedAddress addr6_v4mapped;
  int requested_port = ResolvedAddressGetPort(res_addr);
  absl::MutexLock lock(&this->mu_);
  GPR_ASSERT(!this->started_);
  GPR_ASSERT(addr.size() <= EventEngine::ResolvedAddress::MAX_SIZE_BYTES);
  UnlinkIfUnixDomainSocket(addr);

  /// Check if this is a wildcard port, and if so, try to keep the port the same
  /// as some previously created listener socket.
  for (auto it = acceptors_.begin();
       requested_port == 0 && it != acceptors_.end(); it++) {
    EventEngine::ResolvedAddress sockname_temp;
    socklen_t len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
    if (0 == getsockname((*it)->Socket().sock.Fd(),
                         const_cast<sockaddr*>(sockname_temp.address()),
                         &len)) {
      int used_port = ResolvedAddressGetPort(sockname_temp);
      if (used_port > 0) {
        requested_port = used_port;
        ResolvedAddressSetPort(res_addr, requested_port);
        break;
      }
    }
  }

  auto used_port = ResolvedAddressIsWildcard(res_addr);
  if (used_port.has_value()) {
    requested_port = *used_port;
    return ListenerContainerAddWildcardAddresses(acceptors_, options_,
                                                 requested_port);
  }
  if (ResolvedAddressToV4Mapped(res_addr, &addr6_v4mapped)) {
    res_addr = addr6_v4mapped;
  }

  auto result = CreateAndPrepareListenerSocket(options_, res_addr);
  GRPC_RETURN_IF_ERROR(result.status());
  acceptors_.Append(*result);
  return result->port;
}

void PosixEngineListenerImpl::AsyncConnectionAcceptor::Start() {
  Ref();
  handle_->NotifyOnRead(notify_on_accept_);
}

void PosixEngineListenerImpl::AsyncConnectionAcceptor::NotifyOnAccept(
    absl::Status status) {
  if (!status.ok()) {
    // Shutting down the acceptor. Unref the ref grabbed in
    // AsyncConnectionAcceptor::Start().
    Unref();
    return;
  }
  // loop until accept4 returns EAGAIN, and then re-arm notification.
  for (;;) {
    EventEngine::ResolvedAddress addr;
    memset(const_cast<sockaddr*>(addr.address()), 0, addr.size());
    // Note: If we ever decide to return this address to the user, remember to
    // strip off the ::ffff:0.0.0.0/96 prefix first.
    int fd = Accept4(handle_->WrappedFd(), addr, 1, 1);
    if (fd < 0) {
      switch (errno) {
        case EINTR:
          continue;
        case EAGAIN:
        case ECONNABORTED:
          handle_->NotifyOnRead(notify_on_accept_);
          return;
        default:
          gpr_log(GPR_ERROR, "Closing acceptor. Failed accept4: %s",
                  strerror(errno));
          // Shutting down the acceptor. Unref the ref grabbed in
          // AsyncConnectionAcceptor::Start().
          Unref();
          return;
      }
    }

    // For UNIX sockets, the accept call might not fill up the member
    // sun_path of sockaddr_un, so explicitly call getsockname to get it.
    if (addr.address()->sa_family == AF_UNIX) {
      socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
      if (getsockname(fd, const_cast<sockaddr*>(addr.address()), &len) < 0) {
        gpr_log(GPR_ERROR, "Closing acceptor. Failed getsockname: %s",
                strerror(errno));
        close(fd);
        // Shutting down the acceptor. Unref the ref grabbed in
        // AsyncConnectionAcceptor::Start().
        Unref();
        return;
      }
    }

    PosixSocketWrapper sock(fd);
    (void)sock.SetSocketNoSigpipeIfPossible();
    auto result = sock.ApplySocketMutatorInOptions(
        GRPC_FD_SERVER_CONNECTION_USAGE, listener_->options_);
    if (!result.ok()) {
      gpr_log(GPR_ERROR, "Closing acceptor. Failed to apply socket mutator: %s",
              result.ToString().c_str());
      // Shutting down the acceptor. Unref the ref grabbed in
      // AsyncConnectionAcceptor::Start().
      Unref();
      return;
    }

    // Create an Endpoint here.
    std::string peer_name = *ResolvedAddressToNormalizedString(addr);
    auto endpoint = CreatePosixEndpoint(
        /*handle=*/listener_->poller_->CreateHandle(
            fd, peer_name, listener_->poller_->CanTrackErrors()),
        /*on_shutdown=*/nullptr, /*engine=*/listener_->engine_,
        /*allocator=*/
        listener_->memory_allocator_factory_->CreateMemoryAllocator(
            absl::StrCat("endpoint-tcp-server-connection: ", peer_name)),
        /*options=*/listener_->options_);
    // Call on_accept_ and then resume accepting new connections by continuing
    // the parent for-loop.
    listener_->on_accept_(
        std::move(endpoint),
        listener_->memory_allocator_factory_->CreateMemoryAllocator(
            absl::StrCat("on-accept-tcp-server-connection: ", peer_name)));
  }
  GPR_UNREACHABLE_CODE(return);
}

void PosixEngineListenerImpl::AsyncConnectionAcceptor::Shutdown() {
  // The ShutdownHandle whould trigger any waiting notify_on_accept_ to get
  // scheduled with the not-OK status.
  handle_->ShutdownHandle(absl::InternalError("Shutting down acceptor"));
  Unref();
}

absl::Status PosixEngineListenerImpl::Start() {
  absl::MutexLock lock(&this->mu_);
  // Start each asynchronous acceptor.
  GPR_ASSERT(!this->started_);
  this->started_ = true;
  for (auto it = acceptors_.begin(); it != acceptors_.end(); it++) {
    (*it)->Start();
  }
  return absl::OkStatus();
}

void PosixEngineListenerImpl::TriggerShutdown() {
  // This would get invoked from the destructor of the parent
  // PosixEngineListener object.
  absl::MutexLock lock(&this->mu_);
  for (auto it = acceptors_.begin(); it != acceptors_.end(); it++) {
    // Trigger shutdown of each asynchronous acceptor. This in-turn calls
    // ShutdownHandle on the associated poller event handle. It may also
    // immediately delete the asynchronous acceptor if the acceptor was never
    // started.
    (*it)->Shutdown();
  }
}

PosixEngineListenerImpl::~PosixEngineListenerImpl() {
  // This should get invoked only after all the AsyncConnectionAcceptor's have
  // been destroyed. This is because each AsyncConnectionAcceptor has a
  // shared_ptr ref to the parent PosixEngineListenerImpl.
  if (on_shutdown_ != nullptr) {
    on_shutdown_(absl::InternalError("Shutting down listener"));
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_POSIX_SOCKET_TCP
