// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_UTIL_LATENT_SEE_H
#define GRPC_SRC_CORE_UTIL_LATENT_SEE_H

#include <grpc/support/port_platform.h>

#include <cstring>

#ifdef GRPC_ENABLE_LATENT_SEE
#include <cstdint>

#include "absl/base/no_destructor.h"
#include "absl/strings/string_view.h"

#define GRPC_LATENT_SEE_INTERNAL_PASTE2(str, line) str##line
#define GRPC_LATENT_SEE_INTERNAL_PASTE1(str, line) \
  GRPC_LATENT_SEE_INTERNAL_PASTE2(str, line)
#define GRPC_LATENT_SEE_INTERNAL_UNIQUE(str) \
  GRPC_LATENT_SEE_INTERNAL_PASTE1(str, __LINE__)

namespace grpc_core {
namespace latent_see {

constexpr int kInvalid = -1;
constexpr int StorageSize = 128;

class Marker;

class MarkerStorage {
 public:
  enum MarkerType : uint8_t {
    INVALID = 0,
    INNER_SCOPE = 1,
    PARENT_SCOPE = 2,
    EVENT = 3,
    ERROR = 4,
  };
  struct Ops {
    void (*construct)(MarkerStorage*, const char* name, MarkerType type,
                      const char* file, int line, const char* function);
    void (*destruct)(MarkerStorage*);
  };
  MarkerStorage(const char* name, MarkerType type, const char* file, int line,
                const char* function) {
    ops_.construct(this, name, type, file, line, function);
  }
  ~MarkerStorage() { ops_.destruct(this); }
  // This type is neither copyable nor movable.
  MarkerStorage(const MarkerStorage&) = delete;
  MarkerStorage& operator=(const MarkerStorage&) = delete;

  template <typename T>
  T* GetStorageAs() {
    return reinterpret_cast<T*>(&storage_);
  }

  static void OverrideOps(Ops ops);

 private:
  static Ops ops_;
  alignas(StorageSize) char storage_[StorageSize];
};

template <bool is_parent>
class ScopedTimerStorage {
 public:
  struct Ops {
    void (*construct)(ScopedTimerStorage<is_parent>*, MarkerStorage*);
    void (*destruct)(ScopedTimerStorage<is_parent>*);
  };
  ScopedTimerStorage() = default;
  void Init(MarkerStorage* marker) { ops_.construct(this, marker); }
  ~ScopedTimerStorage() { ops_.destruct(this); }
  // This type is neither copyable nor movable.
  ScopedTimerStorage(const ScopedTimerStorage<is_parent>&) = delete;
  ScopedTimerStorage& operator=(const ScopedTimerStorage<is_parent>&) = delete;

  template <typename T>
  T* GetStorageAs() {
    return reinterpret_cast<T*>(&storage_);
  }

  static void OverrideOps(Ops ops);

 private:
  static Ops ops_;
  alignas(StorageSize) char storage_[StorageSize];
};

class GroupThreadScopeStorage {
 public:
  struct Ops {
    void (*construct)(GroupThreadScopeStorage*, MarkerStorage*,
                      absl::string_view name);
    void (*destruct)(GroupThreadScopeStorage*);
  };
  GroupThreadScopeStorage(MarkerStorage* marker, absl::string_view name) {
    ops_.construct(this, marker, name);
  }
  ~GroupThreadScopeStorage() { ops_.destruct(this); }
  // This type is neither copyable nor movable.
  GroupThreadScopeStorage(const GroupThreadScopeStorage&) = delete;
  GroupThreadScopeStorage& operator=(const GroupThreadScopeStorage&) = delete;

  template <typename T>
  T* GetStorageAs() {
    return reinterpret_cast<T*>(&storage_);
  }

  static void OverrideOps(Ops ops);

 private:
  static Ops ops_;
  alignas(StorageSize) char storage_[StorageSize];
};

using ParentScopeTimerStorage = ScopedTimerStorage<true>;
using InnerScopeTimerStorage = ScopedTimerStorage<false>;

typedef void (*LogFn)(MarkerStorage*);

void SetEventLogger(LogFn logger);

void LogEvent(MarkerStorage* marker);

}  // namespace latent_see
}  // namespace grpc_core

// Parent scope: logs a begin and end event, and flushes the thread log on scope
// exit. Because the flush takes some time it's better to place one parent scope
// at the top of the stack, and use lighter weight scopes within it.
#define GRPC_LATENT_SEE_PARENT_SCOPE(name)                                     \
  static absl::NoDestructor<grpc_core::latent_see::MarkerStorage>              \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker)(                                 \
          #name, grpc_core::latent_see::MarkerStorage::PARENT_SCOPE, __FILE__, \
          __LINE__, __PRETTY_FUNCTION__);                                      \
  grpc_core::latent_see::ParentScopeTimerStorage                               \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(timer);                                  \
  GRPC_LATENT_SEE_INTERNAL_UNIQUE(timer).Init(                                 \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker).get())

// Inner scope: logs a begin and end event. Lighter weight than parent scope,
// but does not flush the thread state - so should only be enclosed by a parent
// scope.
#define GRPC_LATENT_SEE_INNER_SCOPE(name)                                     \
  static absl::NoDestructor<grpc_core::latent_see::MarkerStorage>             \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker)(                                \
          #name, grpc_core::latent_see::MarkerStorage::INNER_SCOPE, __FILE__, \
          __LINE__, __PRETTY_FUNCTION__);                                     \
  grpc_core::latent_see::InnerScopeTimerStorage                               \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(timer);                                 \
  GRPC_LATENT_SEE_INTERNAL_UNIQUE(timer).Init(                                \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker).get())

// Mark: logs a single event.
// This is not flushed automatically, and so should only be used within a parent
// scope.
#define GRPC_LATENT_SEE_MARK(name)                                      \
  static absl::NoDestructor<grpc_core::latent_see::MarkerStorage>       \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker)(                          \
          #name, grpc_core::latent_see::MarkerStorage::EVENT, __FILE__, \
          __LINE__, __PRETTY_FUNCTION__);                               \
  grpc_core::latent_see::LogEvent(GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker).get())

// Denotes that all work done in the current thread will be grouped together
// by name until we leave the current scope.
#define GRPC_LATENT_SEE_GROUP(name)                                         \
  static absl::NoDestructor<grpc_core::latent_see::MarkerStorage>           \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker)(                              \
          "<group>", grpc_core::latent_see::MarkerStorage::SCOPE, __FILE__, \
          __LINE__, __PRETTY_FUNCTION__);                                   \
  grpc_core::latent_see::GroupThreadScopeStorage                            \
  GRPC_LATENT_SEE_INTERNAL_UNIQUE(timer)(                                   \
      GRPC_LATENT_SEE_INTERNAL_UNIQUE(marker).get(), #name)
#else  // !def(GRPC_ENABLE_LATENT_SEE)
#define GRPC_LATENT_SEE_PARENT_SCOPE(name) \
  do {                                     \
  } while (0)
#define GRPC_LATENT_SEE_INNER_SCOPE(name) \
  do {                                    \
  } while (0)
#define GRPC_LATENT_SEE_MARK(name) \
  do {                             \
  } while (0)
#define GRPC_LATENT_SEE_GROUP(name) \
  do {                              \
  } while (0)
#endif  // GRPC_ENABLE_LATENT_SEE

#endif  // GRPC_SRC_CORE_UTIL_LATENT_SEE_H
