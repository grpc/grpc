// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_UTILS_BINDER_AUTO_UTILS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_UTILS_BINDER_AUTO_UTILS_H

#include <grpc/support/port_platform.h>

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include "src/core/ext/transport/binder/utils/ndk_binder.h"

namespace grpc_binder {
namespace ndk_util {

///
/// Represents one strong pointer to an AIBinder object.
/// Copied from binder/ndk/include_cpp/android/binder_auto_utils.h
///
class SpAIBinder {
 public:
  SpAIBinder() : mBinder(nullptr) {}
  explicit SpAIBinder(AIBinder* binder) : mBinder(binder) {}
  SpAIBinder(std::nullptr_t)
      : SpAIBinder() {}  // NOLINT(google-explicit-constructor)
  SpAIBinder(const SpAIBinder& other) { *this = other; }

  ~SpAIBinder() { set(nullptr); }
  SpAIBinder& operator=(const SpAIBinder& other) {
    if (this == &other) {
      return *this;
    }
    AIBinder_incStrong(other.mBinder);
    set(other.mBinder);
    return *this;
  }

  void set(AIBinder* binder) {
    AIBinder* old = *const_cast<AIBinder* volatile*>(&mBinder);
    if (old != nullptr) AIBinder_decStrong(old);
    if (old != *const_cast<AIBinder* volatile*>(&mBinder)) {
      __assert(__FILE__, __LINE__, "Race detected.");
    }
    mBinder = binder;
  }

  AIBinder* get() const { return mBinder; }
  AIBinder** getR() { return &mBinder; }

  bool operator!=(const SpAIBinder& rhs) const { return get() != rhs.get(); }
  bool operator<(const SpAIBinder& rhs) const { return get() < rhs.get(); }
  bool operator<=(const SpAIBinder& rhs) const { return get() <= rhs.get(); }
  bool operator==(const SpAIBinder& rhs) const { return get() == rhs.get(); }
  bool operator>(const SpAIBinder& rhs) const { return get() > rhs.get(); }
  bool operator>=(const SpAIBinder& rhs) const { return get() >= rhs.get(); }

 private:
  AIBinder* mBinder = nullptr;
};
}  // namespace ndk_util
}  // namespace grpc_binder

#endif

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_UTILS_BINDER_AUTO_UTILS_H
