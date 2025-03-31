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

#include "src/core/lib/event_engine/posix_engine/file_descriptor_collection.h"

#include <atomic>
#include <unordered_set>

#include "absl/functional/any_invocable.h"
#include "src/core/lib/experiments/experiments.h"

namespace grpc_event_engine::experimental {

namespace {

bool IsForkEnabled() {
#ifdef GRPC_ENABLE_FORK_SUPPORT
  return grpc_core::IsEventEngineForkEnabled();
#else
  return false;
#endif
}

}  // namespace

FileDescriptor FileDescriptorCollection::Add(int fd) {
  grpc_core::MutexLock lock(&mu_);
  file_descriptors_.emplace(fd);
  return FileDescriptor(fd,
                        current_generation_.load(std::memory_order_relaxed));
}

bool FileDescriptorCollection::Remove(const FileDescriptor& fd) {
  if (!IsForkEnabled()) {
    return true;
  }
  if (fd.generation() == current_generation_.load(std::memory_order_relaxed)) {
    grpc_core::MutexLock lock(&mu_);
    if (file_descriptors_.erase(fd.fd()) == 1) {
      return true;
    }
  }
  return false;
}

std::unordered_set<int> FileDescriptorCollection::AdvanceGeneration() {
  grpc_core::MutexLock lock(&mu_);
  std::unordered_set<int> result = std::move(file_descriptors_);
  ++current_generation_;
  return result;
}

int FileDescriptorCollection::ToInteger(const FileDescriptor& fd) const {
  int raw = fd.fd();
  if (raw <= 0) {
    return 0;
  }
  static const auto fn =
      IsForkEnabled()
          ? absl::AnyInvocable<int(int, int) const>(+[](int fd,
                                                        int gen) -> int {
              CHECK_LT(fd, 1 << kIntFdBits) << "Too many open file descriptors";
              return fd + ((gen & kGenerationMask) << kIntFdBits);
            })
          : absl::AnyInvocable<int(int, int) const>(
                +[](int fd, int /* gen */) -> int { return fd; });
  return fn(raw, fd.generation());
}

PosixErrorOr<FileDescriptor> FileDescriptorCollection::FromInteger(
    int fd) const {
  if (fd <= 0) {
    return PosixErrorOr<FileDescriptor>(FileDescriptor::Invalid());
  }
  if (!IsForkEnabled()) {
    return PosixErrorOr<FileDescriptor>({fd, 0});
  }
  int generation = current_generation_.load(std::memory_order_relaxed);
  if ((fd >> kIntFdBits) != (generation & kGenerationMask)) {
    return PosixErrorOr<FileDescriptor>::WrongGeneration();
  }
  return PosixErrorOr<FileDescriptor>(
      FileDescriptor(fd & ((1 << kIntFdBits) - 1), generation));
}

}  // namespace grpc_event_engine::experimental