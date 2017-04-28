/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
