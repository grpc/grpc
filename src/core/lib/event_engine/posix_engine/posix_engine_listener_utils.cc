// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <limits.h>
#include <stdlib.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep
#include "src/core/util/grpc_check.h"
#include "src/core/util/status_helper.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#include <errno.h>       // IWYU pragma: keep
#include <ifaddrs.h>     // IWYU pragma: keep
#include <netinet/in.h>  // IWYU pragma: keep
#include <sys/socket.h>  // IWYU pragma: keep
#include <unistd.h>      // IWYU pragma: keep
#endif

namespace grpc_event_engine::experimental {

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

namespace {

using ResolvedAddress =
    grpc_event_engine::experimental::EventEngine::ResolvedAddress;
using ListenerSocket = ListenerSocketsContainer::ListenerSocket;

#ifdef GRPC_HAVE_IFADDRS

bool SystemHasIfAddrs() { return true; }

#else  // GRPC_HAVE_IFADDRS

bool SystemHasIfAddrs() { return false; }

#endif  // GRPC_HAVE_IFADDRS

// Prepare socket options for a recently-created socket for listening.
absl::Status PrepareListenerSocketOptions(
    EventEnginePosixInterface* posix_interface, const PosixTcpOptions& options,
    ListenerSocket& socket) {
  FileDescriptor fd = socket.sock;
  GRPC_CHECK(fd.ready());
  bool close_fd = true;
  socket.port = 0;
  auto sock_cleanup =
      absl::MakeCleanup([&close_fd, fd, posix_interface]() -> void {
        if (close_fd && fd.ready()) {
          posix_interface->Close(fd);
        }
      });
  auto status = posix_interface->PrepareListenerSocketOptions(
      socket.sock, options, socket.addr);
  if (!status.ok()) {
    return status;
  }
  // No errors. Set close_fd to false to ensure the socket is not closed.
  close_fd = false;
  return absl::OkStatus();
}

absl::StatusOr<int> NewListenerContainerAddWildcardAddresses(
    EventEnginePosixInterface* posix_interface,
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    int requested_port,
    absl::AnyInvocable<bool(const ListenerSocket&)> socket_filter) {
  bool should_expand_wildcard_addrs =
      SystemHasIfAddrs() && options.expand_wildcard_addrs;
  if (should_expand_wildcard_addrs && socket_filter == nullptr) {
    // If there is no socket filter, we can just expand all local addresses.
    return ListenerContainerAddAllLocalAddresses(
        posix_interface, listener_sockets, options, requested_port);
  }

  ResolvedAddress wild4 = ResolvedAddressMakeWild4(requested_port);
  ResolvedAddress wild6 = ResolvedAddressMakeWild6(requested_port);
  absl::StatusOr<ListenerSocket> v6_sock = absl::InternalError("init");
  absl::StatusOr<ListenerSocket> v4_sock = absl::InternalError("init");
  bool add_v4_sock = true;

  // Try creating IPv6 socket first.
  v6_sock = CreateListenerSocketWithoutBinding(posix_interface, options, wild6);
  if (v6_sock.ok()) {
    if (v6_sock->dsmode == EventEnginePosixInterface::DSMODE_DUALSTACK ||
        v6_sock->dsmode == EventEnginePosixInterface::DSMODE_IPV4) {
      add_v4_sock = false;
    }
  }
  if (add_v4_sock) {
    // If we got a v6-only socket or nothing, try creating IPv4 socket.
    v4_sock =
        CreateListenerSocketWithoutBinding(posix_interface, options, wild4);
  }

  // If there is a socket filter, we only expand the wildcard addresses if
  // the socket filter returns true for either socket.
  if (should_expand_wildcard_addrs &&
      ((v6_sock.ok() && socket_filter(*v6_sock)) ||
       (v4_sock.ok() && socket_filter(*v4_sock)))) {
    // Close the wildcard sockets and expand all local addresses.
    if (v6_sock.ok()) {
      posix_interface->Close(v6_sock->sock);
    }
    if (v4_sock.ok()) {
      posix_interface->Close(v4_sock->sock);
    }
    return ListenerContainerAddAllLocalAddresses(
        posix_interface, listener_sockets, options, requested_port);
  }

  if (!v6_sock.ok() && !v4_sock.ok()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to add any wildcard listeners: ", v6_sock.status().message(),
        v4_sock.status().message()));
  }

  // If we are not expanding wildcard addresses, we now bind to each wildcard
  // socket and add them to the listener sockets container.
  int assigned_port = 0;
  if (v6_sock.ok()) {
    auto bind_status =
        BindListenerSocket(posix_interface, v6_sock->addr, *v6_sock);
    if (bind_status.ok()) {
      assigned_port = v6_sock->port;
      listener_sockets.Append(*v6_sock);
    } else {
      v6_sock = bind_status;
    }
  }
  if (v4_sock.ok()) {
    if (assigned_port != 0) {
      ResolvedAddressSetPort(v4_sock->addr, assigned_port);
    }
    auto bind_status =
        BindListenerSocket(posix_interface, v4_sock->addr, *v4_sock);
    if (bind_status.ok()) {
      assigned_port = v4_sock->port;
      listener_sockets.Append(*v4_sock);
    } else {
      v4_sock = bind_status;
    }
  }
  if (assigned_port > 0) return assigned_port;
  return absl::FailedPreconditionError(absl::StrCat(
      "Failed to add any wildcard listeners: ", v6_sock.status().message(),
      v4_sock.status().message()));
}

}  // namespace

absl::StatusOr<ListenerSocket> CreateAndPrepareListenerSocket(
    EventEnginePosixInterface* posix_interface, const PosixTcpOptions& options,
    const ResolvedAddress& addr) {
  auto result =
      CreateListenerSocketWithoutBinding(posix_interface, options, addr);
  GRPC_RETURN_IF_ERROR(result.status());
  GRPC_RETURN_IF_ERROR(
      BindListenerSocket(posix_interface, result->addr, *result));
  return result;
}

absl::StatusOr<ListenerSocket> CreateListenerSocketWithoutBinding(
    EventEnginePosixInterface* posix_interface, const PosixTcpOptions& options,
    const ResolvedAddress& addr) {
  ResolvedAddress addr4_copy;
  ListenerSocket socket;
  auto result = posix_interface->CreateDualStackSocket(
      nullptr, addr, SOCK_STREAM, 0, socket.dsmode);
  if (!result.ok()) {
    return result.status();
  }
  socket.sock = *result;
  if (socket.dsmode == EventEnginePosixInterface::DSMODE_IPV4 &&
      ResolvedAddressIsV4Mapped(addr, &addr4_copy)) {
    socket.addr = addr4_copy;
  } else {
    socket.addr = addr;
  }
  GRPC_RETURN_IF_ERROR(
      PrepareListenerSocketOptions(posix_interface, options, socket));
  return socket;
}

absl::Status BindListenerSocket(EventEnginePosixInterface* posix_interface,
                                const ResolvedAddress& addr,
                                ListenerSocket& socket) {
  FileDescriptor fd = socket.sock;
  GRPC_CHECK(fd.ready());
  bool close_fd = true;
  socket.port = 0;
  auto sock_cleanup =
      absl::MakeCleanup([&close_fd, fd, posix_interface]() -> void {
        if (close_fd && fd.ready()) {
          posix_interface->Close(fd);
        }
      });
  auto listen_address = posix_interface->BindListenerSocket(socket.sock, addr);
  if (!listen_address.ok()) {
    return std::move(listen_address).status();
  }
  socklen_t len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
  socket.port =
      ResolvedAddressGetPort(ResolvedAddress(listen_address->address(), len));
  close_fd = false;
  GRPC_CHECK_GT(socket.port, 0);
  return absl::OkStatus();
}

bool IsSockAddrLinkLocal(const EventEngine::ResolvedAddress* resolved_addr) {
  const sockaddr* addr = resolved_addr->address();
  if (addr->sa_family == AF_INET) {
    const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
    // Link-local IPv4 addresses are in the range 169.254.0.0/16
    if ((addr4->sin_addr.s_addr & htonl(0xFFFF0000)) == htonl(0xA9FE0000)) {
      return true;
    }
  } else if (addr->sa_family == AF_INET6) {
    const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
    const uint8_t* addr_bytes = addr6->sin6_addr.s6_addr;

    // Check the first 10 bits to make sure they are 1111 1110 10
    if ((addr_bytes[0] == 0xfe) && ((addr_bytes[1] & 0xc0) == 0x80)) {
      return true;
    }
  }

  return false;
}

absl::StatusOr<int> ListenerContainerAddAllLocalAddresses(
    EventEnginePosixInterface* posix_interface,
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    int requested_port) {
#ifdef GRPC_HAVE_IFADDRS
  absl::Status op_status = absl::OkStatus();
  struct ifaddrs* ifa = nullptr;
  struct ifaddrs* ifa_it;
  bool no_local_addresses = true;
  int assigned_port = 0;
  if (requested_port == 0) {
    auto result = posix_interface->GetUnusedPort();
    GRPC_RETURN_IF_ERROR(result.status());
    requested_port = *result;
    VLOG(2) << "Picked unused port " << requested_port;
  }
  if (getifaddrs(&ifa) != 0 || ifa == nullptr) {
    return absl::FailedPreconditionError(
        absl::StrCat("getifaddrs: ", std::strerror(errno)));
  }

  static const bool is_ipv4_available = [] {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) close(fd);
    return fd >= 0;
  }();

  for (ifa_it = ifa; ifa_it != nullptr; ifa_it = ifa_it->ifa_next) {
    ResolvedAddress addr;
    socklen_t len;
    const char* ifa_name = (ifa_it->ifa_name ? ifa_it->ifa_name : "<unknown>");
    if (ifa_it->ifa_addr == nullptr) {
      continue;
    } else if (ifa_it->ifa_addr->sa_family == AF_INET) {
      if (!is_ipv4_available) {
        continue;
      }
      len = static_cast<socklen_t>(sizeof(sockaddr_in));
    } else if (ifa_it->ifa_addr->sa_family == AF_INET6) {
      len = static_cast<socklen_t>(sizeof(sockaddr_in6));
    } else {
      continue;
    }
    addr = EventEngine::ResolvedAddress(ifa_it->ifa_addr, len);
    ResolvedAddressSetPort(addr, requested_port);
    std::string addr_str = *ResolvedAddressToString(addr);
    if (IsSockAddrLinkLocal(&addr)) {
      /* Exclude link-local addresses. */
      continue;
    }
    VLOG(2) << absl::StrFormat(
        "Adding local addr from interface %s flags 0x%x to server: %s",
        ifa_name, ifa_it->ifa_flags, addr_str.c_str());
    // We could have multiple interfaces with the same address (e.g.,
    // bonding), so look for duplicates.
    if (listener_sockets.Find(addr).ok()) {
      VLOG(2) << "Skipping duplicate addr " << addr_str << " on interface "
              << ifa_name;
      continue;
    }
    auto result =
        CreateAndPrepareListenerSocket(posix_interface, options, addr);
    if (!result.ok()) {
      op_status = absl::FailedPreconditionError(
          absl::StrCat("Failed to add listener: ", addr_str,
                       " due to error: ", result.status().message()));
      break;
    } else {
      listener_sockets.Append(*result);
      assigned_port = result->port;
      no_local_addresses = false;
    }
  }
  freeifaddrs(ifa);
  GRPC_RETURN_IF_ERROR(op_status);
  if (no_local_addresses) {
    return absl::FailedPreconditionError("No local addresses");
  }
  return assigned_port;

#else
  (void)listener_sockets;
  (void)options;
  (void)requested_port;
  grpc_core::Crash("System does not support ifaddrs");
#endif
}

absl::StatusOr<int> ListenerContainerAddWildcardAddresses(
    EventEnginePosixInterface* posix_interface,
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    int requested_port,
    absl::AnyInvocable<bool(const ListenerSocket&)> socket_filter) {
  if (grpc_core::IsWildcardIpExpansionRestrictionEnabled()) {
    return NewListenerContainerAddWildcardAddresses(
        posix_interface, listener_sockets, options, requested_port,
        std::move(socket_filter));
  }
  ResolvedAddress wild4 = ResolvedAddressMakeWild4(requested_port);
  ResolvedAddress wild6 = ResolvedAddressMakeWild6(requested_port);
  absl::StatusOr<ListenerSocket> v6_sock;
  absl::StatusOr<ListenerSocket> v4_sock;
  int assigned_port = 0;

  if (SystemHasIfAddrs() && options.expand_wildcard_addrs) {
    return ListenerContainerAddAllLocalAddresses(
        posix_interface, listener_sockets, options, requested_port);
  }

  // Try listening on IPv6 first.
  v6_sock = CreateAndPrepareListenerSocket(posix_interface, options, wild6);
  if (v6_sock.ok()) {
    listener_sockets.Append(*v6_sock);
    requested_port = v6_sock->port;
    assigned_port = v6_sock->port;
    if (v6_sock->dsmode == EventEnginePosixInterface::DSMODE_DUALSTACK ||
        v6_sock->dsmode == EventEnginePosixInterface::DSMODE_IPV4) {
      return v6_sock->port;
    }
  }
  // If we got a v6-only socket or nothing, try adding 0.0.0.0.
  ResolvedAddressSetPort(wild4, requested_port);
  v4_sock = CreateAndPrepareListenerSocket(posix_interface, options, wild4);
  if (v4_sock.ok()) {
    assigned_port = v4_sock->port;
    listener_sockets.Append(*v4_sock);
  }
  if (assigned_port > 0) {
    if (!v6_sock.ok()) {
      VLOG(2) << "Failed to add :: listener, the environment may not support "
                 "IPv6: "
              << v6_sock.status();
    }
    if (!v4_sock.ok()) {
      VLOG(2) << "Failed to add 0.0.0.0 listener, "
                 "the environment may not support IPv4: "
              << v4_sock.status();
    }
    return assigned_port;
  } else {
    GRPC_CHECK(!v6_sock.ok());
    GRPC_CHECK(!v4_sock.ok());
    return absl::FailedPreconditionError(absl::StrCat(
        "Failed to add any wildcard listeners: ", v6_sock.status().message(),
        v4_sock.status().message()));
  }
}

#else  // GRPC_POSIX_SOCKET_UTILS_COMMON

absl::StatusOr<ListenerSocketsContainer::ListenerSocket>
CreateAndPrepareListenerSocket(const PosixTcpOptions& /*options*/,
                               const grpc_event_engine::experimental::
                                   EventEngine::ResolvedAddress& /*addr*/) {
  grpc_core::Crash(
      "CreateAndPrepareListenerSocket is not supported on this platform");
}

absl::StatusOr<int> ListenerContainerAddWildcardAddresses(
    ListenerSocketsContainer& /*listener_sockets*/,
    const PosixTcpOptions& /*options*/, int /*requested_port*/) {
  grpc_core::Crash(
      "ListenerContainerAddWildcardAddresses is not supported on this "
      "platform");
}

absl::StatusOr<int> ListenerContainerAddAllLocalAddresses(
    ListenerSocketsContainer& /*listener_sockets*/,
    const PosixTcpOptions& /*options*/, int /*requested_port*/) {
  grpc_core::Crash(
      "ListenerContainerAddAllLocalAddresses is not supported on this "
      "platform");
}

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

}  // namespace grpc_event_engine::experimental
