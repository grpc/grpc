// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_GPRPP_RANDOM_H
#define GRPC_SRC_CORE_LIB_GPRPP_RANDOM_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <cstdint>
#include <vector>

#include "absl/random/random.h"

namespace grpc_core {

// Wrap a random number generator that's compatible with absl::BitGen (same
// types, range) in a polymorphic reference type.
// Allows for fuzzing of things that depend on rngs.
class BitSourceRef {
 public:
  using result_type = absl::BitGen::result_type;

  BitSourceRef(const BitSourceRef&) = default;
  BitSourceRef& operator=(const BitSourceRef&) = default;

  BitSourceRef(BitSourceRef& other)
      : BitSourceRef(const_cast<const BitSourceRef&>(other)) {}

  template <typename T>
  explicit BitSourceRef(T& source)
      : ptr_(&source), impl_([](void* p) { return (*static_cast<T*>(p))(); }) {}
  explicit BitSourceRef(uint64_t (*source)())
      : ptr_(reinterpret_cast<void*>(source)),
        impl_([](void* p) { return reinterpret_cast<uint64_t (*)()>(p)(); }) {}

  static constexpr result_type min() { return absl::BitGen::min(); }
  static constexpr result_type max() { return absl::BitGen::max(); }
  result_type operator()() { return impl_(ptr_); }

 private:
  void* ptr_;
  result_type (*impl_)(void* ptr);
};

// Set of random numbers from a proto file (or other container) forming a bit
// source.
class ProtoBitSource {
 public:
  template <typename SourceContainer>
  explicit ProtoBitSource(const SourceContainer& c) {
    for (auto r : c) {
      results_.push_back(Clamp(r, BitSourceRef::min(), BitSourceRef::max()));
    }
  }

  uint64_t operator()() {
    if (current_ == results_.size()) return 0;
    uint64_t out = results_[current_];
    ++current_;
    return out;
  }

 private:
  std::vector<uint64_t> results_;
  size_t current_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_RANDOM_H
