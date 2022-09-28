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

#include <grpc/support/port_platform.h>

#include "src/core/lib/slice/slice_refcount.h"

#include <chrono>

#include <grpc/slice.h>

#include "src/core/lib/slice/slice_refcount_base.h"

namespace grpc_core {

uint32_t g_hash_seed = []() {
  auto now = std::chrono::system_clock::now();
  auto now_since_epoch = now.time_since_epoch();
  auto now_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now_since_epoch);
  return static_cast<uint32_t>(now_ns.count());
}();

}  // namespace grpc_core

void grpc_test_only_set_slice_hash_seed(uint32_t seed) {
  grpc_core::g_hash_seed = seed;
}

grpc_slice grpc_slice_ref(grpc_slice slice) {
  if (reinterpret_cast<uintptr_t>(slice.refcount) > 1) {
    slice.refcount->Ref();
  }
  return slice;
}

void grpc_slice_unref(grpc_slice slice) {
  if (reinterpret_cast<uintptr_t>(slice.refcount) > 1) {
    slice.refcount->Unref();
  }
}
