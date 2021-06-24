// Copyright 2021 The gRPC Authors
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

#include "absl/random/internal/randen_slow.h"

#include "absl/random/internal/randen_hwaes.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// This module is a custome implementation of RandenHwAes forarding all ops
// to RandenSlow. This is devised to address the issue where some build
// targets (e.g. php) which cannot specify the compiler options to allow to
// understand instrinsics used in the actual RandenHwAes implementation.
// (https://github.com/grpc/grpc/issues/26478)

void RandenHwAes::Generate(const void* keys, void* state_void) {
  return RandenSlow::Generate(keys, state_void);
}

void RandenHwAes::Absorb(const void* seed_void, void* state_void) {
  return RandenSlow::Absorb(seed_void, state_void);
}

const void* RandenHwAes::GetKeys() {
  return RandenSlow::GetKeys();
}

bool HasRandenHwAesImplementation() {
  return false;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
