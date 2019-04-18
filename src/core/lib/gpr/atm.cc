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

#include <grpc/support/atm.h>

#include "src/core/lib/gpr/useful.h"

gpr_atm gpr_atm_no_barrier_clamped_add(gpr_atm* value, gpr_atm delta,
                                       gpr_atm min, gpr_atm max) {
  gpr_atm current_value;
  gpr_atm new_value;
  do {
    current_value = gpr_atm_no_barrier_load(value);
    new_value = GPR_CLAMP(current_value + delta, min, max);
    if (new_value == current_value) break;
  } while (!gpr_atm_no_barrier_cas(value, current_value, new_value));
  return new_value;
}
