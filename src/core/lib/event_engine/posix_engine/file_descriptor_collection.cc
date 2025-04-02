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
    if (file_descriptors_.erase(fd.fd_) == 1) {
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

}  // namespace grpc_event_engine::experimental