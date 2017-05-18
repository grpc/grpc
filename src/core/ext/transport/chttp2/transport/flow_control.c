/*
 *
 * Copyright 2015, Google Inc.
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

#include "src/core/ext/transport/chttp2/transport/flow_control.h"

#include <math.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

grpc_chttp2_flow_control_action grpc_chttp2_check_for_flow_control_action(
    grpc_chttp2_transport* t, grpc_chttp2_stream* s) {
  grpc_chttp2_flow_control_action action;
  memset(&action, 0, sizeof(action));
  if (t->enable_bdp_probe) {
    // check for needed ping
    if (grpc_bdp_estimator_need_ping(&t->bdp_estimator)) {
      action.send_bdp_ping = GRPC_CHTTP2_FLOW_CONTROL_UPDATE_IMMEDIATELY;
    }
    int64_t estimate = -1;
    if (grpc_bdp_estimator_get_estimate(&t->bdp_estimator, &estimate)) {
      double target = 1 + log2((double)estimate);
      double memory_pressure = grpc_resource_quota_get_memory_pressure(
          grpc_resource_user_quota(grpc_endpoint_get_resource_user(t->ep)));
      if (memory_pressure > 0.8) {
        target *= 1 - GPR_MIN(1, (memory_pressure - 0.8) / 0.1);
      }
      double bdp_error = target - grpc_pid_controller_last(&t->pid_controller);
      gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
      gpr_timespec dt_timespec = gpr_time_sub(now, t->last_pid_update);
      double dt = (double)dt_timespec.tv_sec + dt_timespec.tv_nsec * 1e-9;
      if (dt > 0.1) {
        dt = 0.1;
      }
      double log2_bdp_guess =
          grpc_pid_controller_update(&t->pid_controller, bdp_error, dt);
      t->last_pid_update = now;
      double bdp_dbl = pow(2, log2_bdp_guess);
      action.send_transport_update =
          GRPC_CHTTP2_FLOW_CONTROL_UPDATE_IMMEDIATELY;
      int64_t bdp;
      const int64_t kMinBDP = 128;
      if (bdp_dbl <= kMinBDP) {
        bdp = kMinBDP;
      } else if (bdp_dbl > INT32_MAX) {
        bdp = INT32_MAX;
      } else {
        bdp = (int64_t)(bdp_dbl);
      }
      int64_t delta =
          (int64_t)bdp -
          (int64_t)t->settings[GRPC_LOCAL_SETTINGS]
                              [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
      if (delta != 0 && (delta <= -bdp / 10 || delta >= bdp / 10)) {
        action.send_transport_update =
            GRPC_CHTTP2_FLOW_CONTROL_UPDATE_IMMEDIATELY;
        action.announce_transport_window = bdp;
        action.send_max_frame_update =
            GRPC_CHTTP2_FLOW_CONTROL_UPDATE_IMMEDIATELY;
        action.announce_max_frame = bdp;
      }
    }
  }
  return action;
}
