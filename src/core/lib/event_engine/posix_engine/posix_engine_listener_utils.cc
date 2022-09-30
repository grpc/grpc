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

#include <cstring>
#include <string>

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

struct ListenerSocket {
  // Listener socket fd
  int fd;
  // Assigned/chosen listening port
  int port;
  // Socket configuration
  bool zero_copy_enabled;
  // Address at which the socket is listening for connections
  ResolvedAddress addr;
};

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
absl::Status PrepareSocket(bool reuse_port, ListenerSocket& socket) {
  ResolvedAddress sockname_temp;
  absl::Status error;

  int fd = socket.fd;
  grpc_resolved_address* addr = &socket.addr;
  GPR_ASSERT(fd >= 0);
  if (listener->IsReusePort() && !grpc_is_unix_socket(addr)) {
    err = grpc_set_socket_reuse_port(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
  }

#ifdef GRPC_LINUX_ERRQUEUE
  err = grpc_set_socket_zerocopy(fd);
  if (err != GRPC_ERROR_NONE) {
    /* it's not fatal, so just log it. */
    gpr_log(GPR_DEBUG, "Node does not support SO_ZEROCOPY, continuing.");
    GRPC_ERROR_UNREF(err);
  } else {
    socket.cfg.zero_copy_enabled = true;
  }
#else
  socket.cfg.zero_copy_enabled = false;
#endif
  err = grpc_set_socket_nonblocking(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  err = grpc_set_socket_cloexec(fd, 1);
  if (err != GRPC_ERROR_NONE) goto error;
  if (!grpc_is_unix_socket(addr)) {
    err = grpc_set_socket_low_latency(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
    err = grpc_set_socket_reuse_addr(fd, 1);
    if (err != GRPC_ERROR_NONE) goto error;
    err = grpc_set_socket_tcp_user_timeout(
        fd, listener->GetEndpointConfig().GetChannelArgs(),
        false /* is_client */);
    if (err != GRPC_ERROR_NONE) goto error;
  }
  err = grpc_set_socket_no_sigpipe_if_possible(fd);
  if (err != GRPC_ERROR_NONE) goto error;

  err = grpc_apply_socket_mutator_in_args(
      fd, GRPC_FD_SERVER_LISTENER_USAGE,
      listener->GetEndpointConfig().GetChannelArgs());

  if (err != GRPC_ERROR_NONE) goto error;

  if (bind(fd, reinterpret_cast<grpc_sockaddr*>(const_cast<char*>(addr->addr)),
           addr->len) < 0) {
    err = GRPC_OS_ERROR(errno, "bind");
    goto error;
  }

  if (listen(fd, GetMaxAcceptQueueSize()) < 0) {
    err = GRPC_OS_ERROR(errno, "listen");
    goto error;
  }

  sockname_temp.len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));

  if (getsockname(fd, reinterpret_cast<grpc_sockaddr*>(sockname_temp.addr),
                  &sockname_temp.len) < 0) {
    err = GRPC_OS_ERROR(errno, "getsockname");
    goto error;
  }

  socket.port = grpc_sockaddr_get_port(&sockname_temp);
  return GRPC_ERROR_NONE;

error:
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  if (fd >= 0) {
    close(fd);
  }
  grpc_error_handle ret =
      grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                             "Unable to configure socket", &err, 1),
                         GRPC_ERROR_INT_FD, fd);
  GRPC_ERROR_UNREF(err);
  return ret;
}

static grpc_error_handle AddSocketToListener(
    grpc_event_engine::experimental::EventMgrEventEngineListener* listener,
    grpc_event_engine::experimental::ListenerSocket& socket) {
  grpc_error_handle err = PrepareSocket(listener, socket);
  if (err == GRPC_ERROR_NONE) {
    GPR_ASSERT(socket.port > 0);
    listener->AddSocketLocked(&socket);
  }
  return err;
}
}  // namespace

grpc_error_handle AddWildCardAddrsToListener(
    EventMgrEventEngineListener* listener, int requested_port,
    int* assigned_port) {
  grpc_resolved_address wild4;
  grpc_resolved_address wild6;
  grpc_dualstack_mode dsmode;
  grpc_error_handle v6_err = GRPC_ERROR_NONE;
  grpc_error_handle v4_err = GRPC_ERROR_NONE;

  if (SystemHasIfAddrs() && listener->IsExpandWildcardAddrs()) {
    return ListenerAddAllLocalAddresses(listener, requested_port,
                                        assigned_port);
  }

  grpc_sockaddr_make_wildcards(requested_port, &wild4, &wild6);
  /* Try listening on IPv6 first. */
  if ((v6_err = ListenerAddAddress(listener, &wild6, &dsmode, assigned_port)) ==
      GRPC_ERROR_NONE) {
    if (dsmode == GRPC_DSMODE_DUALSTACK || dsmode == GRPC_DSMODE_IPV4) {
      return GRPC_ERROR_NONE;
    }
    requested_port = *assigned_port;
  }
  /* If we got a v6-only socket or nothing, try adding 0.0.0.0. */
  grpc_sockaddr_set_port(&wild4, requested_port);
  v4_err = ListenerAddAddress(listener, &wild4, &dsmode, assigned_port);
  if (*assigned_port > 0) {
    if (v6_err != GRPC_ERROR_NONE) {
      gpr_log(GPR_INFO,
              "Failed to add :: listener, "
              "the environment may not support IPv6: %s",
              grpc_error_std_string(v6_err).c_str());
      GRPC_ERROR_UNREF(v6_err);
    }
    if (v4_err != GRPC_ERROR_NONE) {
      gpr_log(GPR_INFO,
              "Failed to add 0.0.0.0 listener, "
              "the environment may not support IPv4: %s",
              grpc_error_std_string(v4_err).c_str());
      GRPC_ERROR_UNREF(v4_err);
    }
    return GRPC_ERROR_NONE;
  } else {
    grpc_error_handle root_err = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Failed to add any wildcard listeners");
    GPR_ASSERT(v6_err != GRPC_ERROR_NONE && v4_err != GRPC_ERROR_NONE);
    root_err = grpc_error_add_child(root_err, v6_err);
    root_err = grpc_error_add_child(root_err, v4_err);
    return root_err;
  }
}

grpc_error_handle ListenerAddAddress(EventMgrEventEngineListener* listener,
                                     grpc_resolved_address* addr,
                                     grpc_dualstack_mode* dsmode,
                                     int* assigned_port) {
  grpc_resolved_address addr4_copy;
  int fd;
  grpc_error_handle err =
      grpc_create_dualstack_socket(addr, SOCK_STREAM, 0, dsmode, &fd);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }
  ListenerSocket* socket = new ListenerSocket;
  if (*dsmode == GRPC_DSMODE_IPV4 &&
      grpc_sockaddr_is_v4mapped(addr, &addr4_copy)) {
    addr = &addr4_copy;
  }
  socket->fd = fd;
  socket->cfg.dsmode = *dsmode;
  socket->addr = *addr;
  socket->listener = listener;
  err = AddSocketToListener(listener, *socket);
  if (assigned_port) {
    *assigned_port = socket->port;
  }
  if (err == GRPC_ERROR_NONE) {
    socket->desc_if =
        listener->GetEventEngine()->event_manager_->RegisterFileDescriptor(
            socket->fd);
  } else {
    socket->Unref();
  }
  return err;
}

grpc_error_handle ListenerAddAllLocalAddresses(
    EventMgrEventEngineListener* listener, int requested_port,
    int* assigned_port) {
#ifdef GRPC_HAVE_IFADDRS
  struct ifaddrs* ifa = nullptr;
  struct ifaddrs* ifa_it;
  grpc_error_handle err = GRPC_ERROR_NONE;
  bool no_local_addresses = true;
  if (requested_port == 0) {
    if ((err = GetUnusedPort(&requested_port)) != GRPC_ERROR_NONE) {
      return err;
    } else if (requested_port <= 0) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad get_unused_port()");
    }
    gpr_log(GPR_DEBUG, "Picked unused port %d", requested_port);
  }
  if (getifaddrs(&ifa) != 0 || ifa == nullptr) {
    return GRPC_OS_ERROR(errno, "getifaddrs");
  }
  for (ifa_it = ifa; ifa_it != nullptr; ifa_it = ifa_it->ifa_next) {
    grpc_resolved_address addr;
    grpc_dualstack_mode dsmode;
    const char* ifa_name = (ifa_it->ifa_name ? ifa_it->ifa_name : "<unknown>");
    if (ifa_it->ifa_addr == nullptr) {
      continue;
    } else if (ifa_it->ifa_addr->sa_family == AF_INET) {
      addr.len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
    } else if (ifa_it->ifa_addr->sa_family == AF_INET6) {
      addr.len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
    } else {
      continue;
    }
    memcpy(addr.addr, ifa_it->ifa_addr, addr.len);
    if (!grpc_sockaddr_set_port(&addr, requested_port)) {
      /* Should never happen, because we check sa_family above. */
      err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to set port");
      break;
    }
    std::string addr_str = grpc_sockaddr_to_string(&addr, false);
    gpr_log(GPR_DEBUG,
            "Adding local addr from interface %s flags 0x%x to server: %s",
            ifa_name, ifa_it->ifa_flags, addr_str.c_str());
    /* We could have multiple interfaces with the same address (e.g., bonding),
       so look for duplicates. */
    if (listener->FindSocketLocked(&addr) != nullptr) {
      gpr_log(GPR_DEBUG, "Skipping duplicate addr %s on interface %s",
              addr_str.c_str(), ifa_name);
      continue;
    }
    if ((err = ListenerAddAddress(listener, &addr, &dsmode, assigned_port)) !=
        GRPC_ERROR_NONE) {
      grpc_error_handle root_err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("Failed to add listener: ", addr_str).c_str());
      err = grpc_error_add_child(root_err, err);
      break;
    } else {
      no_local_addresses = false;
    }
  }
  freeifaddrs(ifa);
  if (err != GRPC_ERROR_NONE) {
    return err;
  } else if (no_local_addresses) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No local addresses");
  } else {
    return GRPC_ERROR_NONE;
  }
#else
  gpr_log(GPR_ERROR, "System does not have support ifaddrs\n");
  GPR_ASSERT(0);
#endif
}

}  // namespace posix_engine
}  // namespace grpc_event_engine
