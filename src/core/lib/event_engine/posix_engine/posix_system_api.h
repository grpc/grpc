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

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <array>
#include <atomic>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/sync.h"

#ifdef GRPC_LINUX_EPOLL
#include <sys/epoll.h>
#endif  // GRPC_LINUX_EPOLL

namespace grpc_event_engine {
namespace experimental {

class LockedFd;

ABSL_ATTRIBUTE_TRIVIAL_ABI class FileDescriptor {
 public:
  FileDescriptor() = default;
  explicit FileDescriptor(int fd) : fd_(fd) {}

  bool ready() const { return fd_ > 0; }
  void invalidate() { fd_ = -1; }

 private:
  friend class LockedFd;
  int fd() const { return fd_; }

  int fd_ = -1;
};

class SystemApi;

// FD that is locked for use in this thread
class LockedFd {
 public:
  explicit LockedFd(FileDescriptor fd, const SystemApi& system_api);
  ~LockedFd();

  int fd() const { return fd_.fd(); }

 private:
  FileDescriptor fd_;
  const SystemApi* system_api_;
};

class SystemApi {
 public:
  static constexpr int kDscpNotSet = -1;

  SystemApi() = default;
  SystemApi(const SystemApi& other) = delete;

  ~SystemApi();

  absl::Status AdvanceGeneration();

  absl::StatusOr<FileDescriptor> Accept(FileDescriptor sockfd,
                                        struct sockaddr* addr,
                                        socklen_t* addrlen);
  absl::StatusOr<FileDescriptor> Accept4(
      FileDescriptor sockfd,
      grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
      int nonblock, int cloexec);
  absl::StatusOr<FileDescriptor> Accept4(FileDescriptor sockfd,
                                         struct sockaddr* addr,
                                         socklen_t* addrlen, int flags);
  FileDescriptor AdoptExternalFd(int fd);
  FileDescriptor Socket(int domain, int type, int protocol);
  FileDescriptor EventFd(unsigned int initval, int flags);
  FileDescriptor EpollCreateAndCloexec();

  std::pair<int, std::array<FileDescriptor, 2>> Pipe();
  std::pair<int, std::array<FileDescriptor, 2>> SocketPair(int domain, int type,
                                                           int protocol);

  absl::StatusOr<int> Bind(FileDescriptor fd, const struct sockaddr* addr,
                           socklen_t addrlen) const;
  void Close(FileDescriptor fd);
  absl::StatusOr<int> Connect(FileDescriptor sockfd,
                              const struct sockaddr* addr,
                              socklen_t addrlen) const;
#ifdef GRPC_LINUX_EPOLL
  absl::StatusOr<long> EventFdRead(FileDescriptor fd, uint64_t* value) const;
  absl::StatusOr<int> EventFdWrite(FileDescriptor fd, uint64_t value) const;
  absl::StatusOr<int> EpollCtl(FileDescriptor epfd, int op, FileDescriptor fd,
                               struct epoll_event* event) const;
  absl::StatusOr<int> EpollWait(FileDescriptor epfd, struct epoll_event* events,
                                int maxevents, int timeout) const;
#endif  // GRPC_LINUX_EPOLL
  absl::StatusOr<int> GetSockOpt(FileDescriptor fd, int level, int optname,
                                 void* optval, socklen_t* optlen) const;
  absl::StatusOr<int> GetSockName(FileDescriptor fd, struct sockaddr* addr,
                                  socklen_t* addrlen) const;
  absl::StatusOr<int> GetPeerName(FileDescriptor fd, struct sockaddr* addr,
                                  socklen_t* addrlen) const;
  absl::StatusOr<int> Ioctl(FileDescriptor fd, int request, void* extras) const;
  absl::StatusOr<int> Listen(FileDescriptor fd, int backlog) const;
  absl::StatusOr<long> RecvMsg(FileDescriptor fd, struct msghdr* msg,
                               int flags) const;
  absl::StatusOr<long> Read(FileDescriptor fd, void* buf, size_t count) const;
  absl::StatusOr<long> SendMsg(FileDescriptor fd, const struct msghdr* message,
                               int flags) const;
  absl::Status SetSockOpt(FileDescriptor fd, int level, int optname,
                          const void* optval, socklen_t optlen,
                          absl::string_view label) const;
  absl::StatusOr<int> Shutdown(FileDescriptor sockfd, int how) const;
  absl::StatusOr<long> Write(FileDescriptor fd, const void* buf,
                             size_t count) const;

  absl::Status SetSocketNoSigpipeIfPossible(FileDescriptor fd) const;
  bool IsSocketReusePortSupported() const;
  // Set SO_REUSEPORT
  absl::Status SetSocketReusePort(FileDescriptor fd, int reuse) const;
  // Set socket to use zerocopy
  absl::Status SetSocketZeroCopy(FileDescriptor fd) const;
  // Set socket to non blocking mode
  absl::Status SetNonBlocking(FileDescriptor fd, bool non_blocking) const;
  // Set socket to close on exec
  absl::Status SetSocketCloexec(FileDescriptor fd, int close_on_exec) const;
  // Disable nagle algorithm
  absl::Status SetSocketLowLatency(FileDescriptor fd, int low_latency) const;
  // Set socket to reuse old addresses
  absl::Status SetSocketReuseAddr(FileDescriptor fd, int reuse) const;
  // Set Differentiated Services Code Point (DSCP)
  absl::Status SetSocketDscp(FileDescriptor fd, int dscp) const;
  // Tries to set IP_PKTINFO if available on this platform. If IP_PKTINFO is not
  // available, returns not OK status.
  absl::Status SetSocketIpPktInfoIfPossible(FileDescriptor fd) const;
  // Tries to set IPV6_RECVPKTINFO if available on this platform. If
  // IPV6_RECVPKTINFO is not available, returns not OK status.
  absl::Status SetSocketIpv6RecvPktInfoIfPossible(FileDescriptor fd) const;
  // Tries to set the socket's send buffer to given size.
  absl::Status SetSocketSndBuf(FileDescriptor fd, int buffer_size_bytes) const;
  // Tries to set the socket's receive buffer to given size.
  absl::Status SetSocketRcvBuf(FileDescriptor fd, int buffer_size_bytes) const;
  // Override default Tcp user timeout values if necessary.
  void TrySetSocketTcpUserTimeout(FileDescriptor fd, int keep_alive_time_ms,
                                  int keep_alive_timeout_ms,
                                  bool is_client) const;
  // Configure default values for tcp user timeout to be used by client
  // and server side sockets.
  void ConfigureDefaultTcpUserTimeout(bool enable, int timeout, bool is_client);
  // Return LocalAddress as EventEngine::ResolvedAddress
  absl::StatusOr<EventEngine::ResolvedAddress> LocalAddress(
      FileDescriptor fd) const;
  // Return PeerAddress as EventEngine::ResolvedAddress
  absl::StatusOr<EventEngine::ResolvedAddress> PeerAddress(
      FileDescriptor fd) const;
  // Return LocalAddress as string
  absl::StatusOr<std::string> LocalAddressString(FileDescriptor fd) const;
  // Return PeerAddress as string
  absl::StatusOr<std::string> PeerAddressString(FileDescriptor fd) const;

  absl::Status PerformOperation(
      const FileDescriptor& fd,
      absl::FunctionRef<absl::Status(int)> operation) const {
    return WithFd(fd, operation);
  }

  absl::StatusOr<LockedFd> Lock(FileDescriptor fd) const;

 private:
#if GPR_LINUX == 1
// For Linux, it will be detected to support TCP_USER_TIMEOUT
#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18
#endif
#define SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT 0
#else
// For non-Linux, TCP_USER_TIMEOUT will be used if TCP_USER_TIMEOUT is defined.
#ifdef TCP_USER_TIMEOUT
#define SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT 0
#else
#define TCP_USER_TIMEOUT 0
#define SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT (-1)
#endif  // TCP_USER_TIMEOUT
#endif  // GPR_LINUX == 1

  FileDescriptor RegisterFileDescriptor(int fd);

  template <typename R>
  struct WithFdReturn {
    using type = absl::StatusOr<R>;

    template <typename Fn>
    static type invoke(const Fn fn, int fd) {
      return fn(fd);
    }
  };

  template <>
  struct WithFdReturn<absl::Status> {
    using type = absl::Status;

    template <typename Fn>
    static type invoke(const Fn fn, int fd) {
      return fn(fd);
    }
  };

  template <typename R>
  struct WithFdReturn<absl::StatusOr<R>> {
    using type = absl::StatusOr<R>;

    template <typename Fn>
    static type invoke(const Fn fn, int fd) {
      return fn(fd);
    }
  };

  template <>
  struct WithFdReturn<void> {
    using type = absl::Status;

    template <typename Fn>
    static type invoke(const Fn fn, int fd) {
      fn(fd);
      return absl::OkStatus();
    }
  };

  template <typename Fn>
  auto WithFd(const FileDescriptor& fd, const Fn& fn) const ->
      typename WithFdReturn<decltype(fn(0))>::type {
    if (!fd.ready()) {
      return absl::InternalError("Invalid file descriptor");
    }
    auto locked_fd = Lock(fd);
    if (!locked_fd.ok()) {
      return std::move(locked_fd).status();
    }
    return WithFdReturn<decltype(fn(0))>::invoke(fn, locked_fd->fd());
  }

  // Whether the socket supports TCP_USER_TIMEOUT option.
  // (0: don't know, 1: support, -1: not support)
  mutable std::atomic<int> g_socket_supports_tcp_user_timeout = {
      SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT};
  std::unordered_set<int> fds_ ABSL_GUARDED_BY(&mu_);
  mutable grpc_core::Mutex mu_;

  // The default values for TCP_USER_TIMEOUT are currently configured to be in
  // line with the default values of KEEPALIVE_TIMEOUT as proposed in
  // https://github.com/grpc/proposal/blob/master/A18-tcp-user-timeout.md */
  int kDefaultClientUserTimeoutMs = 20000;
  int kDefaultServerUserTimeoutMs = 20000;
  bool kDefaultClientUserTimeoutEnabled = false;
  bool kDefaultServerUserTimeoutEnabled = true;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_SYSTEM_API_H