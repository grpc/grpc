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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H

#include <unordered_set>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine {
namespace experimental {

class FileDescriptors;

// We acquire a read log for each handle that is locked for io. Some operations
// require multiple handles while ABSL does not allow reentrancy.
class ReentrantLock {
 public:
  explicit ReentrantLock(const FileDescriptors* descriptors);
  ReentrantLock(const ReentrantLock& other) = delete;
  ReentrantLock(ReentrantLock&& other) noexcept
      : descriptors_(other.descriptors_) {
    other.descriptors_ = nullptr;
  }
  ~ReentrantLock() noexcept;

  const FileDescriptors& fds() const { return *descriptors_; }

 private:
  const FileDescriptors* descriptors_;
};

// FD that is locked for use in this thread
class LockedFd {
 public:
  LockedFd(int fd, ReentrantLock lock) : fd_(fd), lock_(std::move(lock)) {}
  LockedFd(const LockedFd& other) = delete;
  LockedFd(LockedFd&& other) = default;

  int fd() const { return fd_; }

 private:
  int fd_;
  ReentrantLock lock_;
};

class FileDescriptor {
 public:
  FileDescriptor() = default;
  explicit FileDescriptor(int fd) : fd_(fd) {}

  bool ready() const { return fd_ > 0; }
  void invalidate() { fd_ = -1; }

  // Not meant to use to access FD for I/O. Only used for debug logging.
  int debug_fd() const { return fd_; }
  int fd() const { return fd_; }

 private:
  int fd_ = -1;
};

class FileDescriptors {
 public:
  enum class State {
    kReady,
    kStopping,
    kStopped,
  };

  FileDescriptors() = default;
  // Class is not copyable because of mutex. Leaving this here in case future
  // refactor changes the class and we need to explicitly prevent copy.
  FileDescriptors(const FileDescriptors& other) = delete;

  FileDescriptor Add(int fd);
  absl::optional<int> Remove(const FileDescriptor& fd);
  std::unordered_set<int> Clear();
  absl::StatusOr<LockedFd> Lock(const FileDescriptor& fd) const;
  void Unlock(const FileDescriptor& fd) const;
  absl::StatusOr<ReentrantLock> PosixLock() const;
  absl::Status Stop();
  void Restart();

  void ExpectStatusForTest(int locks, State state);

 private:
  friend class ReentrantLock;

  void IncrementCounter() const;
  void DecrementCounter() const;

  void SetState(State new_state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    state_ = new_state;
    io_cond_.SignalAll();
  }

  mutable grpc_core::Mutex list_mu_;
  std::unordered_set<int> fds_ ABSL_GUARDED_BY(&list_mu_);

  mutable grpc_core::Mutex mu_;
  mutable int locked_descriptors_ ABSL_GUARDED_BY(&mu_) = 0;
  State state_ ABSL_GUARDED_BY(&mu_) = State::kReady;
  mutable grpc_core::CondVar io_cond_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H