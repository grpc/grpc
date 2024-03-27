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

#include "src/core/lib/event_engine/posix_engine/posix_engine_systemd.h"

#include "absl/strings/match.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/iomgr/port.h"

// copied from tcp_socket_utils for sockaddr_un
#ifdef GRPC_HAVE_UNIX_SOCKET
#ifdef GPR_WINDOWS
// clang-format off
#include <ws2def.h>
#include <afunix.h>
// clang-format on
#else
#include <sys/un.h>
#endif  // GPR_WINDOWS
#endif  // GRPC_HAVE_UNIX_SOCKET

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif  // HAVE_LIBSYSTEMD

namespace grpc_event_engine {
namespace experimental {

#ifdef HAVE_LIBSYSTEMD

// Checks if sockets were provided by systemd socket activation.
//
// IMPORTANT: Every file descriptor provided at startup must remain *open*
// for the call to sd_listen_fds() to not fail with EBADF.
//
// REASON: As seen in Systemd source code `sd-daemon.c`, the sd_listen_fds()
//   function will exit with errno=EBADF(9) if *ANY* of the provided fd
//   are closed. This is because sd_listen_fds() has side effects
//   in fd_cloexec : fcntl F_GETFD/F_SETFD do fail on closed fd,
//   doing an early return with the errno value, which bubbles up.
//
// WORKAROUND: do *not* close Systemd provided file descriptors.
//   Systemd advises in its manual that you should not close them
//   anyway (nor calling shutdown) so prevent these actions in pollers.
//
// In any case, return an error if it happens instead of hiding it.
absl::StatusOr<int> GetSystemdPreallocatedFdCount() {
  int result = sd_listen_fds(0);
  if (result < 0) {
    return absl::InternalError(absl::StrFormat(
        "GetSystemdPreallocatedFdCount: sd_listen_fds() failed: %s. Could not "
        "get number of preallocated Systemd file descriptors, maybe some of the"
        "preallocated file descriptors have been closed since startup ?",
        grpc_core::StrError(result).c_str()));
  }
  return result;
}

bool IsSystemdPreallocatedFdOrLogErrorsWithFalseFallback(int fd) {
    auto result = IsSystemdPreallocatedFd(fd);
    // In case no error can bubble up, so log it
    if (!result.ok()) {
      gpr_log(GPR_ERROR, result.status().ToString().c_str());
    }
    // And let the caller know that we consider that
    // the file descriptors were *not* preallocated.
    return result.value_or(false);
}

absl::StatusOr<bool> IsSystemdPreallocatedFd(int fd) {
  auto result = GetSystemdPreallocatedFdCount();
  GRPC_RETURN_IF_ERROR(result.status());
  return SD_LISTEN_FDS_START <= fd && fd < SD_LISTEN_FDS_START + result.value();
}

absl::optional<int> MaybeGetSockOptIntValue(int fd, int level, int optname) {
  int value;
  socklen_t value_size = sizeof(value);
  getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &value, &value_size);
  if (value == -1) {
    gpr_log(GPR_ERROR,
            "MaybeGetSockOptIntValue: getsockopt(%i, %i, %i) failed: %s", fd,
            level, optname, grpc_core::StrError(errno).c_str());
    return absl::nullopt;
  }
  return value;
}

absl::optional<int> MaybeGetSystemdPreallocatedFdFromNetAddr(
    int fd, const EventEngine::ResolvedAddress& addr) {
  // Checks provided addr against systemd info
  if (sd_is_socket_sockaddr(fd, SOCK_STREAM, addr.address(), addr.size(), 1)) {
    return fd;
  }
  return absl::nullopt;
}

absl::optional<int> MaybeGetSystemdPreallocatedFdFromUnixAbstractAddr(
    int fd, const EventEngine::ResolvedAddress& addr) {
  // abstract unix path are in the form of "\0name"
  auto sa_un = reinterpret_cast<const struct sockaddr_un*>(addr.address());
  int path_len = addr.size() - sizeof(sa_un->sun_family);

  // Checks that the provided addr path matches the information systemd
  // has about that abstract unix socket, with length including first null
  if (sd_is_socket_unix(fd, SOCK_STREAM, 1, sa_un->sun_path, path_len)) {
    return fd;
  }

  return absl::nullopt;
}

absl::optional<int> MaybeGetSystemdPreallocatedFdFromUnixNormalAddr(
    int fd, const EventEngine::ResolvedAddress& addr) {
  auto sa_un = reinterpret_cast<const struct sockaddr_un*>(addr.address());
  absl::string_view addr_un_path(sa_un->sun_path);

  // Path provided in the form of `unix:///foo/bar` were transformed into
  // `///foo/bar` in Chttp2ServerAddPort, so clean it up into `/foo/bar`
  // in order to have a possible match with systemd "natural" full path.
  if (absl::StartsWith(addr_un_path, "///")) {
    addr_un_path.remove_prefix(2);
  }

  // Path provided in the form of `unix://foo/bar` are invalid according
  // to Naming documentation, and were transformed into `//foo/bar`
  /// in Chttp2ServerAddPort, so reject them explicitely here
  if (absl::StartsWith(addr_un_path, "//")) {
    gpr_log(GPR_ERROR, "Invalid address: %s (check the number of /)",
            addr_un_path.data());
    return absl::nullopt;
  }

  // Path provided in the form of `unix:relative`, `unix:./relative`, or
  // `unix:../relative`, are relative (in nature) and cannot be matched
  // to systemd socket information. Systemd which requires an absolute
  // path as per `man 5 systemd-socket` for the `ListenSocket` option.
  char buffer[PATH_MAX];
  if (!absl::StartsWith(addr_un_path, "/")) {
    // Rebuilds an absolute path from the current working directory. This
    // is not a foolproof method, as there could be symlinks/ or hardlinks
    // making it that the path from systemd.socket could be different from
    // the absolute path after it has been rebuilt from the relative path.
    if (realpath(addr_un_path.data(), buffer) == nullptr) {
      gpr_log(GPR_ERROR, "realpath(%s) failed: %s", addr_un_path.data(),
              grpc_core::StrError(errno).c_str());
      return absl::nullopt;
    }
    addr_un_path = absl::string_view(buffer);
  }

  // Checks that the provided addr path matches the information systemd
  // has about that normal unix socket. length=0 for normal unix sockets.
  if (sd_is_socket_unix(fd, SOCK_STREAM, 1, addr_un_path.data(), 0)) {
    return fd;
  }

  return absl::nullopt;
}

absl::optional<int> MaybeGetSystemdPreallocatedFdFromUnixAddr(
    int fd, const EventEngine::ResolvedAddress& addr) {
  auto sa_un = reinterpret_cast<const struct sockaddr_un*>(addr.address());
  if (sa_un->sun_path[0] == '\0') {
    return MaybeGetSystemdPreallocatedFdFromUnixAbstractAddr(fd, addr);
  } else {
    return MaybeGetSystemdPreallocatedFdFromUnixNormalAddr(fd, addr);
  }
}

absl::StatusOr<absl::optional<int>> MaybeGetSystemdPreallocatedFdFromAddr(
    const EventEngine::ResolvedAddress& addr) {
  auto result = GetSystemdPreallocatedFdCount();
  GRPC_RETURN_IF_ERROR(result.status());

  int sd_fd_count = result.value();
  gpr_log(GPR_DEBUG, "Found %i systemd activation sockets", sd_fd_count);
  if (sd_fd_count == 0) {
    return absl::nullopt;
  }

  // For each of the provided socket, try to match with the provided address
  for (int fd = SD_LISTEN_FDS_START; fd < SD_LISTEN_FDS_START + sd_fd_count;
       fd++) {
    // check that the systemd socket is actually a listening connection
    // as per man 5 systemd.socket, because of the `Accept=` option, which
    // allows for systemd to spawn the service once per accepted connection.
    // GRPC expects a listening socket, so do not allow "accepted" sockets.
    absl::optional<int> opt_val =
        MaybeGetSockOptIntValue(fd, SOL_SOCKET, SO_ACCEPTCONN);
    if (!opt_val) {
      continue;
    }
    if (*opt_val != 1) {
      gpr_log(GPR_ERROR, "Systemd socket %i is not in listening mode", fd);
      continue;
    }

    // Only `ListenStream` option of man 5 systemd.socket is supported
    // here, as GRPC is connection oriented. Check that the systemd socket is
    // actually a STREAM connection
    opt_val = MaybeGetSockOptIntValue(fd, SOL_SOCKET, SO_TYPE);
    if (!opt_val) {
      continue;
    }
    if (*opt_val != SOCK_STREAM) {
      gpr_log(GPR_ERROR, "Systemd socket %i is not a stream socket", fd);
      continue;
    }

    // Check that the provided address matches the socket
    auto family = addr.address()->sa_family;
    if (family == AF_UNIX) {
      auto sd_fd = MaybeGetSystemdPreallocatedFdFromUnixAddr(fd, addr);
      if (sd_fd) return *sd_fd;
    } else if (family == AF_INET || family == AF_INET6) {
      auto sd_fd = MaybeGetSystemdPreallocatedFdFromNetAddr(fd, addr);
      if (sd_fd) return *sd_fd;
    } else {
      gpr_log(GPR_ERROR,
              "Systemd socket %i is of an unsupported family (sa_family=%i)",
              fd, addr.address()->sa_family);
    }
  }
  return absl::nullopt;
}

#else  // HAVE_LIBSYSTEMD

absl::StatusOr<bool> IsSystemdPreallocatedFd(int fd) { return false; }

bool IsSystemdPreallocatedFdOrLogErrorsWithFalseFallback(int fd) {
  return false;
}

absl::StatusOr<absl::optional<int>> MaybeGetSystemdPreallocatedFdFromAddr(
    const EventEngine::ResolvedAddress& addr) {
  return absl::nullopt;
}

#endif  // HAVE_LIBSYSTEMD

}  // namespace experimental
}  // namespace grpc_event_engine
