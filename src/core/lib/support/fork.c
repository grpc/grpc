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

#include "src/core/lib/support/fork.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/env.h"

int fork_support_enabled;

void grpc_fork_support_init() {
#ifdef GRPC_ENABLE_FORK_SUPPORT
  fork_support_enabled = 1;
#else
  fork_support_enabled = 0;
  char *env = gpr_getenv("GRPC_ENABLE_FORK_SUPPORT");
  if (env != NULL) {
    static const char *truthy[] = {"yes",  "Yes",  "YES", "true",
                                   "True", "TRUE", "1"};
    for (size_t i = 0; i < GPR_ARRAY_SIZE(truthy); i++) {
      if (0 == strcmp(env, truthy[i])) {
        fork_support_enabled = 1;
      }
    }
    gpr_free(env);
  }
#endif
}

int grpc_fork_support_enabled() {
  return fork_support_enabled;
}

void grpc_enable_fork_support(int enable) {
  fork_support_enabled = enable;
}
