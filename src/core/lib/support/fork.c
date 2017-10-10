/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/support/fork.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/env.h"

/*
 * NOTE: FORKING IS NOT GENERALLY SUPPORTED, THIS IS ONLY INTENDED TO WORK
 *       AROUND VERY SPECIFIC USE CASES.
 */

static int override_fork_support_enabled = -1;
static int fork_support_enabled;

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
  if (override_fork_support_enabled != -1) {
    fork_support_enabled = override_fork_support_enabled;
  }
}

int grpc_fork_support_enabled() { return fork_support_enabled; }

void grpc_enable_fork_support(int enable) {
  override_fork_support_enabled = enable;
}
