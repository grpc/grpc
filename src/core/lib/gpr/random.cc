/*
 *
 * Copyright 2020 gRPC authors.
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

#include "src/core/lib/gpr/random.h"
#include "src/core/lib/gpr/useful.h"

double gpr_generate_uniform_random_number(uint32_t* rng_state) {
  constexpr uint32_t two_raise_31 = uint32_t(1) << 31;
  *rng_state = (1103515245 * *rng_state + 12345) % two_raise_31;
  return *rng_state / static_cast<double>(two_raise_31);
}

double gpr_generate_uniform_random_number_between(uint32_t* rng_state, double a,
                                                  double b) {
  if (a == b) return a;
  if (a > b) GPR_SWAP(double, a, b);  // make sure a < b
  const double range = b - a;
  return a + gpr_generate_uniform_random_number(rng_state) * range;
}
