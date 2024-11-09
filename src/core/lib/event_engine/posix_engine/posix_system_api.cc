// Copyright 2024 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/posix_system_api.h"

#include <fcntl.h>
#include <unistd.h>

namespace grpc_event_engine {
namespace experimental {

FileDescriptor PosixSystemApi::AdoptExternalFd(int fd) const {
  return FileDescriptor(fd);
}

FileDescriptor PosixSystemApi::Socket(int domain, int type,
                                      int protocol) const {
  return FileDescriptor(socket(domain, type, protocol));
}

int PosixSystemApi::Bind(FileDescriptor fd, const struct sockaddr* addr,
                         socklen_t addrlen) const {
  return bind(fd.fd(), addr, addrlen);
}

void PosixSystemApi::Close(FileDescriptor fd) const { close(fd.fd()); }

int PosixSystemApi::Fcntl(FileDescriptor fd, int op, int args) const {
  return fcntl(fd.fd(), op, args);
}

int PosixSystemApi::GetSockOpt(FileDescriptor fd, int level, int optname,
                               void* optval, socklen_t* optlen) const {
  return getsockopt(fd.fd(), level, optname, optval, optlen);
}

int PosixSystemApi::GetSockName(FileDescriptor fd, struct sockaddr* addr,
                                socklen_t* addrlen) const {
  return getsockname(fd.fd(), addr, addrlen);
}

int PosixSystemApi::GetPeerName(FileDescriptor fd, struct sockaddr* addr,
                                socklen_t* addrlen) const {
  return getpeername(fd.fd(), addr, addrlen);
}

int PosixSystemApi::Listen(FileDescriptor fd, int backlog) const {
  return listen(fd.fd(), backlog);
}

ssize_t PosixSystemApi::RecvMsg(FileDescriptor fd, struct msghdr* msg,
                                int flags) const {
  return recvmsg(fd.fd(), msg, flags);
}

ssize_t PosixSystemApi::SendMsg(FileDescriptor fd, const struct msghdr* message,
                                int flags) const {
  return sendmsg(fd.fd(), message, flags);
}

int PosixSystemApi::SetSockOpt(FileDescriptor fd, int level, int optname,
                               const void* optval, socklen_t optlen) const {
  return setsockopt(fd.fd(), level, optname, optval, optlen);
}

}  // namespace experimental
}  // namespace grpc_event_engine