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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_INTERFACE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_INTERFACE_H

#include <grpc/event_engine/event_engine.h>

#include <cerrno>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "src/core/lib/event_engine/posix_engine/file_descriptor_collection.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"

namespace grpc_event_engine::experimental {

class EventEnginePosixInterface {
 public:
  // An enum to keep track of IPv4/IPv6 socket modes.
  //
  // Currently, this information is only used when a socket is first created,
  // but in the future we may wish to store it alongside the fd.  This would let
  // calls like sendto() know which family to use without asking the kernel
  // first.
  enum DSMode {
    // Uninitialized, or a non-IP socket.
    DSMODE_NONE,
    // AF_INET only.
    DSMODE_IPV4,
    // AF_INET6 only, because IPV6_V6ONLY could not be cleared.
    DSMODE_IPV6,
    // AF_INET6, which also supports ::ffff-mapped IPv4 addresses.
    DSMODE_DUALSTACK
  };

  EventEnginePosixInterface() : descriptors_(1) {}
  EventEnginePosixInterface(const EventEnginePosixInterface& other) = delete;
  EventEnginePosixInterface(EventEnginePosixInterface&& other) = delete;

#ifdef GRPC_ENABLE_FORK_SUPPORT
  // ---- Fork generation management ----
  // Advances the internal generation counter, potentially invalidating old
  // descriptors.
  void AdvanceGeneration();
#endif  // GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return descriptors_.generation(); }

  // ---- File Descriptor Management ----
  // Adopts an existing POSIX file descriptor, returning a managed
  // FileDescriptor object.
  FileDescriptor Adopt(int fd);
  void Close(const FileDescriptor& fd);
  // Retrieves the raw POSIX file descriptor, if valid for the current
  // generation.
  PosixErrorOr<int> GetFd(const FileDescriptor& fd);

  // ---- Socket/FD Creation Factories ----
  struct PosixSocketCreateResult {
    FileDescriptor sock;
    EventEngine::ResolvedAddress mapped_target_addr;
  };
  // Return a PosixSocketCreateResult which manages a configured, unbound,
  // unconnected TCP client fd.
  //  options: may contain custom tcp settings for the fd.
  //  target_addr: the destination address.
  //
  // Returns: Not-OK status on error. Otherwise it returns a
  // PosixSocketWrapper::PosixSocketCreateResult type which includes a sock
  // of type PosixSocketWrapper and a mapped_target_addr which is
  // target_addr mapped to an address appropriate to the type of socket FD
  // created. For example, if target_addr is IPv4 and dual stack sockets are
  // available, mapped_target_addr will be an IPv4-mapped IPv6 address.
  //
  absl::StatusOr<PosixSocketCreateResult> CreateAndPrepareTcpClientSocket(
      const PosixTcpOptions& options,
      const EventEngine::ResolvedAddress& target_addr);
  // Creates a new socket for connecting to (or listening on) an address.
  //
  // If addr is AF_INET6, this creates an IPv6 socket first.  If that fails,
  // and addr is within ::ffff:0.0.0.0/96, then it automatically falls back
  // to an IPv4 socket.
  //
  // If addr is AF_INET, AF_UNIX, or anything else, then this is similar to
  // calling socket() directly.
  //
  // Returns a FileDescriptor on success, otherwise returns a not-OK
  // absl::Status
  //
  // The dsmode output indicates which address family was actually created.
  absl::StatusOr<FileDescriptor> CreateDualStackSocket(
      std::function<int(int, int, int)> socket_factory,
      const experimental::EventEngine::ResolvedAddress& addr, int type,
      int protocol, DSMode& dsmode);
  PosixErrorOr<FileDescriptor> EpollCreateAndCloexec();
  PosixErrorOr<FileDescriptor> EventFd(int initval, int flags);
  absl::StatusOr<std::pair<FileDescriptor, FileDescriptor>> Pipe();
  PosixErrorOr<FileDescriptor> Socket(int domain, int type, int protocol);

  // ---- Socket Operations (General POSIX) ----
  PosixErrorOr<FileDescriptor> Accept(const FileDescriptor& sockfd,
                                      struct sockaddr* addr,
                                      socklen_t* addrlen);
  PosixErrorOr<FileDescriptor> Accept4(const FileDescriptor& sockfd,
                                       EventEngine::ResolvedAddress& addr,
                                       int nonblock, int cloexec);
  PosixError Connect(const FileDescriptor& sockfd, const struct sockaddr* addr,
                     socklen_t addrlen);
  PosixErrorOr<int64_t> Read(const FileDescriptor& fd, absl::Span<char> buffer);
  PosixErrorOr<int64_t> RecvMsg(const FileDescriptor& fd,
                                struct msghdr* message, int flags);
  PosixErrorOr<int64_t> SendMsg(const FileDescriptor& fd,
                                const struct msghdr* message, int flags);
  PosixError Shutdown(const FileDescriptor& fd, int how);
  PosixErrorOr<int64_t> Write(const FileDescriptor& fd,
                              absl::Span<char> buffer);

  // ---- Socket Configuration & Querying ----
  // Applies socket mutator options defined within PosixTcpOptions to a file
  // descriptor.
  absl::Status ApplySocketMutatorInOptions(const FileDescriptor& fd,
                                           grpc_fd_usage usage,
                                           const PosixTcpOptions& options);
  // Configures the default TCP_USER_TIMEOUT socket option for future sockets
  // (static).
  static void ConfigureDefaultTcpUserTimeout(bool enable, int timeout,
                                             bool is_client);
  // Applies standard configuration to a socket based on its type. Returns zero
  // to indicate a success or a negative value to indicate an error
  int ConfigureSocket(const FileDescriptor& fd, int type);
  // Gets a socket option value (getsockopt wrapper).
  PosixError GetSockOpt(const FileDescriptor& fd, int level, int optname,
                        void* optval, void* optlen);
  // Finds and returns an unused network port.
  absl::StatusOr<int> GetUnusedPort();
  // Performs an ioctl operation on a file descriptor.
  PosixError Ioctl(const FileDescriptor& fd, int op, void* arg);
  // Retrieves the local address of a socket as an EventEngine::ResolvedAddress.
  absl::StatusOr<EventEngine::ResolvedAddress> LocalAddress(
      const FileDescriptor& fd);
  // Retrieves the local address of a socket as a string.
  absl::StatusOr<std::string> LocalAddressString(const FileDescriptor& fd);
  // Retrieves the peer address of a connected socket as an
  // EventEngine::ResolvedAddress.
  absl::StatusOr<EventEngine::ResolvedAddress> PeerAddress(
      const FileDescriptor& fd);
  // Retrieves the peer address of a connected socket as a string.
  absl::StatusOr<std::string> PeerAddressString(const FileDescriptor& fd);
  // Prepares a listener socket with specified options and address binding.
  absl::StatusOr<EventEngine::ResolvedAddress> PrepareListenerSocket(
      const FileDescriptor& fd, const PosixTcpOptions& options,
      const EventEngine::ResolvedAddress& address);
  // Applies a grpc_socket_mutator function to configure a socket.
  absl::Status SetSocketMutator(const FileDescriptor& fd, grpc_fd_usage usage,
                                grpc_socket_mutator* mutator);
  // Tries to set the SO_NOSIGPIPE option on a socket if the platform supports
  // it.
  absl::Status SetSocketNoSigpipeIfPossible(const FileDescriptor& fd);
  // Sets a socket option value (setsockopt wrapper).
  PosixErrorOr<int64_t> SetSockOpt(const FileDescriptor& fd, int level,
                                   int optname, uint32_t optval);

  // Epoll
#ifdef GRPC_LINUX_EPOLL
  PosixError EpollCtlAdd(const FileDescriptor& epfd, bool writable,
                         const FileDescriptor& fd, void* data);
  PosixError EpollCtlDel(const FileDescriptor& epfd, const FileDescriptor& fd);
#endif  // GRPC_LINUX_EPOLL

  PosixError EventFdRead(const FileDescriptor& fd);
  PosixError EventFdWrite(const FileDescriptor& fd);

 private:
  static bool IsEventEngineForkEnabled() {
#ifdef GRPC_ENABLE_FORK_SUPPORT
    return grpc_core::IsEventEngineForkEnabled();
#else   // GRPC_ENABLE_FORK_SUPPORT
    return false;
#endif  // GRPC_ENABLE_FORK_SUPPORT
  }

  absl::Status PrepareTcpClientSocket(int fd,
                                      const EventEngine::ResolvedAddress& addr,
                                      const PosixTcpOptions& options);
  PosixError PosixResultWrap(
      const FileDescriptor& wrapped,
      const absl::AnyInvocable<int(int) const>& fn) const;
  bool IsCorrectGeneration(const FileDescriptor& fd) const;
  PosixErrorOr<FileDescriptor> RegisterPosixResult(int result);

  FileDescriptorCollection descriptors_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_INTERFACE_H
