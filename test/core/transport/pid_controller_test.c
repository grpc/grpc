/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/transport/pid_controller.h"

#include <float.h>
#include <math.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

static void test_noop(void) {
  gpr_log(GPR_INFO, "test_noop");
  grpc_pid_controller pid;
  grpc_pid_controller_init(
      &pid, (grpc_pid_controller_args){.gain_p = 1,
                                       .gain_i = 1,
                                       .gain_d = 1,
                                       .initial_control_value = 1,
                                       .min_control_value = DBL_MIN,
                                       .max_control_value = DBL_MAX,
                                       .integral_range = DBL_MAX});
}

static void test_simple_convergence(double gain_p, double gain_i, double gain_d,
                                    double dt, double set_point, double start) {
  gpr_log(GPR_INFO,
          "test_simple_convergence(p=%lf, i=%lf, d=%lf); dt=%lf set_point=%lf "
          "start=%lf",
          gain_p, gain_i, gain_d, dt, set_point, start);
  grpc_pid_controller pid;
  grpc_pid_controller_init(
      &pid, (grpc_pid_controller_args){.gain_p = gain_p,
                                       .gain_i = gain_i,
                                       .gain_d = gain_d,
                                       .initial_control_value = start,
                                       .min_control_value = DBL_MIN,
                                       .max_control_value = DBL_MAX,
                                       .integral_range = DBL_MAX});

  for (int i = 0; i < 100000; i++) {
    grpc_pid_controller_update(&pid, set_point - grpc_pid_controller_last(&pid),
                               1);
  }

  GPR_ASSERT(fabs(set_point - grpc_pid_controller_last(&pid)) < 0.1);
  if (gain_i > 0) {
    GPR_ASSERT(fabs(pid.error_integral) < 0.1);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_noop();
  test_simple_convergence(0.2, 0, 0, 1, 100, 0);
  test_simple_convergence(0.2, 0.1, 0, 1, 100, 0);
  test_simple_convergence(0.2, 0.1, 0.1, 1, 100, 0);
  return 0;
}
