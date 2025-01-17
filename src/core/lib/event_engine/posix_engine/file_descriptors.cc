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

#include "src/core/lib/event_engine/posix_engine/file_descriptors.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <cerrno>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#include <arpa/inet.h>  // IWYU pragma: keep
#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/in.h>  // IWYU pragma: keep
#include <netinet/tcp.h>
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif  //  GRPC_POSIX_SOCKET_UTILS_COMMON

#ifdef GRPC_POSIX_SOCKET

#define IF_POSIX_SOCKET(signature, body) signature body

#else  // GRPC_POSIX_SOCKET

#include "src/core/util/crash.h"

#define IF_POSIX_SOCKET(signature, body) \
  signature { grpc_core::Crash("unimplemented"); }

#endif  // GRPC_POSIX_SOCKET

namespace grpc_event_engine::experimental {

FileDescriptor FileDescriptors::Adopt(int fd) { return FileDescriptor(fd); }

FileDescriptorResult FileDescriptors::RegisterPosixResult(int result) {
  if (result > 0) {
    return FileDescriptorResult::FD(Adopt(result));
  } else {
    return FileDescriptorResult::Error();
  }
}

IF_POSIX_SOCKET(void FileDescriptors::Close(const FileDescriptor& fd),
                { close(fd.fd()); })

//
// Factories
//
IF_POSIX_SOCKET(FileDescriptorResult FileDescriptors::Accept(
                    int sockfd, struct sockaddr* addr, socklen_t* addrlen),
                { return RegisterPosixResult(accept(sockfd, addr, addrlen)); })

#ifdef GRPC_POSIX_SOCKETUTILS

IF_POSIX_SOCKET(
    FileDescriptorResult FileDescriptors::Accept4(
        int sockfd,
        grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
        int nonblock, int cloexec),
    {
      EventEngine::ResolvedAddress peer_addr;
      socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
      FileDescriptorResult fd =
          Accept(sockfd, const_cast<sockaddr*>(peer_addr.address()), &len);
      if (!fd.ok()) {
        return fd;
      }
      int flags;
      int fdescriptor = fd.fd();
      if (nonblock) {
        flags = fcntl(fdescriptor, F_GETFL, 0);
        if (flags < 0) goto close_and_error;
        if (fcntl(fdescriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
          goto close_and_error;
        }
      }
      if (cloexec) {
        flags = fcntl(fdescriptor, F_GETFD, 0);
        if (flags < 0) goto close_and_error;
        if (fcntl(fdescriptor, F_SETFD, flags | FD_CLOEXEC) != 0) {
          goto close_and_error;
        }
      }
      addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
      return fd;
    close_and_error:
      Close(*fd);
      return FileDescriptorResult::Error();
    })

#else  // GRPC_POSIX_SOCKETUTILS

IF_POSIX_SOCKET(
    FileDescriptorResult FileDescriptors::Accept4(
        int sockfd,
        grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
        int nonblock, int cloexec),
    {
      int flags = 0;
      flags |= nonblock ? SOCK_NONBLOCK : 0;
      flags |= cloexec ? SOCK_CLOEXEC : 0;
      EventEngine::ResolvedAddress peer_addr;
      socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
      FileDescriptorResult ret = RegisterPosixResult(accept4(
          sockfd, const_cast<sockaddr*>(peer_addr.address()), &len, flags));
      addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
      return ret;
    })

#endif  // GRPC_POSIX_SOCKETUTILS

}  // namespace grpc_event_engine::experimental