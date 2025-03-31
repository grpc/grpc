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

#include <atomic>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

#include "absl/strings/str_format.h"
#include "src/core/util/strerror.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

template <typename T>
class PosixErrorOr {
 private:
  // Convenient alias
  template <typename T1>
  using if_not_void_t = std::enable_if_t<std::negation_v<std::is_void<T1>>, T1>;

 public:
  PosixErrorOr() = default;
  PosixErrorOr(const PosixErrorOr& other) = default;
  PosixErrorOr(PosixErrorOr&& other) = default;
  template <typename T1 = T>
  explicit PosixErrorOr(if_not_void_t<T1> value) : value_(std::move(value)) {}
  PosixErrorOr& operator=(const PosixErrorOr& other) = default;
  PosixErrorOr& operator=(PosixErrorOr&& other) = default;

  static PosixErrorOr Error(int code) { return PosixErrorOr(PosixError{code}); }

  static PosixErrorOr WrongGeneration() {
    return PosixErrorOr(WrongGenerationError());
  }

  bool ok() const {
    return std::holds_alternative<
        std::conditional_t<std::is_void_v<T>, std::monostate, T>>(value_);
  }

  int code() const {
    const PosixError* error = std::get_if<PosixError>(&value_);
    CHECK_NE(error, nullptr);
    return error->code;
  }

  bool IsPosixError() const {
    return std::holds_alternative<PosixError>(value_);
  }

  bool IsPosixError(int code) const {
    const PosixError* error = std::get_if<PosixError>(&value_);
    return error != nullptr && code == error->code;
  }

  bool IsWrongGenerationError() const {
    return std::holds_alternative<WrongGenerationError>(value_);
  }

  template <typename T1 = T>
  if_not_void_t<T1>* operator->() {
    return &std::get<T>(value_);
  }

  template <typename T1 = T>
  if_not_void_t<T1>& operator*() {
    return std::get<T>(value_);
  }

  template <typename T1 = T>
  const if_not_void_t<T1>& value() const {
    CHECK(ok());
    return std::get<T>(value_);
  }

  template <typename T1 = T>
  if_not_void_t<T1> value_or(T1&& default_value) const {
    if (ok()) {
      return value();
    } else {
      return std::forward<T1>(default_value);
    }
  }

  std::string StrError() const {
    if (ok()) {
      return "ok";
    } else if (IsWrongGenerationError()) {
      return "file descriptor was created pre fork";
    } else {
      return grpc_core::StrError(code());
    }
  }

 private:
  struct PosixError {
    int code;
  };
  struct WrongGenerationError {};
  using Payload =
      std::variant<std::conditional_t<std::is_void_v<T>, std::monostate, T>,
                   PosixError, WrongGenerationError>;

  explicit PosixErrorOr(Payload error) : value_(std::move(error)) {}

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
  // Escape for iomgr and tests. Not to be used elsewhere
  int iomgr_fd() const { return fd_; }
  // For logging/debug purposes - may include generation in the future, do not
  // rely on it for Posix calls!
  int debug_fd() const { return fd_; }

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
  // Encodes a file descriptor (fd) and its generation into a single integer,
  // required by some libraries (e.g., Ares).
  // Formula: `fd + ((generation & kGenerationMask) << kIntFdBits)`.
  //
  // Use ToInteger/FromInteger for conversion.
  //
  // LIMITATIONS:
  // 1. Fails (with an assertion) if fd > 28 bits. However, POSIX assigns
  //    the lowest available fd, making 2^28 (~268M) open fds highly impractical
  //    due to kernel resource usage.
  // 2. Only uses the lower 3 bits of generation, risking collisions (e.g., gen
  //    9 accepts gen 1).
  static constexpr int kIntFdBits = 28;
  static constexpr int kGenerationMask = 0x7;  // 3 bits

  // Adds a raw file descriptor `fd` to the collection and associates it
  // with the current generation. Simply constructs a new FileDescriptor
  // instance without adding to a collection if fork is disabled.
  FileDescriptor Add(int fd);
  // Removes a FileDescriptor from the collection.
  // If fork support is disabled, this always returns true.
  // If fork support is enabled, fd is only removed if its generation matches
  // the current collection generation.
  bool Remove(const FileDescriptor& fd);

  // TODO (eostroukhov) Completely recreate the ARES resolver on fork and
  // remove 2 methods below
  // Encodes a FileDescriptor (fd and generation) into a single integer.
  // If fork support is disabled, this simply returns the raw fd.
  // If fork support is enabled, it combines the fd and the lower bits of the
  // generation according to the defined bitmask and shift.
  int ToInteger(const FileDescriptor& fd) const;
  // Decodes an integer (previously encoded by ToInteger) back into a
  // FileDescriptor.
  // If fork support is disabled, it creates a FileDescriptor with generation 0.
  // If fork support is enabled, it extracts the fd and checks if the encoded
  // generation bits match the current generation bits.
  PosixErrorOr<FileDescriptor> FromInteger(int fd) const;

  // Advances the collection's generation number, clears the internal list of
  // tracked file descriptors, and returns the set of fds that were being
  // tracked under the previous generation. This should be called after a fork
  // in the child process.
  std::unordered_set<int> AdvanceGeneration();

  // Returns the current generation number of the collection.
  int generation() const {
    return current_generation_.load(std::memory_order_relaxed);
  }

 private:
  grpc_core::Mutex mu_;
  std::unordered_set<int> file_descriptors_ ABSL_GUARDED_BY(mu_);
  std::atomic_int current_generation_{1};
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H
