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

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

// Stubs for compiling on non-POSIX platforms (e.g., Windows)
#ifndef GRPC_POSIX_SOCKET
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/util/crash.h"

namespace grpc_event_engine::experimental {

bool IsSocketReusePortSupported() {
  grpc_core::Crash(
      "unimplemented on this platform: IsSocketReusePortSupported");
}

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::Accept(
    const FileDescriptor& sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Accept");
}

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::Accept4(
    const FileDescriptor& sockfd,
    grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
    int nonblock, int cloexec) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Accept4");
}

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::Socket(int domain,
                                                               int type,
                                                               int protocol) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Socket");
}

#ifndef GRPC_POSIX_WAKEUP_FD
absl::StatusOr<std::pair<FileDescriptor, FileDescriptor> >
EventEnginePosixInterface::Pipe() {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Pipe");
}
#endif  // GRPC_POSIX_WAKEUP_FD

PosixError EventEnginePosixInterface::Connect(const FileDescriptor& sockfd,
                                              const struct sockaddr* addr,
                                              socklen_t addrlen) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Connect");
}

PosixError EventEnginePosixInterface::Ioctl(const FileDescriptor& fd, int op,
                                            void* arg) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Ioctl");
}

PosixError EventEnginePosixInterface::Shutdown(const FileDescriptor& fd,
                                               int how) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Shutdown");
}

PosixError EventEnginePosixInterface::GetSockOpt(const FileDescriptor& fd,
                                                 int level, int optname,
                                                 void* optval, void* optlen) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::GetSockOpt");
}

PosixErrorOr<int64_t> EventEnginePosixInterface::SetSockOpt(
    const FileDescriptor& fd, int level, int optname, uint32_t optval) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::SetSockOpt");
}

#ifndef GRPC_POSIX_WAKEUP_FD
PosixErrorOr<int64_t> EventEnginePosixInterface::Read(const FileDescriptor& fd,
                                                      absl::Span<char> buf) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Read");
}

PosixErrorOr<int64_t> EventEnginePosixInterface::Write(const FileDescriptor& fd,
                                                       absl::Span<char> buf) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Write");
}
#endif  // GRPC_POSIX_WAKEUP_FD

PosixErrorOr<int64_t> EventEnginePosixInterface::RecvMsg(
    const FileDescriptor& fd, struct msghdr* message, int flags) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::RecvMsg");
}

PosixErrorOr<int64_t> EventEnginePosixInterface::SendMsg(
    const FileDescriptor& fd, const struct msghdr* message, int flags) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::SendMsg");
}

// Note: PrepareTcpClientSocket is private in the header, stubs might not be
// needed depending on usage, but updating message for consistency if it is.
absl::Status EventEnginePosixInterface::PrepareTcpClientSocket(
    int fd, const EventEngine::ResolvedAddress& addr,
    const PosixTcpOptions& options) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::PrepareTcpClientSocket");
}

absl::StatusOr<EventEngine::ResolvedAddress>
EventEnginePosixInterface::PrepareListenerSocket(
    const FileDescriptor& fd, const PosixTcpOptions& options,
    const EventEngine::ResolvedAddress& address) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::PrepareListenerSocket");
}

absl::StatusOr<int> EventEnginePosixInterface::GetUnusedPort() {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::GetUnusedPort");
}

// Note: InternalApplySocketMutatorInOptions seems like a helper not in the
// class. Assuming it's a free function in this namespace.
absl::Status InternalApplySocketMutatorInOptions(
    int fd, grpc_fd_usage usage, const PosixTcpOptions& options) {
  grpc_core::Crash(
      "unimplemented on this platform: InternalApplySocketMutatorInOptions");
}

absl::StatusOr<EventEngine::ResolvedAddress>
EventEnginePosixInterface::LocalAddress(const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::LocalAddress");
}

absl::StatusOr<std::string> EventEnginePosixInterface::LocalAddressString(
    const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::LocalAddressString");
}

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::EventFd(int initval,
                                                                int flags) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::EventFd");
}

PosixError EventEnginePosixInterface::EventFdRead(const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::EventFdRead");
}

PosixError EventEnginePosixInterface::EventFdWrite(const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::EventFdWrite");
}

int EventEnginePosixInterface::ConfigureSocket(const FileDescriptor& fd,
                                               int type) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::ConfigureSocket");
}

PosixErrorOr<int> EventEnginePosixInterface::GetFd(const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::GetFd");
}

absl::StatusOr<EventEngine::ResolvedAddress>
EventEnginePosixInterface::PeerAddress(const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::PeerAddress");
}

absl::StatusOr<std::string> EventEnginePosixInterface::PeerAddressString(
    const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::PeerAddressString");
}

#ifndef GRPC_POSIX_WAKEUP_FD
void EventEnginePosixInterface::Close(const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Close");
}
#endif  // GRPC_POSIX_WAKEUP_FD

absl::StatusOr<FileDescriptor> EventEnginePosixInterface::CreateDualStackSocket(
    std::function<int(int, int, int)> socket_factory,
    const experimental::EventEngine::ResolvedAddress& addr, int type,
    int protocol, DSMode& dsmode) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::CreateDualStackSocket");
}

absl::Status EventEnginePosixInterface::ApplySocketMutatorInOptions(
    const FileDescriptor& fd, grpc_fd_usage usage,
    const PosixTcpOptions& options) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::ApplySocketMutatorInOptions");
}

absl::StatusOr<EventEnginePosixInterface::PosixSocketCreateResult>
EventEnginePosixInterface::CreateAndPrepareTcpClientSocket(
    const PosixTcpOptions& options,
    const EventEngine::ResolvedAddress& target_addr) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::CreateAndPrepareTcpClientSocket");
}

FileDescriptor EventEnginePosixInterface::Adopt(int fd) {
  grpc_core::Crash(
      "unimplemented on this platform: EventEnginePosixInterface::Adopt");
}

PosixErrorOr<FileDescriptor>
EventEnginePosixInterface::EpollCreateAndCloexec() {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::EpollCreateAndCloexec");
}

absl::Status EventEnginePosixInterface::SetSocketNoSigpipeIfPossible(
    const FileDescriptor& fd) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::SetSocketNoSigpipeIfPossible");
}

#ifdef GRPC_ENABLE_FORK_SUPPORT

void EventEnginePosixInterface::AdvanceGeneration() {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::AdvanceGeneration");
}

#endif  // GRPC_ENABLE_FORK_SUPPORT

absl::Status EventEnginePosixInterface::SetSocketMutator(
    const FileDescriptor& fd, grpc_fd_usage usage,
    grpc_socket_mutator* mutator) {
  grpc_core::Crash(
      "unimplemented on this platform: "
      "EventEnginePosixInterface::SetSocketMutator");
}

}  // namespace grpc_event_engine::experimental

#endif  // !GRPC_POSIX_SOCKET
