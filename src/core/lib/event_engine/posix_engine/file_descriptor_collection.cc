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

#include "absl/strings/substitute.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep
#include "src/core/util/strerror.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_ENABLE_FORK_SUPPORT

namespace {

bool IsForkEnabled() { return grpc_core::IsEventEngineForkEnabled(); }

}  // namespace

FileDescriptorCollection::FileDescriptorCollection(int generation) noexcept
    : generation_(generation) {}

FileDescriptorCollection::FileDescriptorCollection(
    FileDescriptorCollection&& other) noexcept
    : generation_(other.generation_) {
  grpc_core::MutexLock lock(&other.mu_);
  file_descriptors_ = std::move(other.file_descriptors_);
  other.generation_ = -1;
  other.file_descriptors_.clear();
}

FileDescriptorCollection& FileDescriptorCollection::operator=(
    FileDescriptorCollection&& other) noexcept {
  generation_ = other.generation_;
  grpc_core::MutexLock self_lock(&mu_);
  grpc_core::MutexLock other_lock(&other.mu_);
  file_descriptors_ = std::move(other.file_descriptors_);
  other.generation_ = -1;
  other.file_descriptors_.clear();
  return *this;
}

FileDescriptor FileDescriptorCollection::Add(int fd) {
  if (IsForkEnabled()) {
    grpc_core::MutexLock lock(&mu_);
    file_descriptors_.emplace(fd);
  }
  return FileDescriptor(fd, generation_);
}

bool FileDescriptorCollection::Remove(const FileDescriptor& fd) {
  if (!IsForkEnabled()) {
    return true;
  }
  if (fd.generation() == generation_) {
    grpc_core::MutexLock lock(&mu_);
    return file_descriptors_.erase(fd.fd()) == 1;
  }
  return false;
}

absl::flat_hash_set<int>
FileDescriptorCollection::ClearAndReturnRawDescriptors() {
  if (!IsForkEnabled()) {
    return {};
  }
  grpc_core::MutexLock lock(&mu_);
  absl::flat_hash_set<int> file_descriptors = std::move(file_descriptors_);
  // Should not be necessary, but standard is not clear if move would empty
  // the collection
  file_descriptors_.clear();
  return file_descriptors;
}

#else  // GRPC_ENABLE_FORK_SUPPORT

FileDescriptorCollection::FileDescriptorCollection(
    int /* generation */) noexcept {}

FileDescriptorCollection::FileDescriptorCollection(
    FileDescriptorCollection&& other) noexcept = default;

FileDescriptorCollection& FileDescriptorCollection::operator=(
    FileDescriptorCollection&& other) noexcept = default;

FileDescriptor FileDescriptorCollection::Add(int fd) {
  return FileDescriptor(fd, 0);
}

bool FileDescriptorCollection::Remove(const FileDescriptor& /* fd */) {
  return true;
}

absl::flat_hash_set<int>
FileDescriptorCollection::ClearAndReturnRawDescriptors() {
  return {};
}

#endif  // GRPC_ENABLE_FORK_SUPPORT

std::string PosixError::StrError() const {
  if (ok()) {
    return "ok";
  }
  if (IsWrongGenerationError()) {
    return "file descriptor was created pre fork";
  }
  int value = *errno_value();
  return absl::Substitute("$0 ($1)", grpc_core::StrError(value), value);
}

}  // namespace grpc_event_engine::experimental