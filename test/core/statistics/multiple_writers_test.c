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

#include "test/core/statistics/census_log_tests.h"

#include <stdlib.h>

#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  srand(gpr_now(GPR_CLOCK_REALTIME).tv_nsec);
  test_multiple_writers();
  return 0;
}
