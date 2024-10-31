// Copyright 2022 gRPC authors.
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

#include "test/core/event_engine/test_suite/posix/oracle_event_engine_posix.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/alloc.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/util/crash.h"
#include "src/core/util/strerror.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

const char* kStopMessage = "STOP";

grpc_resolved_address CreateGRPCResolvedAddress(
    const EventEngine::ResolvedAddress& ra) {
  grpc_resolved_address grpc_addr;
  memcpy(grpc_addr.addr, ra.address(), ra.size());
  grpc_addr.len = ra.size();
  return grpc_addr;
}

// Blocks until poll(2) indicates that one of the fds has pending I/O
// the deadline is reached whichever comes first. Returns an OK
// status a valid I/O event is available for at least one of the fds, a Status
// with canonical code DEADLINE_EXCEEDED if the deadline expired and a non-OK
// Status if any other error occurred.
absl::Status PollFds(struct pollfd* pfds, int nfds, absl::Duration timeout) {
  int rv;
  while (true) {
    if (timeout != absl::InfiniteDuration()) {
      rv = poll(pfds, nfds,
                static_cast<int>(absl::ToInt64Milliseconds(timeout)));
    } else {
      rv = poll(pfds, nfds, /* timeout = */ -1);
    }
    const int saved_errno = errno;
    errno = saved_errno;
    if (rv >= 0 || errno != EINTR) {
      break;
    }
  }
  if (rv < 0) {
    return absl::UnknownError(grpc_core::StrError(errno));
  }
  if (rv == 0) {
    return absl::CancelledError("Deadline exceeded");
  }
  return absl::OkStatus();
}

absl::Status BlockUntilReadable(int fd) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  return PollFds(&pfd, 1, absl::InfiniteDuration());
}

absl::Status BlockUntilWritableWithTimeout(int fd, absl::Duration timeout) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLOUT;
  pfd.revents = 0;
  return PollFds(&pfd, 1, timeout);
}

absl::Status BlockUntilWritable(int fd) {
  return BlockUntilWritableWithTimeout(fd, absl::InfiniteDuration());
}

// Tries to read upto num_expected_bytes from the socket. It returns early if
// specified data is not yet available.
std::string TryReadBytes(int sockfd, int& saved_errno, int num_expected_bytes) {
  int ret = 0;
  static constexpr int kDefaultNumExpectedBytes = 1024;
  if (num_expected_bytes <= 0) {
    num_expected_bytes = kDefaultNumExpectedBytes;
  }
  std::string read_data = std::string(num_expected_bytes, '\0');
  char* buffer = const_cast<char*>(read_data.c_str());
  int pending_bytes = num_expected_bytes;
  do {
    errno = 0;
    ret = read(sockfd, buffer + num_expected_bytes - pending_bytes,
               pending_bytes);
    if (ret > 0) {
      pending_bytes -= ret;
    }
  } while (pending_bytes > 0 && ((ret > 0) || (ret < 0 && errno == EINTR)));
  saved_errno = errno;
  return read_data.substr(0, num_expected_bytes - pending_bytes);
}

// Blocks calling thread until the specified number of bytes have been
// read from the provided socket or it encounters an unrecoverable error. It
// puts the read bytes into a string and returns the string. If it encounters an
// error, it returns an empty string and updates saved_errno with the
// appropriate errno.
std::string ReadBytes(int sockfd, int& saved_errno, int num_expected_bytes) {
  std::string read_data;
  do {
    saved_errno = 0;
    read_data += TryReadBytes(sockfd, saved_errno,
                              num_expected_bytes - read_data.length());
    if (saved_errno == EAGAIN &&
        read_data.length() < static_cast<size_t>(num_expected_bytes)) {
      CHECK_OK(BlockUntilReadable(sockfd));
    } else if (saved_errno != 0 && num_expected_bytes > 0) {
      read_data.clear();
      break;
    }
  } while (read_data.length() < static_cast<size_t>(num_expected_bytes));
  return read_data;
}

// Tries to write the specified bytes over the socket. It returns the number of
// bytes actually written.
int TryWriteBytes(int sockfd, int& saved_errno, std::string write_bytes) {
  int ret = 0;
  int pending_bytes = write_bytes.length();
  do {
    errno = 0;
    ret = write(sockfd,
                write_bytes.c_str() + write_bytes.length() - pending_bytes,
                pending_bytes);
    if (ret > 0) {
      pending_bytes -= ret;
    }
  } while (pending_bytes > 0 && ((ret > 0) || (ret < 0 && errno == EINTR)));
  saved_errno = errno;
  return write_bytes.length() - pending_bytes;
}

// Blocks calling thread until the specified number of bytes have been
// written over the provided socket or it encounters an unrecoverable error. The
// bytes to write are specified as a string. If it encounters an error, it
// returns an empty string and updates saved_errno with the appropriate errno
// and returns a value less than zero.
int WriteBytes(int sockfd, int& saved_errno, std::string write_bytes) {
  int ret = 0;
  int original_write_length = write_bytes.length();
  do {
    saved_errno = 0;
    ret = TryWriteBytes(sockfd, saved_errno, write_bytes);
    if (saved_errno == EAGAIN && ret < static_cast<int>(write_bytes.length())) {
      CHECK_GE(ret, 0);
      CHECK_OK(BlockUntilWritable(sockfd));
    } else if (saved_errno != 0) {
      CHECK_LT(ret, 0);
      return ret;
    }
    write_bytes = write_bytes.substr(ret, std::string::npos);
  } while (!write_bytes.empty());
  return original_write_length;
}
}  // namespace

PosixOracleEndpoint::PosixOracleEndpoint(int socket_fd)
    : socket_fd_(socket_fd) {
  read_ops_ = grpc_core::Thread(
      "read_ops_thread",
      [](void* arg) {
        static_cast<PosixOracleEndpoint*>(arg)->ProcessReadOperations();
      },
      this);
  write_ops_ = grpc_core::Thread(
      "write_ops_thread",
      [](void* arg) {
        static_cast<PosixOracleEndpoint*>(arg)->ProcessWriteOperations();
      },
      this);
  read_ops_.Start();
  write_ops_.Start();
}

void PosixOracleEndpoint::Shutdown() {
  grpc_core::MutexLock lock(&mu_);
  if (std::exchange(is_shutdown_, true)) {
    return;
  }
  read_ops_channel_ = ReadOperation();
  read_op_signal_->Notify();
  write_ops_channel_ = WriteOperation();
  write_op_signal_->Notify();
  read_ops_.Join();
  write_ops_.Join();
}

std::unique_ptr<PosixOracleEndpoint> PosixOracleEndpoint::Create(
    int socket_fd) {
  return std::make_unique<PosixOracleEndpoint>(socket_fd);
}

PosixOracleEndpoint::~PosixOracleEndpoint() {
  Shutdown();
  close(socket_fd_);
}

bool PosixOracleEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                               SliceBuffer* buffer, const ReadArgs* args) {
  grpc_core::MutexLock lock(&mu_);
  CHECK_NE(buffer, nullptr);
  int read_hint_bytes =
      args != nullptr ? std::max(1, static_cast<int>(args->read_hint_bytes))
                      : 0;
  read_ops_channel_ =
      ReadOperation(read_hint_bytes, buffer, std::move(on_read));
  read_op_signal_->Notify();
  return false;
}

bool PosixOracleEndpoint::Write(
    absl::AnyInvocable<void(absl::Status)> on_writable, SliceBuffer* data,
    const WriteArgs* /*args*/) {
  grpc_core::MutexLock lock(&mu_);
  CHECK_NE(data, nullptr);
  write_ops_channel_ = WriteOperation(data, std::move(on_writable));
  write_op_signal_->Notify();
  return false;
}

void PosixOracleEndpoint::ProcessReadOperations() {
  LOG(INFO) << "Starting thread to process read ops ...";
  while (true) {
    read_op_signal_->WaitForNotification();
    read_op_signal_ = std::make_unique<grpc_core::Notification>();
    auto read_op = std::exchange(read_ops_channel_, ReadOperation());
    if (!read_op.IsValid()) {
      read_op(std::string(), absl::CancelledError("Closed"));
      break;
    }
    int saved_errno;
    std::string read_data =
        ReadBytes(socket_fd_, saved_errno, read_op.GetNumBytesToRead());
    read_op(read_data, read_data.empty()
                           ? absl::CancelledError(
                                 absl::StrCat("Read failed with error = ",
                                              grpc_core::StrError(saved_errno)))
                           : absl::OkStatus());
  }
  LOG(INFO) << "Shutting down read ops thread ...";
}

void PosixOracleEndpoint::ProcessWriteOperations() {
  LOG(INFO) << "Starting thread to process write ops ...";
  while (true) {
    write_op_signal_->WaitForNotification();
    write_op_signal_ = std::make_unique<grpc_core::Notification>();
    auto write_op = std::exchange(write_ops_channel_, WriteOperation());
    if (!write_op.IsValid()) {
      write_op(absl::CancelledError("Closed"));
      break;
    }
    int saved_errno;
    int ret = WriteBytes(socket_fd_, saved_errno, write_op.GetBytesToWrite());
    write_op(ret < 0 ? absl::CancelledError(
                           absl::StrCat("Write failed with error = ",
                                        grpc_core::StrError(saved_errno)))
                     : absl::OkStatus());
  }
  LOG(INFO) << "Shutting down write ops thread ...";
}

PosixOracleListener::PosixOracleListener(
    EventEngine::Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
    : on_accept_(std::move(on_accept)),
      on_shutdown_(std::move(on_shutdown)),
      memory_allocator_factory_(std::move(memory_allocator_factory)) {
  if (pipe(pipefd_) == -1) {
    grpc_core::Crash(absl::StrFormat("Error creating pipe: %s",
                                     grpc_core::StrError(errno).c_str()));
  }
}

absl::Status PosixOracleListener::Start() {
  grpc_core::MutexLock lock(&mu_);
  CHECK(!listener_fds_.empty());
  if (std::exchange(is_started_, true)) {
    return absl::InternalError("Cannot start listener more than once ...");
  }
  serve_ = grpc_core::Thread(
      "accept_thread",
      [](void* arg) {
        static_cast<PosixOracleListener*>(arg)->HandleIncomingConnections();
      },
      this);
  serve_.Start();
  return absl::OkStatus();
}

PosixOracleListener::~PosixOracleListener() {
  grpc_core::MutexLock lock(&mu_);
  if (!is_started_) {
    serve_.Join();
    return;
  }
  for (int i = 0; i < static_cast<int>(listener_fds_.size()); i++) {
    shutdown(listener_fds_[i], SHUT_RDWR);
  }
  // Send a STOP message over the pipe.
  CHECK(write(pipefd_[1], kStopMessage, strlen(kStopMessage)) != -1);
  serve_.Join();
  on_shutdown_(absl::OkStatus());
}

void PosixOracleListener::HandleIncomingConnections() {
  LOG(INFO) << "Starting accept thread ...";
  CHECK(!listener_fds_.empty());
  int nfds = listener_fds_.size();
  // Add one extra file descriptor to poll the pipe fd.
  ++nfds;
  struct pollfd* pfds =
      static_cast<struct pollfd*>(gpr_malloc(sizeof(struct pollfd) * nfds));
  memset(pfds, 0, sizeof(struct pollfd) * nfds);
  while (true) {
    for (int i = 0; i < nfds; i++) {
      pfds[i].fd = i == nfds - 1 ? pipefd_[0] : listener_fds_[i];
      pfds[i].events = POLLIN;
      pfds[i].revents = 0;
    }
    if (!PollFds(pfds, nfds, absl::InfiniteDuration()).ok()) {
      break;
    }
    int saved_errno = 0;
    if ((pfds[nfds - 1].revents & POLLIN) &&
        ReadBytes(pipefd_[0], saved_errno, strlen(kStopMessage)) ==
            std::string(kStopMessage)) {
      break;
    }
    for (int i = 0; i < nfds - 1; i++) {
      if (!(pfds[i].revents & POLLIN)) {
        continue;
      }
      // pfds[i].fd has a readable event.
      int client_sock_fd = accept(pfds[i].fd, nullptr, nullptr);
      if (client_sock_fd < 0) {
        LOG(ERROR) << "Error accepting new connection: "
                   << grpc_core::StrError(errno)
                   << ". Ignoring connection attempt ...";
        continue;
      }
      on_accept_(PosixOracleEndpoint::Create(client_sock_fd),
                 memory_allocator_factory_->CreateMemoryAllocator("test"));
    }
  }
  LOG(INFO) << "Shutting down accept thread ...";
  gpr_free(pfds);
}

absl::StatusOr<int> PosixOracleListener::Bind(
    const EventEngine::ResolvedAddress& addr) {
  grpc_core::MutexLock lock(&mu_);
  if (is_started_) {
    return absl::FailedPreconditionError(
        "Listener is already started, ports can no longer be bound");
  }
  int new_socket;
  int opt = -1;
  grpc_resolved_address address = CreateGRPCResolvedAddress(addr);
  const char* scheme = grpc_sockaddr_get_uri_scheme(&address);
  if (scheme == nullptr || strcmp(scheme, "ipv6") != 0) {
    return absl::UnimplementedError(
        "Unsupported bind address type. Only IPV6 addresses are supported "
        "currently by the PosixOracleListener ...");
  }

  // Creating a new socket file descriptor.
  if ((new_socket = socket(AF_INET6, SOCK_STREAM, 0)) <= 0) {
    return absl::UnknownError(
        absl::StrCat("Error creating socket: ", grpc_core::StrError(errno)));
  }
  // MacOS biulds fail if SO_REUSEADDR and SO_REUSEPORT are set in the same
  // setsockopt syscall. So they are set separately one after the other.
  if (setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    return absl::UnknownError(absl::StrCat("Error setsockopt(SO_REUSEADDR): ",
                                           grpc_core::StrError(errno)));
  }
  if (setsockopt(new_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
    return absl::UnknownError(absl::StrCat("Error setsockopt(SO_REUSEPORT): ",
                                           grpc_core::StrError(errno)));
  }

  // Forcefully bind the new socket.
  if (bind(new_socket, reinterpret_cast<const struct sockaddr*>(addr.address()),
           address.len) < 0) {
    return absl::UnknownError(
        absl::StrCat("Error bind: ", grpc_core::StrError(errno)));
  }
  // Set the new socket to listen for one active connection at a time.
  if (listen(new_socket, 1) < 0) {
    return absl::UnknownError(
        absl::StrCat("Error listen: ", grpc_core::StrError(errno)));
  }
  listener_fds_.push_back(new_socket);
  return 0;
}

// PosixOracleEventEngine implements blocking connect. It blocks the calling
// thread until either connect succeeds or fails with timeout.
EventEngine::ConnectionHandle PosixOracleEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& /*args*/, MemoryAllocator /*memory_allocator*/,
    EventEngine::Duration timeout) {
  int client_sock_fd;
  absl::Time deadline = absl::Now() + absl::FromChrono(timeout);
  grpc_resolved_address address = CreateGRPCResolvedAddress(addr);
  const char* scheme = grpc_sockaddr_get_uri_scheme(&address);
  if (scheme == nullptr || strcmp(scheme, "ipv6") != 0) {
    on_connect(
        absl::CancelledError("Unsupported bind address type. Only ipv6 "
                             "addresses are currently supported."));
    return {};
  }
  if ((client_sock_fd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
    on_connect(absl::CancelledError(
        absl::StrCat("Connect failed: socket creation error: ",
                     grpc_core::StrError(errno).c_str())));
    return {};
  }
  int err;
  int num_retries = 0;
  static constexpr int kMaxRetries = 5;
  do {
    err = connect(client_sock_fd, const_cast<struct sockaddr*>(addr.address()),
                  address.len);
    if (err < 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK)) {
      auto status = BlockUntilWritableWithTimeout(
          client_sock_fd,
          std::max(deadline - absl::Now(), absl::ZeroDuration()));
      if (!status.ok()) {
        on_connect(status);
        return {};
      }
    } else if (err < 0) {
      if (errno != ECONNREFUSED || ++num_retries > kMaxRetries) {
        on_connect(absl::CancelledError("Connect failed."));
        return {};
      }
      // If ECONNREFUSED && num_retries < kMaxRetries, wait a while and try
      // again.
      absl::SleepFor(absl::Milliseconds(100));
    }
  } while (err < 0 && absl::Now() < deadline);
  if (err < 0 && absl::Now() >= deadline) {
    on_connect(absl::CancelledError("Deadline exceeded"));
  } else {
    on_connect(PosixOracleEndpoint::Create(client_sock_fd));
  }
  return {};
}

}  // namespace experimental
}  // namespace grpc_event_engine
