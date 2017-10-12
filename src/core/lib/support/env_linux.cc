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

#include "src/core/lib/support/env.h"

#include <dlfcn.h>
#include <features.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/string.h"

const char *gpr_getenv_silent(const char *name, char **dst) {
  const char *insecure_func_used = NULL;
  char *result = NULL;
#if defined(GPR_BACKWARDS_COMPATIBILITY_MODE)
  typedef char *(*getenv_type)(const char *);
  static getenv_type getenv_func = NULL;
  /* Check to see which getenv variant is supported (go from most
   * to least secure) */
  const char *names[] = {"secure_getenv", "__secure_getenv", "getenv"};
  for (size_t i = 0; getenv_func == NULL && i < GPR_ARRAY_SIZE(names); i++) {
    getenv_func = (getenv_type)dlsym(RTLD_DEFAULT, names[i]);
    if (getenv_func != NULL && strstr(names[i], "secure") == NULL) {
      insecure_func_used = names[i];
    }
  }
  result = getenv_func(name);
#elif __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 17)
  result = secure_getenv(name);
#else
  result = getenv(name);
  insecure_func_used = "getenv";
#endif
  *dst = result == NULL ? result : gpr_strdup(result);
  return insecure_func_used;
}

char *gpr_getenv(const char *name) {
  char *result = NULL;
  const char *insecure_func_used = gpr_getenv_silent(name, &result);
  if (insecure_func_used != NULL) {
    gpr_log(GPR_DEBUG, "Warning: insecure environment read function '%s' used",
            insecure_func_used);
  }
  return result;
}

void gpr_setenv(const char *name, const char *value) {
  int res = setenv(name, value, 1);
  GPR_ASSERT(res == 0);
}

#endif /* GPR_LINUX_ENV */
