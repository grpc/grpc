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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_SYSTEM_API_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_SYSTEM_API_H

#include "src/core/lib/event_engine/extensions/system_api.h"

namespace grpc_event_engine {
namespace experimental {

class PosixSystemApi : public SystemApi {
 public:
  FileDescriptor AdoptExternalFd(int fd) const override;
  FileDescriptor Socket(int domain, int type, int protocol) const override;

  int Bind(FileDescriptor fd, const struct sockaddr* addr,
           socklen_t addrlen) const override;
  void Close(FileDescriptor fd) const override;
  int Fcntl(FileDescriptor fd, int op, int args) const override;
  int GetSockOpt(FileDescriptor fd, int level, int optname, void* optval,
                 socklen_t* optlen) const override;
  int GetSockName(FileDescriptor fd, struct sockaddr* addr,
                  socklen_t* addrlen) const override;
  int GetPeerName(FileDescriptor fd, struct sockaddr* addr,
                  socklen_t* addrlen) const override;
  int Listen(FileDescriptor fd, int backlog) const override;
  long RecvMsg(FileDescriptor fd, struct msghdr* msg, int flags) const override;
  long SendMsg(FileDescriptor fd, const struct msghdr* message,
               int flags) const override;
  int SetSockOpt(FileDescriptor fd, int level, int optname, const void* optval,
                 socklen_t optlen) const override;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_SYSTEM_API_H