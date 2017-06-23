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

#ifndef GRPC_CORE_LIB_SUPPORT_ENV_H
#define GRPC_CORE_LIB_SUPPORT_ENV_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Env utility functions */

/* Gets the environment variable value with the specified name.
   Returns a newly allocated string. It is the responsability of the caller to
   gpr_free the return value if not NULL (which means that the environment
   variable exists). */
char *gpr_getenv(const char *name);

/* Sets the the environment with the specified name to the specified value. */
void gpr_setenv(const char *name, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SUPPORT_ENV_H */
