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

#ifndef GRPC_CORE_LIB_GPR_RANDOM_H
#define GRPC_CORE_LIB_GPR_RANDOM_H

#include <grpc/support/port_platform.h>

/* Generate a random number between 0 and 1. We roll our own RNG because seeding
 * rand() modifies a global variable we have no control over. */
double gpr_generate_uniform_random_number(uint32_t* rng_state);

double gpr_generate_uniform_random_number_between(uint32_t* rng_state, double a,
                                                  double b);

#endif  // GRPC_CORE_LIB_GPR_RANDOM_H
