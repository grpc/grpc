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

#include "src/core/lib/event_engine/posix_engine/file_descriptors.h"

#include <grpc/support/port_platform.h>
#include <unistd.h>

#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace grpc_event_engine {
namespace experimental {
namespace {

grpc_core::Mutex gone_threads_mu;
std::unordered_set<int> gone_threads ABSL_GUARDED_BY(&gone_threads_mu);

class ThreadLocalCounter {
 public:
  ThreadLocalCounter() {
    grpc_core::MutexLock lock(&gone_threads_mu);
    gone_threads.erase(GetTid());
  }

  ~ThreadLocalCounter() {
    grpc_core::MutexLock lock(&gone_threads_mu);
    gone_threads.emplace(GetTid());
  }

  void FdLocked(const FileDescriptors* descriptors) {
    if (descriptors != nullptr && !IsThreadAlive()) {
      ++thread_locks_count_[descriptors];
    }
  }

  void FdUnlocked(const FileDescriptors* descriptors) {
    if (descriptors != nullptr && !IsThreadAlive()) {
      --thread_locks_count_[descriptors];
      CHECK_GE(thread_locks_count_[descriptors], 0);
    }
  }

  int count(const FileDescriptors* descriptors) const {
    if (!IsThreadAlive()) {
      return 0;
    }
    auto c = thread_locks_count_.find(descriptors);
    if (c != thread_locks_count_.end()) {
      return c->second;
    } else {
      return 0;
    }
  }

 private:
#ifdef GPR_HAS_PTHREAD_H
  static bool IsThreadAlive() {
    int tid = gettid();
    grpc_core::MutexLock lock(&gone_threads_mu);
    return gone_threads.find(tid) == gone_threads.end();
  }

  static int GetTid() { return gettid(); }
#elif
  static bool IsThreadAlive() { return true; }
  static int GetTid() { return -1; }
#endif  // GPR_HAS_PTHREAD_H

  std::unordered_map<const FileDescriptors*, int> thread_locks_count_;
};

thread_local ThreadLocalCounter counter;

}  // namespace

ReentrantLock::ReentrantLock(const FileDescriptors* descriptors)
    : descriptors_(descriptors) {
  if (descriptors_ != nullptr) {
    descriptors_->IncrementCounter();
    counter.FdLocked(descriptors);
  }
}

ReentrantLock::~ReentrantLock() noexcept {
  if (descriptors_ != nullptr) {
    descriptors_->DecrementCounter();
    counter.FdUnlocked(descriptors_);
  }
}

FileDescriptor FileDescriptors::Add(int fd) {
  grpc_core::MutexLock lock(&list_mu_);
  fds_.insert(fd);
  return FileDescriptor{fd};
}

absl::optional<int> FileDescriptors::Remove(const FileDescriptor& fd) {
  auto locked_fd = Lock(fd);
  if (locked_fd.ok()) {
    return locked_fd->fd();
  }
  return absl::nullopt;
}

std::unordered_set<int> FileDescriptors::Clear() {
  grpc_core::MutexLock lock(&list_mu_);
  std::unordered_set<int> ret;
  std::swap(fds_, ret);
  return ret;
}

absl::StatusOr<LockedFd> FileDescriptors::Lock(const FileDescriptor& fd) const {
  auto posix_lock = PosixLock();
  if (!posix_lock.ok()) {
    return std::move(posix_lock).status();
  }
  LockedFd locked_fd{fd.fd(), std::move(posix_lock).value()};
  grpc_core::MutexLock lock(&list_mu_);
  if (fds_.find(locked_fd.fd()) == fds_.end()) {
    return absl::InternalError(
        absl::StrCat("Descriptor ", locked_fd.fd(), " not found"));
  }
  return locked_fd;
}

absl::StatusOr<ReentrantLock> FileDescriptors::PosixLock() const {
  {
    grpc_core::MutexLock lock(&mu_);
    if (state_ != State::kReady) {
      return absl::AbortedError("I/O operations are disabled");
    }
  }
  return ReentrantLock(this);
}

void FileDescriptors::IncrementCounter() const {
  grpc_core::MutexLock lock(&mu_);
  ++locked_descriptors_;
  io_cond_.SignalAll();
}

void FileDescriptors::DecrementCounter() const {
  grpc_core::MutexLock lock(&mu_);
  --locked_descriptors_;
  CHECK_GE(locked_descriptors_, 0);
  io_cond_.SignalAll();
}

absl::Status FileDescriptors::Stop() {
  if (counter.count(this) > 0) {
    return absl::FailedPreconditionError(
        absl::StrFormat("Current thread holds %d i/o locks that need to be "
                        "released before calling fork",
                        counter.count(this)));
  }
  grpc_core::MutexLock lock(&mu_);
  CHECK(state_ == State::kReady)
      << (state_ == State::kStopping ? "Actual: stopping" : "Actual: stopped");
  SetState(State::kStopping);
  while (locked_descriptors_ > 0) {
    io_cond_.Wait(&mu_);
  }
  SetState(State::kStopped);
  return absl::OkStatus();
}

void FileDescriptors::Restart() {
  grpc_core::MutexLock lock(&mu_);
  CHECK(state_ == State::kStopped)
      << (state_ == State::kStopping ? "Actual: stopping" : "Actual: ready");
  SetState(State::kReady);
}

void FileDescriptors::ExpectStatusForTest(int locks, State state) {
  grpc_core::MutexLock lock(&mu_);
  while (locked_descriptors_ != locks || state_ != state) {
    io_cond_.Wait(&mu_);
  }
}

}  // namespace experimental
}  // namespace grpc_event_engine