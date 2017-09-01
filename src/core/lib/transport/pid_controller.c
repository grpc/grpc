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
#include <grpc/support/useful.h>

void grpc_pid_controller_init(grpc_pid_controller *pid_controller,
                              grpc_pid_controller_args args) {
  pid_controller->args = args;
  pid_controller->last_control_value = args.initial_control_value;
  grpc_pid_controller_reset(pid_controller);
}

void grpc_pid_controller_reset(grpc_pid_controller *pid_controller) {
  pid_controller->last_error = 0.0;
  pid_controller->last_dc_dt = 0.0;
  pid_controller->error_integral = 0.0;
}

double grpc_pid_controller_update(grpc_pid_controller *pid_controller,
                                  double error, double dt) {
  if (dt == 0) return pid_controller->last_control_value;
  /* integrate error using the trapezoid rule */
  pid_controller->error_integral +=
      dt * (pid_controller->last_error + error) * 0.5;
  pid_controller->error_integral = GPR_CLAMP(
      pid_controller->error_integral, -pid_controller->args.integral_range,
      pid_controller->args.integral_range);
  double diff_error = (error - pid_controller->last_error) / dt;
  /* calculate derivative of control value vs time */
  double dc_dt = pid_controller->args.gain_p * error +
                 pid_controller->args.gain_i * pid_controller->error_integral +
                 pid_controller->args.gain_d * diff_error;
  /* and perform trapezoidal integration */
  double new_control_value = pid_controller->last_control_value +
                             dt * (pid_controller->last_dc_dt + dc_dt) * 0.5;
  new_control_value =
      GPR_CLAMP(new_control_value, pid_controller->args.min_control_value,
                pid_controller->args.max_control_value);
  pid_controller->last_error = error;
  pid_controller->last_dc_dt = dc_dt;
  pid_controller->last_control_value = new_control_value;
  return new_control_value;
}

double grpc_pid_controller_last(grpc_pid_controller *pid_controller) {
  return pid_controller->last_control_value;
}
