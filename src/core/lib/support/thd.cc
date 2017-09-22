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

/* Posix implementation for gpr threads. */

#include <string.h>

#include <grpc/support/thd.h>

enum { GPR_THD_JOINABLE = 1 };

gpr_thd_options gpr_thd_options_default(void) {
  gpr_thd_options options;
  memset(&options, 0, sizeof(options));
  return options;
}

void gpr_thd_options_set_detached(gpr_thd_options* options) {
  options->flags &= ~GPR_THD_JOINABLE;
}

void gpr_thd_options_set_joinable(gpr_thd_options* options) {
  options->flags |= GPR_THD_JOINABLE;
}

int gpr_thd_options_is_detached(const gpr_thd_options* options) {
  if (!options) return 1;
  return (options->flags & GPR_THD_JOINABLE) == 0;
}

int gpr_thd_options_is_joinable(const gpr_thd_options* options) {
  if (!options) return 0;
  return (options->flags & GPR_THD_JOINABLE) == GPR_THD_JOINABLE;
}
