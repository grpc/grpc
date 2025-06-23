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

#ifdef GRPC_POSTMORTEM_CHECKS
#include "src/core/util/crash.h"
#include "src/core/util/postmortem_emit.h"

#undef CHECK
#define CHECK(a)                          \
  if (!(a)) {                             \
    grpc_core::PostMortemEmit();          \
    grpc_core::Crash("Failed CHECK: #a"); \
  }

#undef CHECK_EQ
#define CHECK_EQ(a, b)                             \
  if ((a) != (b)) {                                \
    grpc_core::PostMortemEmit();                   \
    grpc_core::Crash("Failed CHECK_EQ: #a vs #b"); \
  }

#undef CHECK_NE
#define CHECK_NE(a, b)                             \
  if ((a) == (b)) {                                \
    grpc_core::PostMortemEmit();                   \
    grpc_core::Crash("Failed CHECK_NE: #a vs #b"); \
  }

#undef CHECK_GT
#define CHECK_GT(a, b)                             \
  if ((a) <= (b)) {                                \
    grpc_core::PostMortemEmit();                   \
    grpc_core::Crash("Failed CHECK_GT: #a vs #b"); \
  }

#undef CHECK_LT
#define CHECK_LT(a, b)                             \
  if ((a) >= (b)) {                                \
    grpc_core::PostMortemEmit();                   \
    grpc_core::Crash("Failed CHECK_LT: #a vs #b"); \
  }

#undef CHECK_GE
#define CHECK_GE(a, b)                             \
  if ((a) < (b)) {                                 \
    grpc_core::PostMortemEmit();                   \
    grpc_core::Crash("Failed CHECK_GE: #a vs #b"); \
  }

#undef CHECK_LE
#define CHECK_LE(a, b)                             \
  if ((a) > (b)) {                                 \
    grpc_core::PostMortemEmit();                   \
    grpc_core::Crash("Failed CHECK_LE: #a vs #b"); \
  }

#ifndef NDEBUG
#undef DCHECK
#define DCHECK(a)                          \
  if (!(a)) {                              \
    grpc_core::PostMortemEmit();           \
    grpc_core::Crash("Failed DCHECK: #a"); \
  }

#undef DCHECK_GE
#define DCHECK_GE(a, b)                             \
  if ((a) < (b)) {                                  \
    grpc_core::PostMortemEmit();                    \
    grpc_core::Crash("Failed DCHECK_GE: #a vs #b"); \
  }

#undef DCHECK_LE
#define DCHECK_LE(a, b)                             \
  if ((a) > (b)) {                                  \
    grpc_core::PostMortemEmit();                    \
    grpc_core::Crash("Failed DCHECK_LE: #a vs #b"); \
  }
#else
#undef DCHECK
#define DCHECK(a)
#undef DCHECK_GE
#define DCHECK_GE(a, b)
#undef DCHECK_LE
#define DCHECK_LE(a, b)
#endif

#else
#include "absl/log/check.h"
#endif

#endif  // GRPC_SRC_CORE_UTIL_GRPC_CHECK_H