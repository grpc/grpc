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
#include <optional>
#include <unordered_set>

namespace grpc_event_engine::experimental {

namespace {}  // namespace

FileDescriptor FileDescriptorCollection::Add(int fd) {
  grpc_core::MutexLock lock(&mu_);
  file_descriptors_.emplace(fd);
  return FileDescriptor(fd,
                        current_generation_.load(std::memory_order_relaxed));
}

std::optional<int> FileDescriptorCollection::Remove(const FileDescriptor& fd) {
  if (fd.generation() == current_generation_.load(std::memory_order_relaxed)) {
    grpc_core::MutexLock lock(&mu_);
    int raw = fd.fd();
    if (file_descriptors_.erase(fd.fd()) == 1) {
      return raw;
    }
  }
  return std::nullopt;
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
  CHECK_LT(raw, 1 << kIntFdBits) << "Too many open file descriptors";
  int generation = current_generation_.load(std::memory_order_relaxed);
  return raw + ((generation & kGenerationMask) << kIntFdBits);
}

FileDescriptorResult FileDescriptorCollection::FromInteger(int fd) const {
  if (fd <= 0) {
    return FileDescriptorResult(FileDescriptor::Invalid());
  }
  int generation = current_generation_.load(std::memory_order_relaxed);
  if ((fd >> kIntFdBits) != (generation & kGenerationMask)) {
    return FileDescriptorResult(OperationResultKind::kWrongGeneration, 0);
  }
  return FileDescriptorResult(
      FileDescriptor(fd & ((1 << kIntFdBits) - 1), generation));
}

FileDescriptorResult FileDescriptorCollection::RegisterPosixResult(int result) {
  if (result > 0) {
    return FileDescriptorResult(Add(result));
  } else {
    return FileDescriptorResult(OperationResultKind::kError, errno);
  }
}

std::optional<int> FileDescriptorCollection::GetRawFileDescriptor(
    const FileDescriptor& fd) const {
  if (!IsCorrectGeneration(fd)) {
    return std::nullopt;
  }
  return fd.fd();
}

}  // namespace grpc_event_engine::experimental