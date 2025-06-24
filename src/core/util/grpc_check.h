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
#include "absl/log/log.h"
#include "src/core/util/postmortem_emit.h"
#include <limits.h>

bool PostMortemEmitAndReturnTrue();

#undef CHECK
#define CHECK(a) \
  if (!(a) && PostMortemEmitAndReturnTrue()) LOG(FATAL) << "Failed CHECK(#a). "

#undef CHECK_EQ
#define CHECK_EQ(a, b)                       \
  if (!((a) == (b)) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed CHECK_EQ(#a, #b). "

#undef CHECK_NE
#define CHECK_NE(a, b)                     \
  if ((a) == (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed CHECK_NE(#a, #b). "

#undef CHECK_GT
#define CHECK_GT(a, b)                     \
  if ((a) <= (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed CHECK_GT(#a, #b). "

#undef CHECK_LT
#define CHECK_LT(a, b)                     \
  if ((a) >= (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed CHECK_LT(#a,  #b). "

#undef CHECK_GE
#define CHECK_GE(a, b)                    \
  if ((a) < (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed CHECK_GE(#a, #b). "

#undef CHECK_LE
#define CHECK_LE(a, b)                    \
  if ((a) > (b) && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed CHECK_LE: #a vs #b"

#undef CHECK_OK
#define CHECK_OK(a)                       \
  if (!(a).ok() && PostMortemEmitAndReturnTrue()) \
  LOG(FATAL) << "Failed "                 \
                "CHECK_OK(#a). "

#undef DCHECK
#undef DCHECK_GE
#undef DCHECK_LE
#undef DCHECK_GT
#undef DCHECK_LT
#undef DCHECK_NE
#ifndef NDEBUG
#define DCHECK(a) CHECK(a)
#define DCHECK_GE(a, b) CHECK_GE(a, b)
#define DCHECK_LE(a, b) CHECK_LE(a, b)
#define DCHECK_GT(a, b) CHECK_GT(a, b)
#define DCHECK_LT(a, b) CHECK_LT(a, b)
#define DCHECK_NE(a, b) CHECK_NE(a, b)
#else
// VLOG(INT_MAX) effectively sends logs nowhere
#define DCHECK(a) VLOG(INT_MAX)
#define DCHECK_GE(a, b) VLOG(INT_MAX)
#define DCHECK_LE(a, b) VLOG(INT_MAX)
#define DCHECK_GT(a, b) VLOG(INT_MAX)
#define DCHECK_LT(a, b) VLOG(INT_MAX)
#define DCHECK_NE(a, b) VLOG(INT_MAX)
#endif

#else
#include "absl/log/check.h"
#endif

#endif  // GRPC_SRC_CORE_UTIL_GRPC_CHECK_H