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

#include <unordered_set>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep

namespace grpc_event_engine::experimental {

#ifdef GRPC_ENABLE_FORK_SUPPORT

FileDescriptorCollection::FileDescriptorCollection(
    FileDescriptorCollection&& other) noexcept
    : generation_(other.generation_) {
  grpc_core::MutexLock lock(&other.mu_);
  file_descriptors_ = std::move(other.file_descriptors_);
}

FileDescriptorCollection& FileDescriptorCollection::operator=(
    FileDescriptorCollection&& other) noexcept {
  generation_ = other.generation_;
  grpc_core::MutexLock self_lock(&mu_);
  grpc_core::MutexLock other_lock(&other.mu_);
  file_descriptors_ = std::move(other.file_descriptors_);
  return *this;
}

FileDescriptor FileDescriptorCollection::Add(int fd) {
  if (grpc_core::IsEventEngineForkEnabled()) {
    grpc_core::MutexLock lock(&mu_);
    file_descriptors_.emplace(fd);
  }
  return FileDescriptor(fd, generation_);
}

bool FileDescriptorCollection::Remove(const FileDescriptor& fd) {
  if (!grpc_core::IsEventEngineForkEnabled()) {
    return true;
  }
  if (fd.generation() == generation_) {
    grpc_core::MutexLock lock(&mu_);
    return file_descriptors_.erase(fd.fd_) == 1;
  }
  return false;
}

std::unordered_set<int> FileDescriptorCollection::Clear() {
  grpc_core::MutexLock lock(&mu_);
  std::unordered_set<int> file_descriptors = std::move(file_descriptors_);
  // Should not be necessary, but standard is not clear if move would empty
  // the collection
  file_descriptors_.clear();
  return file_descriptors;
}

#else  // GRPC_ENABLE_FORK_SUPPORT

FileDescriptorCollection::FileDescriptorCollection(
    FileDescriptorCollection&& other) noexcept
    : generation_(other.generation_) {}

FileDescriptorCollection& FileDescriptorCollection::operator=(
    FileDescriptorCollection&& other) noexcept {
  generation_ = other.generation_;
  return *this;
}

FileDescriptor FileDescriptorCollection::Add(int fd) {
  return FileDescriptor(fd, 0);
}

bool FileDescriptorCollection::Remove(const FileDescriptor& fd) { return true; }

std::unordered_set<int> FileDescriptorCollection::Clear() { return {}; }

#endif  // GRPC_ENABLE_FORK_SUPPORT

}  // namespace grpc_event_engine::experimental