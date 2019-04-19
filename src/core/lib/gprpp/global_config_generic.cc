/*
 *
 * Copyright 2019 gRPC authors.
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

#include "src/core/lib/gprpp/global_config_generic.h"
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

static void gpr_global_config_error_default_function(
    const char* error_message) {
  gpr_log(GPR_ERROR, "%s", error_message);
}

static gpr_atm g_global_config_error_func =
    (gpr_atm)gpr_global_config_error_default_function;

void gpr_set_global_config_error_function(gpr_global_config_error_func func) {
  gpr_atm_no_barrier_store(&g_global_config_error_func, (gpr_atm)func);
}

void gpr_call_global_config_error_function(const char* error_message) {
  ((gpr_global_config_error_func)gpr_atm_no_barrier_load(
      &g_global_config_error_func))(error_message);
}
