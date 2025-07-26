// Copyright 2025 The gRPC Authors
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

#ifdef GPR_APPLE
#include <AvailabilityMacros.h>
#ifdef AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER

#include <netinet/in.h>
#include <sys/socket.h>

#include "src/core/lib/debug/trace_flags.h"
#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/event_engine/cf_engine/cfsocket_listener.h"
#include "src/core/lib/event_engine/cf_engine/cfstream_endpoint.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/util/strerror.h"

namespace grpc_event_engine::experimental {

namespace {

auto GetCfSocketResolvedAddress6(CFSocketRef ipv6cfsock) {
  CFTypeUniqueRef<CFDataRef> sin6cfd = CFSocketCopyAddress(ipv6cfsock);
  auto sin6 = reinterpret_cast<const sockaddr*>(CFDataGetBytePtr(sin6cfd));
  return EventEngine::ResolvedAddress{sin6, sin6->sa_len};
}

}  // namespace

CFSocketListenerImpl::CFSocketListenerImpl(
    std::shared_ptr<CFEventEngine> engine,
    EventEngine::Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
    : engine_(std::move(engine)),
      on_accept_(std::move(on_accept)),
      on_shutdown_(std::move(on_shutdown)),
      memory_allocator_factory_(std::move(memory_allocator_factory)) {}

CFSocketListenerImpl::~CFSocketListenerImpl() {
  on_shutdown_(absl::OkStatus());
  dispatch_release(queue_);

  GRPC_TRACE_LOG(event_engine, INFO)
      << "CFSocketListenerImpl::~CFSocketListenerImpl, this: " << this;
}

void CFSocketListenerImpl::Shutdown() {
  GRPC_TRACE_LOG(event_engine, INFO)
      << "CFSocketListenerImpl::Shutdown: this: " << this;

  grpc_core::MutexLock lock(&mu_);

  shutdown_ = true;
  for (auto& [ipv6cfsock] : ipv6cfsocks_) {
    CFSocketInvalidate(ipv6cfsock);
  }
  if (runloop_) {
    CFRunLoopWakeUp(runloop_);
    CFRunLoopStop(runloop_);
  }
}

absl::StatusOr<int> CFSocketListenerImpl::Bind(
    const EventEngine::ResolvedAddress& addr) {
  grpc_core::MutexLock lock(&mu_);

  if (started_) {
    return absl::FailedPreconditionError(
        "Listener is already started, ports can no longer be bound");
  }
  CHECK(addr.size() <= EventEngine::ResolvedAddress::MAX_SIZE_BYTES);

  int port = ResolvedAddressGetPort(addr);
  // if port is not specified, reuse any listening port
  if (port == 0) {
    for (auto& [ipv6cfsock] : ipv6cfsocks_) {
      auto bind_addr = GetCfSocketResolvedAddress6(ipv6cfsock);
      int bind_port = ResolvedAddressGetPort(bind_addr);
      if (bind_port > 0) {
        port = bind_port;
        break;
      }
    }
  }

  EventEngine::ResolvedAddress addr6;
  // treat :: or 0.0.0.0 as a family-agnostic wildcard.
  if (MaybeGetWildcardPortFromAddress(addr).has_value()) {
    addr6 = ResolvedAddressMakeWild6(port);
  }
  // convert to ipv6 if needed
  if (!ResolvedAddressToV4Mapped(addr, &addr6)) {
    addr6 = addr;
  }
  // update port
  ResolvedAddressSetPort(addr6, port);

  // open socket
  CFSocketContext ipv6cfsock_context = {0, this, Retain, Release, nullptr};
  auto ipv6cfsock = CFSocketCreate(kCFAllocatorDefault, PF_INET6, SOCK_STREAM,
                                   IPPROTO_TCP, kCFSocketAcceptCallBack,
                                   handleConnect, &ipv6cfsock_context);

  // allow reuse of the address and port
  auto sin6_fd = CFSocketGetNative(ipv6cfsock);
  int sock_flag = 1;
  int err = setsockopt(sin6_fd, SOL_SOCKET, SO_REUSEADDR, &sock_flag,
                       sizeof(sock_flag));
  if (err != 0) {
    return absl::InternalError(absl::StrCat(
        "CFSocketListenerImpl::Bind, setsockopt(SO_REUSEADDR) errors: ",
        grpc_core::StrError(errno)));
  }
  err = setsockopt(sin6_fd, SOL_SOCKET, SO_REUSEPORT, &sock_flag,
                   sizeof(sock_flag));
  if (err != 0) {
    return absl::InternalError(absl::StrCat(
        "CFSocketListenerImpl::Bind, setsockopt(SO_REUSEPORT) errors: ",
        grpc_core::StrError(errno)));
  }

  // bind socket to address
  CFTypeUniqueRef<CFDataRef> sin6cfd =
      CFDataCreate(kCFAllocatorDefault, (UInt8*)addr6.address(), addr6.size());
  CFSocketError cf_error = CFSocketSetAddress(ipv6cfsock, sin6cfd);
  if (cf_error != kCFSocketSuccess) {
    return absl::InternalError(absl::StrCat(
        "CFSocketListenerImpl::Bind, CFSocketSetAddress error: ", cf_error));
  }

  // find actual bind address and port
  auto bind_addr = GetCfSocketResolvedAddress6(ipv6cfsock);
  int bind_port = ResolvedAddressGetPort(bind_addr);

  ipv6cfsocks_.emplace_back(ipv6cfsock);

  GRPC_TRACE_LOG(event_engine, INFO)
      << "CFSocketListenerImpl::Bind, addr: "
      << ResolvedAddressToString(addr).value_or("")
      << ", bind_addr: " << ResolvedAddressToString(bind_addr).value_or("")
      << ", this: " << this;

  return bind_port;
}

absl::Status CFSocketListenerImpl::Start() {
  grpc_core::MutexLock lock(&mu_);

  CHECK(!started_);
  started_ = true;

  dispatch_async_f(queue_, Ref().release(), [](void* thatPtr) {
    grpc_core::RefCountedPtr<CFSocketListenerImpl> that{
        static_cast<CFSocketListenerImpl*>(thatPtr)};

    GRPC_TRACE_LOG(event_engine, INFO)
        << "CFSocketListenerImpl::Start, running CFRunLoop"
        << ", this: " << thatPtr;

    {
      grpc_core::MutexLock lock(&that->mu_);
      if (that->shutdown_) {
        return;
      }

      that->runloop_ = CFRunLoopGetCurrent();
      for (auto& [ipv6cfsock] : that->ipv6cfsocks_) {
        CFTypeUniqueRef<CFRunLoopSourceRef> ipv6cfsock_source =
            CFSocketCreateRunLoopSource(kCFAllocatorDefault, ipv6cfsock, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), ipv6cfsock_source,
                           kCFRunLoopDefaultMode);
      }
    }

    CFRunLoopRun();

    GRPC_TRACE_LOG(event_engine, INFO)
        << "CFSocketListenerImpl::Start, CFRunLoop stopped"
        << ", this: " << thatPtr;
  });

  return absl::OkStatus();
}

/* static */
void CFSocketListenerImpl::handleConnect(CFSocketRef s,
                                         CFSocketCallBackType type,
                                         CFDataRef address, const void* data,
                                         void* info) {
  CFSocketListenerImpl* self = static_cast<CFSocketListenerImpl*>(info);

  switch (type) {
    case kCFSocketAcceptCallBack: {
      std::string peer_name = "unknown";
      auto peer_sin6 =
          reinterpret_cast<const sockaddr*>(CFDataGetBytePtr(address));
      auto peer_addr =
          EventEngine::ResolvedAddress(peer_sin6, peer_sin6->sa_len);
      auto addr_uri = ResolvedAddressToURI(peer_addr);
      if (!addr_uri.ok()) {
        GRPC_TRACE_LOG(event_engine, ERROR)
            << "invalid peer name: " << addr_uri.status() << ", this: " << self;
      } else {
        peer_name = *addr_uri;
      }

      auto socketHandle = *static_cast<const CFSocketNativeHandle*>(data);
      auto endpoint = std::make_unique<CFStreamEndpoint>(
          self->engine_,
          self->memory_allocator_factory_->CreateMemoryAllocator(
              absl::StrCat("endpoint-tcp-server-connection: ", peer_name)));

      endpoint->AcceptSocket(
          [that = self->Ref(), socketHandle, peer_name = std::move(peer_name),
           endpoint = std::move(endpoint)](absl::Status status) mutable {
            if (!status.ok()) {
              GRPC_TRACE_LOG(event_engine, ERROR)
                  << "CFSocketListenerImpl::handleConnect, accept failed: "
                  << status << ", this: " << that.get();
              return;
            }

            that->on_accept_(
                std::move(endpoint),
                that->memory_allocator_factory_->CreateMemoryAllocator(
                    absl::StrCat("on-accept-tcp-server-connection: ",
                                 peer_name)));
            GRPC_TRACE_LOG(event_engine, INFO)
                << "CFSocketListenerImpl::handleConnect, accepted socket: "
                << socketHandle << ", peer_name: " << peer_name
                << ", this: " << that.get();
          },
          socketHandle, peer_addr);

      break;
    }
    default:
      GRPC_TRACE_LOG(event_engine, ERROR)
          << "CFSocketListenerImpl::handleConnect, unexpected type: " << type
          << ", this: " << self;
      break;
  }
}

}  // namespace grpc_event_engine::experimental

#endif  // AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER
#endif  // GPR_APPLE
