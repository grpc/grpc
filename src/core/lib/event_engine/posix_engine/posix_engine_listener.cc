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

#include "src/core/lib/event_engine/posix_engine/posix_engine_listener.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/trace.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace posix_engine {

PosixEngineListener::PosixEngineListener(
    EventEngine::Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const grpc_event_engine::experimental::EndpointConfig& config,
    std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory>
        memory_allocator_factory,
    PosixEventPoller* poller, std::shared_ptr<EventEngine> engine)
    : on_accept_(std::move(on_accept)),
      on_shutdown_(std::move(on_shutdown)),
      options_(TcpOptionsFromEndpointConfig(config)),
      memory_allocator_factory_(std::move(memory_allocator_factory)),
      engine_(std::move(engine)),
      sockets_(this) {}

absl::StatusOr<int> PosixEngineListener::Bind(
    const EventEngine::ResolvedAddress& addr) {
  EventEngine::ResolvedAddress res_addr = addr;
  EventEngine::ResolvedAddress addr6_v4mapped;
  int requested_port = SockaddrGetPort(res_addr);
  absl::StatusOr<int> err;
  int assigned_port;
  absl::MutexLock lock(&this->mu_);
  GPR_ASSERT(!this->started_);
  GPR_ASSERT(addr.size() <= EventEngine::ResolvedAddress::MAX_SIZE_BYTES);
  UnlinkIfUnixDomainSocket(addr);

  /// Check if this is a wildcard port, and if so, try to keep the port the same
  /// as some previously created listener socket.
  for (auto it = sockets_.begin(); requested_port == 0 && it != sockets_.end();
       it++) {
    EventEngine::ResolvedAddress sockname_temp;
    socklen_t len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
    if (0 == getsockname(it->sock.Fd(),
                         const_cast<sockaddr*>(sockname_temp.address()),
                         &len)) {
      int used_port = SockaddrGetPort(sockname_temp);
      if (used_port > 0) {
        requested_port = used_port;
        SockaddrSetPort(res_addr, requested_port);
        break;
      }
    }
  }

  int used_port = SockaddrIsWildcard(res_addr);
  if (used_port > 0) {
    requested_port = used_port;
    return ListenerContainerAddWildcardAddresses(sockets_, options_,
                                                 requested_port);
  }
  if (SockaddrToV4Mapped(&res_addr, &addr6_v4mapped)) {
    res_addr = addr6_v4mapped;
  }

  auto result = CreateAndPrepareListenerSocket(options_, res_addr);
  if (result.ok()) {
    sockets_.Append(*result);
    return result->port;
  } else {
    return result.status();
  }
}

void PosixEngineListener::NotifyOnAccept(
    ListenerSocketsContainer::ListenerSocket* socket) {
  EventMgrEventEngineListener* listener = socket->listener;
  grpc_error_handle err;
  /* loop until accept4 returns EAGAIN,
      and then re-arm notification */
  for (;;) {
    grpc_resolved_address addr;
    memset(&addr, 0, sizeof(addr));
    addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
    /* Note: If we ever decide to return this address to the user,
    remember to strip off the ::ffff:0.0.0.0/96 prefix first. */
    int fd = grpc_accept4(socket->fd, &addr, 1, 1);
    if (fd < 0) {
      switch (errno) {
        case EINTR:
          continue;
        case EAGAIN:
          socket->desc_if->NotifyWhenReadable(::util::functional::ToCallback(
              [socket]() { socket->listener->NotifyOnAccept(socket); }));
          return;
        default:
          gpr_log(GPR_ERROR, "Failed accept4: %s", strerror(errno));
          socket->Unref();
          return;
      }
    }

    /* For UNIX sockets, the accept call might not fill up the member sun_path
     * of sockaddr_un, so explicitly call getsockname to get it. */
    if (grpc_is_unix_socket(&addr)) {
      memset(&addr, 0, sizeof(addr));
      addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
      if (getsockname(fd, reinterpret_cast<struct sockaddr*>(addr.addr),
                      &(addr.len)) < 0) {
        gpr_log(GPR_ERROR, "Failed getsockname: %s", strerror(errno));
        close(fd);
        socket->Unref();
        return;
      }
    }

    grpc_set_socket_no_sigpipe_if_possible(fd);

    err = grpc_apply_socket_mutator_in_args(
        fd, GRPC_FD_SERVER_CONNECTION_USAGE,
        listener->GetEndpointConfig().GetChannelArgs());
    if (err != GRPC_ERROR_NONE) {
      socket->Unref();
      return;
    }

    // TODO(vigneshbabu): Add tracing information here about incoming
    // connection.

    // Create an Endpoint here
    auto e = absl::make_unique<EventMgrEventEngineEndpoint>(
        fd, listener,
        listener->slice_allocator_factory_->CreateSliceAllocator(
            "TODO(vigneshbabu): get peer name"));
    // TODO(vigneshbabu): Figure out how to handle errors in
    // PopulateAddresses()
    e->PopulateAddresses();
    auto slice_allocator = e->GetSliceAllocator();
    // Call listener->on_accept_ with endpoint
    listener->on_accept_(std::move(e), *slice_allocator);
  }
  GPR_UNREACHABLE_CODE(return);
}

absl::Status PosixEngineListener::Start() {
  absl::MutexLock lock(&this->mu_);
  GPR_ASSERT(!this->started_);
  this->started_ = true;
  for (auto it = sockets_.begin(); it != sockets_.end(); it++) {
    (*it)->desc_if->NotifyWhenReadable(
        // TODO(vigneshbabu): Think about converting these callbacks to
        // permanent closures as well.
        ::util::functional::ToCallback(
            [socket = (*it)]() { socket->listener->NotifyOnAccept(socket); }));
  }
  return absl::OkStatus();
}

}  // namespace posix_engine
}  // namespace grpc_event_engine