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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H

#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

#include "absl/strings/str_format.h"
#include "src/core/util/strerror.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

class PosixError {
 public:
  static PosixError WrongGeneration() {
    return PosixError(WrongGenerationError());
  }

  static PosixError Error(int errno_value) {
    return PosixError(PosixErrorValue{errno_value});
  }

  PosixError() : PosixError(std::monostate()) {}

  bool ok() const { return std::holds_alternative<std::monostate>(payload_); }

  bool IsPosixError() const {
    return std::holds_alternative<PosixErrorValue>(payload_);
  }

  bool IsPosixError(int errno_value) const {
    const PosixErrorValue* error_value =
        std::get_if<PosixErrorValue>(&payload_);
    return error_value != nullptr && error_value->errno_value == errno_value;
  }

  bool IsWrongGenerationError() const {
    return std::holds_alternative<WrongGenerationError>(payload_);
  }

  int errno_value() const {
    return std::get<PosixErrorValue>(payload_).errno_value;
  }

  std::string StrError() const {
    if (ok()) {
      return "ok";
    }
    if (IsWrongGenerationError()) {
      return "file descriptor was created pre fork";
    }
    return grpc_core::StrError(errno_value());
  }

 private:
  struct PosixErrorValue {
    int errno_value;
  };
  struct WrongGenerationError {};
  using Payload =
      std::variant<std::monostate, PosixErrorValue, WrongGenerationError>;

  explicit PosixError(Payload error) : payload_(error) {}

  Payload payload_;
};

template <typename T>
class PosixErrorOr {
 public:
  using Payload = std::variant<T, PosixError>;

  PosixErrorOr() = default;
  PosixErrorOr(const PosixErrorOr& other) = default;
  PosixErrorOr(PosixErrorOr&& other) = default;
  explicit PosixErrorOr(Payload error) : value_(std::move(error)) {}
  PosixErrorOr& operator=(const PosixErrorOr& other) = default;
  PosixErrorOr& operator=(PosixErrorOr&& other) = default;

  bool ok() const { return std::holds_alternative<T>(value_); }

  int errno_value() const { return std::get<PosixError>(value_).errno_value(); }

  bool IsPosixError() const {
    const PosixError* error = std::get_if<PosixError>(&value_);
    return error != nullptr && error->IsPosixError();
  }

  bool IsPosixError(int errno_value) const {
    const PosixError* error = std::get_if<PosixError>(&value_);
    return error != nullptr && error->IsPosixError(errno_value);
  }

  bool IsWrongGenerationError() const {
    const PosixError* error = std::get_if<PosixError>(&value_);
    return error != nullptr && error->IsWrongGenerationError();
  }

  T* operator->() { return &std::get<T>(value_); }

  T& operator*() { return std::get<T>(value_); }

  const T& value() const { return std::get<T>(value_); }

  T value_or(T default_value) const {
    if (ok()) {
      return value();
    }
    return default_value;
  }

  std::string StrError() const {
    if (ok()) {
      return "ok";
    }
    return std::get<PosixError>(value_).StrError();
  }

 private:
  Payload value_;
};

// Represents a file descriptor, potentially associated with a fork generation.
// When compiling with fork support (GRPC_ENABLE_FORK_SUPPORT is defined),
// FileDescriptor includes a generation number to track its validity across
// forks. Otherwise, it only stores the fd.
class FileDescriptor {
 public:
  constexpr FileDescriptor() = default;
#if GRPC_ENABLE_FORK_SUPPORT
  constexpr FileDescriptor(int fd, int generation)
      : fd_(fd), generation_(generation) {};
#else   // GRPC_ENABLE_FORK_SUPPORT
  constexpr FileDescriptor(int fd, int /* generation */) : fd_(fd) {};
#endif  // GRPC_ENABLE_FORK_SUPPORT

  bool ready() const { return fd_ >= 0; }
  int fd() const { return fd_; }
  constexpr static FileDescriptor Invalid() { return FileDescriptor(-1, 0); }

#if GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return generation_; }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(
        absl::StrFormat("FD(%d), generation: %d", fd.fd_, fd.generation_));
  }
#else   // GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return 0; }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(absl::StrFormat("FD(%d)", fd.fd_));
  }
#endif  // GRPC_ENABLE_FORK_SUPPORT

 private:
  // Can get raw fd!
  friend class FileDescriptorCollection;
  friend class EventEnginePosixInterface;

  int fd_ = -1;
#if GRPC_ENABLE_FORK_SUPPORT
  int generation_ = 0;
#endif  // GRPC_ENABLE_FORK_SUPPORT
};

// Manages a collection of file descriptors, tracking their validity across
// forks by associating them with a generation number. This is necessary
// to ensure FDs created before a fork are not used after the fork.
class FileDescriptorCollection {
 public:
  explicit FileDescriptorCollection(int generation) : generation_(generation) {}
  FileDescriptorCollection(FileDescriptorCollection&& other) noexcept;
  FileDescriptorCollection& operator=(
      FileDescriptorCollection&& other) noexcept;
  // Adds a raw file descriptor `fd` to the collection and associates it
  // with the current generation. Simply constructs a new FileDescriptor
  // instance without adding to a collection if fork is disabled.
  FileDescriptor Add(int fd);
  // Removes a FileDescriptor from the collection.
  // If fork support is disabled, this always returns true.
  // If fork support is enabled, fd is only removed if its generation matches
  // the current collection generation. Returns true if the fd was removed.
  bool Remove(const FileDescriptor& fd);
  // Returns all file descriptors and empties the collection
  std::unordered_set<int> Clear();

  // Returns the current generation number of the collection.
  int generation() const { return generation_; }

 private:
#if GRPC_ENABLE_FORK_SUPPORT
  grpc_core::Mutex mu_;
  std::unordered_set<int> file_descriptors_ ABSL_GUARDED_BY(mu_);
#endif  // GRPC_ENABLE_FORK_SUPPORT
  // Never changed outside of ctor, no need to synchronize
  int generation_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H
