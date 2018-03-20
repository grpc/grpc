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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gpr/fork.h"

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/useful.h"

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
#endif
  bool env_var_set = false;
  char* env = gpr_getenv("GRPC_ENABLE_FORK_SUPPORT");
  if (env != nullptr) {
    static const char* truthy[] = {"yes",  "Yes",  "YES", "true",
                                   "True", "TRUE", "1"};
    static const char* falsey[] = {"no",    "No",    "NO", "false",
                                   "False", "FALSE", "0"};
    for (size_t i = 0; i < GPR_ARRAY_SIZE(truthy); i++) {
      if (0 == strcmp(env, truthy[i])) {
        fork_support_enabled = 1;
        env_var_set = true;
        break;
      }
    }
    if (!env_var_set) {
      for (size_t i = 0; i < GPR_ARRAY_SIZE(falsey); i++) {
        if (0 == strcmp(env, falsey[i])) {
          fork_support_enabled = 0;
          env_var_set = true;
          break;
        }
      }
    }
    gpr_free(env);
  }
  if (override_fork_support_enabled != -1) {
    fork_support_enabled = override_fork_support_enabled;
  }
}

int grpc_fork_support_enabled() { return fork_support_enabled; }

void grpc_enable_fork_support(int enable) {
  override_fork_support_enabled = enable;
}
