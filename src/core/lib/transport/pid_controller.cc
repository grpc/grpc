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

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

PidController::PidController(const Args& args)
    : last_control_value_(args.initial_control_value()), args_(args) {}

double PidController::Update(double error, double dt) {
  if (dt <= 0) return last_control_value_;
  /* integrate error using the trapezoid rule */
  error_integral_ += dt * (last_error_ + error) * 0.5;
  error_integral_ = GPR_CLAMP(error_integral_, -args_.integral_range(),
                              args_.integral_range());
  double diff_error = (error - last_error_) / dt;
  /* calculate derivative of control value vs time */
  double dc_dt = args_.gain_p() * error + args_.gain_i() * error_integral_ +
                 args_.gain_d() * diff_error;
  /* and perform trapezoidal integration */
  double new_control_value =
      last_control_value_ + dt * (last_dc_dt_ + dc_dt) * 0.5;
  new_control_value = GPR_CLAMP(new_control_value, args_.min_control_value(),
                                args_.max_control_value());
  last_error_ = error;
  last_dc_dt_ = dc_dt;
  last_control_value_ = new_control_value;
  return new_control_value;
}

}  // namespace grpc_core
