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
#error Verifying GRPC_POSTMORTEM_CHECKS set on CI

#include <limits.h>

#include "src/core/util/postmortem_emit.h"
#include "absl/log/log.h"

bool PostMortemEmitAndReturnTrue();

#define GRPC_CHECK(a)                           \
  while (!(a) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK(" #a "). "

#define GRPC_CHECK_EQ(a, b)                              \
  while (!((a) == (b)) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK_EQ(" #a ", " #b "). "

#define GRPC_CHECK_NE(a, b)                           \
  while ((a) == (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK_NE(" #a ", " #b "). "

#define GRPC_CHECK_GT(a, b)                           \
  while ((a) <= (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK_GT(" #a ", " #b "). "

#define GRPC_CHECK_LT(a, b)                           \
  while ((a) >= (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK_LT(" #a ",  " #b "). "

#define GRPC_CHECK_GE(a, b)                          \
  while ((a) < (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK_GE(" #a ", " #b "). "

#define GRPC_CHECK_LE(a, b)                          \
  while ((a) > (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK_LE(" #a " vs " #b ")."

#define GRPC_CHECK_OK(a)                             \
  while (!(a).ok() && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed GRPC_CHECK_OK(" #a "). "

#ifndef NDEBUG
#define GRPC_DCHECK(a) GRPC_CHECK(a)
#define GRPC_DCHECK_EQ(a, b) GRPC_CHECK_EQ(a, b)
#define GRPC_DCHECK_GE(a, b) GRPC_CHECK_GE(a, b)
#define GRPC_DCHECK_LE(a, b) GRPC_CHECK_LE(a, b)
#define GRPC_DCHECK_GT(a, b) GRPC_CHECK_GT(a, b)
#define GRPC_DCHECK_LT(a, b) GRPC_CHECK_LT(a, b)
#define GRPC_DCHECK_NE(a, b) GRPC_CHECK_NE(a, b)
#else  // NDEBUG
#define GRPC_DCHECK(a) \
  while (false) LOG(INFO)
#define GRPC_DCHECK_EQ(a, b) \
  while (false) LOG(INFO)
#define GRPC_DCHECK_GE(a, b) \
  while (false) LOG(INFO)
#define GRPC_DCHECK_LE(a, b) \
  while (false) LOG(INFO)
#define GRPC_DCHECK_GT(a, b) \
  while (false) LOG(INFO)
#define GRPC_DCHECK_LT(a, b) \
  while (false) LOG(INFO)
#define GRPC_DCHECK_NE(a, b) \
  while (false) LOG(INFO)
#endif

#else  // GRPC_POSTMORTEM_CHECKS
#include "absl/log/check.h"

#define GRPC_CHECK(a) CHECK(a)
#define GRPC_CHECK_EQ(a, b) CHECK_EQ(a, b)
#define GRPC_CHECK_NE(a, b) CHECK_NE(a, b)
#define GRPC_CHECK_GT(a, b) CHECK_GT(a, b)
#define GRPC_CHECK_LT(a, b) CHECK_LT(a, b)
#define GRPC_CHECK_GE(a, b) CHECK_GE(a, b)
#define GRPC_CHECK_LE(a, b) CHECK_LE(a, b)
#define GRPC_CHECK_OK(a) CHECK_OK(a)
#define GRPC_DCHECK(a) DCHECK(a)
#define GRPC_DCHECK_EQ(a, b) DCHECK_EQ(a, b)
#define GRPC_DCHECK_GE(a, b) DCHECK_GE(a, b)
#define GRPC_DCHECK_LE(a, b) DCHECK_LE(a, b)
#define GRPC_DCHECK_GT(a, b) DCHECK_GT(a, b)
#define GRPC_DCHECK_LT(a, b) DCHECK_LT(a, b)
#define GRPC_DCHECK_NE(a, b) DCHECK_NE(a, b)
#endif  // GRPC_POSTMORTEM_CHECKS

#endif  // GRPC_SRC_CORE_UTIL_GRPC_CHECK_H