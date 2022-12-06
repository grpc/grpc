// Copyright 2022 gRPC authors.
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
#ifndef GRPC_CORE_LIB_GPR_LOG_INTERNAL_H
#define GRPC_CORE_LIB_GPR_LOG_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <stdio.h>
#include <stdlib.h>

#include <cstring>

/** abort() the process if x is zero, with rudimentary logging to prevent
   circular dependencies with gpr_log.

   Intended for internal invariants.  If the error can be recovered from,
   without the possibility of corruption, or might best be reflected via
   an exception in a higher-level language, consider returning error code.  */
#define GPR_ASSERT_INTERNAL(x)                     \
  do {                                             \
    if (GPR_UNLIKELY(!(x))) {                      \
      fprintf(stderr, "assertion failed: %s", #x); \
      abort();                                     \
    }                                              \
  } while (0)

#ifndef NDEBUG
#define GPR_DEBUG_ASSERT_INTERNAL(x) GPR_ASSERT_INTERNAL(x)
#else
#define GPR_DEBUG_ASSERT_INTERNAL(x)
#endif

#define GPR_LOG_ERROR_INTERNAL(format, ...)                       \
  do {                                                            \
    char f[] = __FILE__;                                          \
    char* display_file = f;                                       \
    char* slash_pos = strrchr(f, '/');                            \
    if (slash_pos != nullptr) display_file = slash_pos + 1;       \
    char prefix[60];                                              \
    sprintf(prefix, "INTERNAL %37s:%d]", display_file, __LINE__); \
    fprintf(stderr, "%-60s " format "\n", prefix, __VA_ARGS__);   \
  } while (0)

#endif  // GRPC_CORE_LIB_GPR_LOG_INTERNAL_H
