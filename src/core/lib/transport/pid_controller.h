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

#ifndef GRPC_CORE_LIB_TRANSPORT_PID_CONTROLLER_H
#define GRPC_CORE_LIB_TRANSPORT_PID_CONTROLLER_H

/* \file Simple PID controller.
   Implements a proportional-integral-derivative controller.
   Used when we want to iteratively control a variable to converge some other
   observed value to a 'set-point'.
   Gains can be set to adjust sensitivity to current error (p), the integral
   of error (i), and the derivative of error (d). */

typedef struct {
  double gain_p;
  double gain_i;
  double gain_d;
  double initial_control_value;
  double min_control_value;
  double max_control_value;
  double integral_range;
} grpc_pid_controller_args;

typedef struct {
  double last_error;
  double error_integral;
  double last_control_value;
  double last_dc_dt;
  grpc_pid_controller_args args;
} grpc_pid_controller;

/** Initialize the controller */
void grpc_pid_controller_init(grpc_pid_controller *pid_controller,
                              grpc_pid_controller_args args);

/** Reset the controller: useful when things have changed significantly */
void grpc_pid_controller_reset(grpc_pid_controller *pid_controller);

/** Update the controller: given a current error estimate, and the time since
    the last update, returns a new control value */
double grpc_pid_controller_update(grpc_pid_controller *pid_controller,
                                  double error, double dt);

/** Returns the last control value calculated */
double grpc_pid_controller_last(grpc_pid_controller *pid_controller);

#endif /* GRPC_CORE_LIB_TRANSPORT_PID_CONTROLLER_H */
