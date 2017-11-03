/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_SUPPORT_VECTOR_H
#define GRPC_CORE_LIB_SUPPORT_VECTOR_H

#include "src/core/lib/support/memory.h"

namespace grpc_core {

// NOTE: We eventually want to use absl::InlinedVector here.  However,
// there are currently build problems that prevent us from using absl.
// In the interim, we define a custom implementation as a place-holder,
// with the intent to eventually replace this with the absl
// implementation.
//
// This place-holder implementation does not implement the full set of
// functionality from the absl version; it has just the methods that we
// currently happen to need in gRPC.  If additional functionality is
// needed before this gets replaced with the absl version, it can be
// added, with the following proviso:
//
// ANY METHOD ADDED HERE MUST COMPLY WITH THE INTERFACE IN THE absl
// IMPLEMENTATION!
//
// TODO(ctiller, nnoble, roth): Replace this with absl::InlinedVector
// once we integrate absl into the gRPC build system in a usable way.
template <typename T, size_t N>
class InlinedVector {
 public:
  InlinedVector() {}
  ~InlinedVector() { gpr_free(dynamic_); }

  // For now, we do not support copying.
  InlinedVector(const InlinedVector&) = delete;
  InlinedVector& operator=(const InlinedVector&) = delete;

  T& operator[](size_t offset) {
    GPR_ASSERT(offset < size_);
    if (offset < N) {
      return inline_[offset];
    } else {
      return dynamic_[offset - N];
    }
  }

  void push_back(const T& value) {
    if (size_ < N) {
      inline_[size_] = value;
    } else {
      const size_t dynamic_size = size_ - N;
      dynamic_ = reinterpret_cast<T*>(
          gpr_realloc(dynamic_, sizeof(T) * dynamic_size + 1));
      dynamic_[dynamic_size] = value;
    }
    ++size_;
  }

  size_t size() const { return size_; }

 private:
  T inline_[N];
  T* dynamic_ = nullptr;
  size_t size_ = 0;
};

}  // namespace grpc_core

#endif
