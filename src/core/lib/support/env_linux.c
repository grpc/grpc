/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

char *gpr_getenv(const char *name) {
#if defined(GPR_BACKWARDS_COMPATIBILITY_MODE)
  typedef char *(*getenv_type)(const char *);
  static getenv_type getenv_func = NULL;
  /* Check to see which getenv variant is supported (go from most
   * to least secure) */
  const char *names[] = {"secure_getenv", "__secure_getenv", "getenv"};
  for (size_t i = 0; getenv_func == NULL && i < GPR_ARRAY_SIZE(names); i++) {
    getenv_func = (getenv_type)dlsym(RTLD_DEFAULT, names[i]);
    if (getenv_func != NULL && strstr(names[i], "secure") == NULL) {
      gpr_log(GPR_DEBUG,
              "Warning: insecure environment read function '%s' used",
              names[i]);
    }
  }
  char *result = getenv_func(name);
  return result == NULL ? result : gpr_strdup(result);
#elif __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 17)
  char *result = secure_getenv(name);
  return result == NULL ? result : gpr_strdup(result);
#else
  gpr_log(GPR_DEBUG, "Warning: insecure environment read function '%s' used",
          "getenv");
  char *result = getenv(name);
  return result == NULL ? result : gpr_strdup(result);
#endif
}

void gpr_setenv(const char *name, const char *value) {
  int res = setenv(name, value, 1);
  GPR_ASSERT(res == 0);
}

#endif /* GPR_LINUX_ENV */
