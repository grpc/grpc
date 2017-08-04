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
