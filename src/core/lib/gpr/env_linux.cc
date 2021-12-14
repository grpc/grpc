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

/* for secure_getenv. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <grpc/support/port_platform.h>

#ifdef GPR_LINUX_ENV

#include <dlfcn.h>
#include <features.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"

char* gpr_getenv(const char* name) {
  char* result = nullptr;
#if defined(GPR_BACKWARDS_COMPATIBILITY_MODE)
  typedef char* (*getenv_type)(const char*);
  static getenv_type getenv_func = nullptr;
  /* Check to see which getenv variant is supported (go from most
   * to least secure) */
  if (getenv_func == nullptr) {
    const char* names[] = {"secure_getenv", "__secure_getenv", "getenv"};
    for (size_t i = 0; i < GPR_ARRAY_SIZE(names); i++) {
      getenv_func = (getenv_type)dlsym(RTLD_DEFAULT, names[i]);
      if (getenv_func != nullptr) {
        break;
      }
    }
  }
  result = getenv_func(name);
#elif __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 17)
  result = secure_getenv(name);
#else
  result = getenv(name);
#endif
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

#endif /* GPR_LINUX_ENV */
