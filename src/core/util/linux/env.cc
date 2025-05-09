//
//
// Copyright 2015 gRPC authors.
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
//
//

// for secure_getenv.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <grpc/support/port_platform.h>

#include <optional>
#include <string>

#ifdef GPR_LINUX_ENV

#include <features.h>
#include <stdlib.h>

#include "src/core/util/env.h"

namespace grpc_core {

std::optional<std::string> GetEnv(const char* name) {
  char* result = nullptr;
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 17)) && \
    !defined(GRPC_FORCE_UNSECURE_GETENV)
  result = secure_getenv(name);
#else
  result = getenv(name);

#endif
  if (result == nullptr) return std::nullopt;
  return result;
}

void SetEnv(const char* name, const char* value) {
  int res = setenv(name, value, 1);
  if (res != 0) abort();
}

void UnsetEnv(const char* name) {
  int res = unsetenv(name);
  if (res != 0) abort();
}

}  // namespace grpc_core

#endif  // GPR_LINUX_ENV
