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

#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

class PosixError {
 public:
  static constexpr PosixError Ok() { return PosixError(kOk); }

  static constexpr PosixError Error(int errno_value) {
    CHECK_GT(errno_value, 0);
    return PosixError(errno_value);
  }

  static constexpr PosixError WrongGeneration() {
    return PosixError(kWrongGenerationError);
  }

  constexpr PosixError() : payload_(kOk) {}

  constexpr bool ok() const { return payload_ == kOk; }

  bool IsPosixError() const { return payload_ > 0; }

  bool IsPosixError(int errno_value) const {
    return errno_value >= 0 && payload_ == errno_value;
  }

  bool IsWrongGenerationError() const {
    return payload_ == kWrongGenerationError;
  }

  std::optional<int> errno_value() const {
    if (payload_ > 0) return payload_;
    return std::nullopt;
  }

  std::string StrError() const;

 private:
  static constexpr int kWrongGenerationError = -1;
  static constexpr int kOk = 0;

  explicit constexpr PosixError(int error) : payload_(error) {}

  int payload_;
};

template <typename T>
class PosixErrorOr {
 public:
  using Payload = std::variant<T, PosixError>;

  constexpr PosixErrorOr() = default;
  constexpr PosixErrorOr(const PosixErrorOr& other) = default;
  constexpr PosixErrorOr(PosixErrorOr&& other) = default;
  constexpr PosixErrorOr(  // NOLINT(google-explicit-constructor)
      const PosixError& error)
      : value_(error) {
    CHECK(!error.ok());
  }
  constexpr PosixErrorOr(T&& value)  // NOLINT(google-explicit-constructor)
      : value_(std::forward<T>(value)) {}
  PosixErrorOr& operator=(const PosixErrorOr& other) = default;
  PosixErrorOr& operator=(PosixErrorOr&& other) = default;

  bool ok() const { return std::holds_alternative<T>(value_); }

  std::optional<int> errno_value() const {
    if (ok()) {
      return std::nullopt;
    }
    return std::get<PosixError>(value_).errno_value();
  }

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

  const T* operator->() const { return &std::get<T>(value_); }

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

  template <typename Sink>
  friend void AbslStringify(Sink& sink, PosixErrorOr<T> error) {
    if (error.ok()) {
      sink.Append(absl::StrCat(error.value()));
    } else {
      sink.Append(error.StrError());
    }
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
  constexpr FileDescriptor() noexcept = default;
#if GRPC_ENABLE_FORK_SUPPORT
  constexpr FileDescriptor(int fd, int generation) noexcept
      : fd_(fd), generation_(generation) {};
#else   // GRPC_ENABLE_FORK_SUPPORT
  constexpr FileDescriptor(int fd, int /* generation */) noexcept : fd_(fd) {};
#endif  // GRPC_ENABLE_FORK_SUPPORT

  bool ready() const { return fd_ >= 0; }
  int fd() const { return fd_; }
  constexpr static FileDescriptor Invalid() { return FileDescriptor(-1, 0); }

#if GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return generation_; }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(
        absl::StrFormat("fd(%d, generation: %d)", fd.fd_, fd.generation_));
  }

  bool operator==(const FileDescriptor& other) const {
    return fd_ == other.fd_ && generation_ == other.generation_;
  }
#else  // GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return 0; }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(absl::StrFormat("fd(%d)", fd.fd_));
  }
  bool operator==(const FileDescriptor& other) const {
    return fd_ == other.fd_;
  }

#endif  // GRPC_ENABLE_FORK_SUPPORT

  friend std::ostream& operator<<(std::ostream& os, const FileDescriptor& fd) {
    os << absl::StrCat(fd);
    return os;
  }

 private:
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
  explicit FileDescriptorCollection(int generation) noexcept;
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
  // Clears the internal collection and returns a set of file descriptors
  absl::flat_hash_set<int> ClearAndReturnRawDescriptors();

  // Returns the current generation number of the collection.
#if GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return generation_; }
#else   // GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return 0; }
#endif  // GRPC_ENABLE_FORK_SUPPORT

 private:
#if GRPC_ENABLE_FORK_SUPPORT
  grpc_core::Mutex mu_;
  absl::flat_hash_set<int> file_descriptors_ ABSL_GUARDED_BY(mu_);
  // Never changed outside of ctor, no need to synchronize
  int generation_;
#endif  // GRPC_ENABLE_FORK_SUPPORT
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H
