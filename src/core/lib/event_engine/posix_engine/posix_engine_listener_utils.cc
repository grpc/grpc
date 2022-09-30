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

#include <ifaddrs.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/status_helper.h"

#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

static gpr_once g_init_max_accept_queue_size = GPR_ONCE_INIT;
static int g_max_accept_queue_size;

namespace grpc_event_engine {
namespace posix_engine {

namespace {

using ResolvedAddress =
    ::grpc_event_engine::experimental::EventEngine::ResolvedAddress;

#ifdef GRPC_HAVE_IFADDRS

// Bind to "::" to get a port number not used by any address.
absl::StatusOr<int> GetUnusedPort() {
  ResolvedAddress wild = SockaddrMakeWild6(0);
  PosixSocketWrapper::DSMode dsmode;
  auto sock = PosixSocketWrapper::CreateDualStackSocket(nullptr, wild,
                                                        SOCK_STREAM, 0, dsmode);
  if (!sock.ok()) {
    return sock.status();
  }
  if (dsmode == PosixSocketWrapper::DSMode::DSMODE_IPV4) {
    wild = SockaddrMakeWild4(0);
  }
  if (bind(sock->Fd(), wild.address(), wild.size()) != 0) {
    close(sock->Fd());
    return absl::InternalError(
        absl::StrCat("bind(GetUnusedPort): ", std::strerror(errno)));
  }
  socklen_t len = wild.size();
  if (getsockname(sock->Fd(), const_cast<sockaddr*>(wild.address()), &len) !=
      0) {
    close(sock->Fd());
    return absl::InternalError(
        absl::StrCat("getsockname(GetUnusedPort): ", std::strerror(errno)));
  }
  close(sock->Fd());
  int port = SockaddrGetPort(wild);
  if (port <= 0) {
    return absl::InternalError("Bad port");
  }
  return port;
}

bool SystemHasIfAddrs() { return true; }

#else  // GRPC_HAVE_IFADDRS

bool SystemHasIfAddrs() { return false; }

#endif  // GRPC_HAVE_IFADDRS

// get max listen queue size on linux
int InitMaxAcceptQueueSize() {
  int n = SOMAXCONN;
  char buf[64];
  FILE* fp = fopen("/proc/sys/net/core/somaxconn", "r");
  int max_accept_queue_size;
  if (fp == nullptr) {
    // 2.4 kernel.
    return SOMAXCONN;
  }
  if (fgets(buf, sizeof buf, fp)) {
    char* end;
    long i = strtol(buf, &end, 10);
    if (i > 0 && i <= INT_MAX && end && *end == '\n') {
      n = static_cast<int>(i);
    }
  }
  fclose(fp);
  max_accept_queue_size = n;

  if (max_accept_queue_size < MIN_SAFE_ACCEPT_QUEUE_SIZE) {
    gpr_log(GPR_INFO,
            "Suspiciously small accept queue (%d) will probably lead to "
            "connection drops",
            max_accept_queue_size);
  }
  return max_accept_queue_size;
}

int GetMaxAcceptQueueSize() {
  static const int kMaxAcceptQueueSize = InitMaxAcceptQueueSize();
  return kMaxAcceptQueueSize;
}

// Prepare a recently-created socket for listening.
absl::Status PrepareSocket(const PosixTcpOptions& options,
                           ListenerSocket& socket) {
  ResolvedAddress sockname_temp;
  absl::Status error;
  int fd = socket.sock.Fd();
  GPR_ASSERT(fd >= 0);
  bool close_fd = true;
  socket.zero_copy_enabled = false;
  socket.port = 0;
  auto sock_cleanup = absl::MakeCleanup([&close_fd, &socket]() -> void {
    if (close_fd and socket.sock.Fd() >= 0) {
      close(socket.sock.Fd());
    }
  });
  if (PosixSocketWrapper::IsSocketReusePortSupported() &&
      options.allow_reuse_port && socket.addr.address()->sa_family != AF_UNIX) {
    RETURN_IF_NOT_OK(socket.sock.SetSocketReusePort(1));
  }

#ifdef GRPC_LINUX_ERRQUEUE
  if (!socket.sock.SetSocketZeroCopy().ok()) {
    // it's not fatal, so just log it.
    gpr_log(GPR_DEBUG, "Node does not support SO_ZEROCOPY, continuing.");
  } else {
    socket.zero_copy_enabled = true;
  }
#endif

  RETURN_IF_NOT_OK(socket.sock.SetSocketNonBlocking(1));
  RETURN_IF_NOT_OK(socket.sock.SetSocketCloexec(1));

  if (socket.addr.address()->sa_family != AF_UNIX) {
    RETURN_IF_NOT_OK(socket.sock.SetSocketLowLatency(1));
    RETURN_IF_NOT_OK(socket.sock.SetSocketReuseAddr(1));
    socket.sock.TrySetSocketTcpUserTimeout(options, false);
  }
  RETURN_IF_NOT_OK(socket.sock.SetSocketNoSigpipeIfPossible());
  RETURN_IF_NOT_OK(socket.sock.ApplySocketMutatorInOptions(
      GRPC_FD_SERVER_LISTENER_USAGE, options));

  if (bind(fd, socket.addr.address(), socket.addr.size()) < 0) {
    return absl::InternalError(
        absl::StrCat("Error in bind: ", std::strerror(errno)));
  }

  if (listen(fd, GetMaxAcceptQueueSize()) < 0) {
    return absl::InternalError(
        absl::StrCat("Error in listen: ", std::strerror(errno)));
  }
  socklen_t len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));

  if (getsockname(fd, const_cast<sockaddr*>(sockname_temp.address()), &len) <
      0) {
    return absl::InternalError(
        absl::StrCat("Error in getsockname: ", std::strerror(errno)));
  }

  socket.port = SockaddrGetPort(ResolvedAddress(sockname_temp.address(), len));
  // No errors. Set close_fd to false to ensure the socket is not closed.
  close_fd = false;
  return absl::OkStatus();
}

absl::Status AddSocketToListener(ListenerSocketsContainer& listener_sockets,
                                 const PosixTcpOptions& options,
                                 ListenerSocket& socket) {
  RETURN_IF_NOT_OK(PrepareSocket(options, socket));
  GPR_ASSERT(socket.port > 0);
  listener_sockets.AddSocket(socket);
  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<int> ListenerAddAddress(
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
    PosixSocketWrapper::DSMode& dsmode) {
  ResolvedAddress addr4_copy;
  int fd;
  ListenerSocket socket;
  auto result = PosixSocketWrapper::CreateDualStackSocket(
      nullptr, addr, SOCK_STREAM, 0, socket.dsmode);
  if (!result.ok()) {
    return result.status();
  }
  socket.sock = *result;
  if (socket.dsmode == PosixSocketWrapper::DSMODE_IPV4 &&
      SockaddrIsV4Mapped(&addr, &addr4_copy)) {
    socket.addr = addr4_copy;
  } else {
    socket.addr = addr;
  }

  RETURN_IF_NOT_OK(AddSocketToListener(listener_sockets, options, socket));
  dsmode = socket.dsmode;
  return socket.port;
}

absl::StatusOr<int> ListenerAddAllLocalAddresses(
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    int requested_port) {
#ifdef GRPC_HAVE_IFADDRS
  absl::Status status;
  struct ifaddrs* ifa = nullptr;
  struct ifaddrs* ifa_it;
  bool no_local_addresses = true;
  int assigned_port = 0;
  if (requested_port == 0) {
    auto result = GetUnusedPort();
    if (!result.ok()) {
      return result.status();
    } else if (*result <= 0) {
      return absl::InternalError("Bad get_unused_port()");
    }
    requested_port = *result;
    gpr_log(GPR_DEBUG, "Picked unused port %d", requested_port);
  }
  if (getifaddrs(&ifa) != 0 || ifa == nullptr) {
    return absl::InternalError(
        absl::StrCat("getifaddrs: ", std::strerror(errno)));
  }
  for (ifa_it = ifa; ifa_it != nullptr; ifa_it = ifa_it->ifa_next) {
    ResolvedAddress addr;
    PosixSocketWrapper::DSMode dsmode;
    socklen_t len;
    const char* ifa_name = (ifa_it->ifa_name ? ifa_it->ifa_name : "<unknown>");
    if (ifa_it->ifa_addr == nullptr) {
      continue;
    } else if (ifa_it->ifa_addr->sa_family == AF_INET) {
      len = static_cast<socklen_t>(sizeof(sockaddr_in));
    } else if (ifa_it->ifa_addr->sa_family == AF_INET6) {
      len = static_cast<socklen_t>(sizeof(sockaddr_in6));
    } else {
      continue;
    }
    memcpy(const_cast<sockaddr*>(addr.address()), ifa_it->ifa_addr, len);
    if (!SockaddrSetPort(addr, requested_port)) {
      // Should never happen, because we check sa_family above.
      status = absl::InternalError("Failed to set port");
      break;
    }
    std::string addr_str = *SockaddrToString(&addr, false);
    gpr_log(GPR_DEBUG,
            "Adding local addr from interface %s flags 0x%x to server: %s",
            ifa_name, ifa_it->ifa_flags, addr_str.c_str());
    // We could have multiple interfaces with the same address (e.g., bonding),
    // so look for duplicates.
    if (listener_sockets.FindSocket(addr).ok()) {
      gpr_log(GPR_DEBUG, "Skipping duplicate addr %s on interface %s",
              addr_str.c_str(), ifa_name);
      continue;
    }
    auto result = ListenerAddAddress(listener_sockets, options, addr, dsmode);
    if (!result.ok()) {
      status = absl::InternalError(
          absl::StrCat("Failed to add listener: ", addr_str,
                       " due to error: ", result.status().message()));
      break;
    } else {
      assigned_port = *result;
      no_local_addresses = false;
    }
  }
  freeifaddrs(ifa);
  if (!status.ok()) {
    return status;
  } else if (no_local_addresses) {
    return absl::InternalError("No local addresses");
  } else {
    return assigned_port;
  }
#else
  GPR_ASSERT(false && "System does not support ifaddrs");
#endif
}

absl::StatusOr<int> AddWildCardAddrsToListener(
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    int requested_port) {
  ResolvedAddress wild4 = SockaddrMakeWild4(requested_port);
  ResolvedAddress wild6 = SockaddrMakeWild6(requested_port);
  PosixSocketWrapper::DSMode dsmode;
  absl::StatusOr<int> v6_err = absl::OkStatus();
  absl::StatusOr<int> v4_err = absl::OkStatus();
  int assigned_port = 0;

  if (SystemHasIfAddrs() && options.expand_wildcard_addrs) {
    return ListenerAddAllLocalAddresses(listener_sockets, options,
                                        requested_port);
  }

  // Try listening on IPv6 first.
  v6_err = ListenerAddAddress(listener_sockets, options, wild6, dsmode);
  if (v6_err.ok()) {
    if (dsmode == PosixSocketWrapper::DSMODE_DUALSTACK ||
        dsmode == PosixSocketWrapper::DSMODE_IPV4) {
      return *v6_err;
    }
    requested_port = *v6_err;
    assigned_port = *v6_err;
  }
  // If we got a v6-only socket or nothing, try adding 0.0.0.0.
  SockaddrSetPort(wild4, requested_port);
  v4_err = ListenerAddAddress(listener_sockets, options, wild4, dsmode);
  if (assigned_port > 0) {
    if (!v6_err.ok()) {
      gpr_log(
          GPR_INFO,
          "Failed to add :: listener, the environment may not support IPv6: %s",
          v6_err.status().ToString().c_str());
    }
    if (!v4_err.ok()) {
      gpr_log(GPR_INFO,
              "Failed to add 0.0.0.0 listener, "
              "the environment may not support IPv4: %s",
              v4_err.status().ToString().c_str());
    }
    return assigned_port;
  } else {
    GPR_ASSERT(!v6_err.ok());
    GPR_ASSERT(!v4_err.ok());
    return absl::InternalError(absl::StrCat(
        "Failed to add any wildcard listeners: ", v6_err.status().message(),
        v4_err.status().message()));
  }
}

}  // namespace posix_engine
}  // namespace grpc_event_engine
