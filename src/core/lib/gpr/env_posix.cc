/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_ENV

#include <stdlib.h>

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"

char* gpr_getenv(const char* name) {
  char* result = getenv(name);
  return result == nullptr ? result : gpr_strdup(result);
}

void gpr_setenv(const char* name, const char* value) {
  int res = setenv(name, value, 1);
  GPR_ASSERT(res == 0);
}

void gpr_unsetenv(const char* name) {
  int res = unsetenv(name);
  GPR_ASSERT(res == 0);
}

#endif /* GPR_POSIX_ENV */
