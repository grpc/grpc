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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_SYSTEM_API_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_SYSTEM_API_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#ifdef GRPC_LINUX_EPOLL
#include <sys/epoll.h>
#endif

namespace grpc_event_engine {
namespace experimental {

class FileDescriptor {
 public:
  FileDescriptor() : fd_(-1) {}
  explicit FileDescriptor(int fd) : fd_(fd) {}

  bool ready() const { return fd_ > 0; }
  void invalidate() { fd_ = -1; }
  int fd() const { return fd_; }

 private:
  int fd_;
};

#ifdef GRPC_LINUX_EPOLL
using EpollEvent = epoll_event;
#else
struct EpollEvent {};
#endif

class SystemApi {
 public:
  virtual ~SystemApi() = default;

  // Factories
  virtual FileDescriptor AdoptExternalFd(int fd) const = 0;
  virtual FileDescriptor EventFd(unsigned int initval, int flags) const = 0;
  virtual FileDescriptor Socket(int domain, int type, int protocol) const = 0;

  // Functions operating on file descriptors
  virtual int Bind(FileDescriptor fd, const struct sockaddr* addr,
                   socklen_t addrlen) const = 0;
  virtual void Close(FileDescriptor fd) const = 0;
  virtual int EpollWait(FileDescriptor epfd, EpollEvent* events, int maxevents,
                         int timeout) const = 0;
  virtual int EpollCtl(FileDescriptor epfd, int op, FileDescriptor fd,
                        EpollEvent* event) const = 0;
  virtual int EventfdRead(FileDescriptor fd) const = 0;
  virtual int EventfdWrite(FileDescriptor fd, uint64_t counter) const = 0;
  virtual int Fcntl(FileDescriptor fd, int op, int args) const = 0;
  virtual int GetSockOpt(FileDescriptor fd, int level, int optname,
                         void* optval, socklen_t* optlen) const = 0;
  virtual int GetSockName(FileDescriptor fd, struct sockaddr* addr,
                          socklen_t* addrlen) const = 0;
  virtual int GetPeerName(FileDescriptor fd, struct sockaddr* addr,
                          socklen_t* addrlen) const = 0;
  virtual int Listen(FileDescriptor fd, int backlog) const = 0;
  virtual long Read(FileDescriptor fd, void* buf, size_t nbyte) const = 0;
  virtual long RecvMsg(FileDescriptor fd, struct msghdr* msg,
                       int flags) const = 0;
  virtual long SendMsg(FileDescriptor fd, const struct msghdr* message,
                       int flags) const = 0;
  virtual int SetSockOpt(FileDescriptor fd, int level, int optname,
                         const void* optval, socklen_t optlen) const = 0;
  virtual void Shutdown(FileDescriptor fd, int how) const = 0;
  virtual long Write(FileDescriptor fd, const void* buf,
                     size_t nbyte) const = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_SYSTEM_API_H