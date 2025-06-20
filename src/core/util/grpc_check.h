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

#ifndef GRPC_SRC_CORE_UTIL_GRPC_CHECK_H
#define GRPC_SRC_CORE_UTIL_GRPC_CHECK_H

#include "absl/log/check.h"
#include "src/core/util/postmortem_emit.h"

#ifdef GRPC_POSTMORTEM_CHECKS
#define GRPC_CHECK(a)            \
  if (!a) {                      \
    grpc_core::PostMortemEmit(); \
    CHECK(a);                    \
  }

#define GRPC_CHECK_EQ(a, b) \
  if (a != b) {             \
    PostMortemEmit();       \
    CHECK_EQ(a, b);         \
  }

#else

#define GRPC_CHECK(a) CHECK(a)
#define GRPC_CHECK_EQ(a, b) CHECK_EQ(a, b)

#endif

#endif  // GRPC_SRC_CORE_UTIL_GRPC_CHECK_H