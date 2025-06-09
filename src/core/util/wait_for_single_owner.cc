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

#include "src/core/util/wait_for_single_owner.h"

namespace grpc_core {

// Copied from test/core/test_util/build.cc
// Define GRPC_BUILD_HAS_ASAN as 1 or 0 depending on if we're building under
// ASAN.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GRPC_BUILD_HAS_ASAN 1
#else
#define GRPC_BUILD_HAS_ASAN 0
#endif
#else
#ifdef ADDRESS_SANITIZER
#define GRPC_BUILD_HAS_ASAN 1
#else
#define GRPC_BUILD_HAS_ASAN 0
#endif
#endif

#if GRPC_BUILD_HAS_ASAN
#include <sanitizer/lsan_interface.h>

void AsanAssertNoLeaks() { __lsan_do_leak_check(); }
#else  // GRPC_BUILD_HAS_ASAN
void AsanAssertNoLeaks() {}
#endif  // GRPC_BUILD_HAS_ASAN

}  // namespace grpc_core