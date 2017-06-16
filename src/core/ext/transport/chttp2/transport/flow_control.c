/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/flow_control.h"

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/string.h"

grpc_tracer_flag grpc_flowctl_trace = GRPC_TRACER_INITIALIZER(false);

#define DEFAULT_WINDOW 65535;

#define GRPC_FLOW_CONTROL_IF_TRACING(stmt)   \
  if (!(GRPC_TRACER_ON(grpc_flowctl_trace))) \
    ;                                        \
  else                                       \
  stmt

void grpc_chttp2_flowctl_trace(const char *file, int line, bool credit,
                               const char *ctx, const char *var, int64_t prev,
                               uint32_t delta);

#define FLOW_CONTROL_CREDIT(file, line, ctx, var, delta)                      \
  if (GRPC_TRACER_ON(grpc_flowctl_trace)) {                                   \
    grpc_chttp2_flowctl_trace(file, line, true, #ctx, #var, ctx->var, delta); \
  }                                                                           \
  ctx->var += delta;

#define FLOW_CONTROL_DEBIT(file, line, ctx, var, delta)                        \
  if (GRPC_TRACER_ON(grpc_flowctl_trace)) {                                    \
    grpc_chttp2_flowctl_trace(file, line, false, #ctx, #var, ctx->var, delta); \
  }                                                                            \
  ctx->var -= delta;

/*******************************************************************************
 * INTERFACE
 */

/* if there is any disparity between local and announced, send an update */
// TODO(ncteisen): tune this. No need to send an update if there is plenty
// of room
uint32_t grpc_chttp2_flow_control_get_stream_announce(
    grpc_chttp2_stream_flow_control_data *sfc, uint32_t initial_window) {
  uint32_t stream_announced_window =
      (uint32_t)(sfc->announced_local_window_delta + initial_window);
  if (stream_announced_window < 32768) {
    return (uint32_t)(sfc->local_window_delta -
                      sfc->announced_local_window_delta);

  } else {
    return 0;
  }
}

uint32_t grpc_chttp2_flow_control_get_transport_announce(
    grpc_chttp2_transport_flow_control_data *tfc) {
  if (tfc->announced_local_window < 32768) {
    return (uint32_t)(tfc->local_window - tfc->announced_local_window);
  } else {
    return 0;
  }
}

void grpc_chttp2_flow_control_transport_init(
    grpc_chttp2_transport_flow_control_data *tfc) {
  GRPC_FLOW_CONTROL_IF_TRACING(gpr_log(
      GPR_DEBUG, "Initiating transport flow control. All values default"));
  tfc->remote_window = DEFAULT_WINDOW;
  tfc->local_window = DEFAULT_WINDOW;
  tfc->announced_local_window = DEFAULT_WINDOW;
}

void grpc_chttp2_flow_control_stream_init(
    grpc_chttp2_stream_flow_control_data *sfc) {
  GRPC_FLOW_CONTROL_IF_TRACING(
      gpr_log(GPR_DEBUG, "Initiating stream flow control. All deltas 0"));
  GPR_ASSERT(sfc->remote_window_delta == 0);
  GPR_ASSERT(sfc->local_window_delta == 0);
  GPR_ASSERT(sfc->announced_local_window_delta == 0);
}

void _grpc_chttp2_flow_control_credit_local_transport(
    grpc_chttp2_transport_flow_control_data *tfc, uint32_t val,
    const char *file, int line) {
  FLOW_CONTROL_CREDIT(file, line, tfc, local_window, val);
}

void _grpc_chttp2_flow_control_debit_local_transport(
    grpc_chttp2_transport_flow_control_data *tfc, uint32_t val,
    const char *file, int line) {
  FLOW_CONTROL_DEBIT(file, line, tfc, local_window, val);
}

void _grpc_chttp2_flow_control_credit_local_stream(
    grpc_chttp2_stream_flow_control_data *sfc, uint32_t val, const char *file,
    int line) {
  FLOW_CONTROL_CREDIT(file, line, sfc, local_window_delta, val);
}

void _grpc_chttp2_flow_control_debit_local_stream(
    grpc_chttp2_stream_flow_control_data *sfc, uint32_t val, const char *file,
    int line) {
  FLOW_CONTROL_DEBIT(file, line, sfc, local_window_delta, val);
}

void _grpc_chttp2_flow_control_announce_credit_transport(
    grpc_chttp2_transport_flow_control_data *tfc, uint32_t val,
    const char *file, int line) {
  FLOW_CONTROL_CREDIT(file, line, tfc, announced_local_window, val);
}

void _grpc_chttp2_flow_control_announce_debit_transport(
    grpc_chttp2_transport_flow_control_data *tfc, uint32_t val,
    const char *file, int line) {
  FLOW_CONTROL_DEBIT(file, line, tfc, announced_local_window, val);
}

void _grpc_chttp2_flow_control_announce_credit_stream(
    grpc_chttp2_stream_flow_control_data *sfc, uint32_t val, const char *file,
    int line) {
  FLOW_CONTROL_CREDIT(file, line, sfc, announced_local_window_delta, val);
}

void _grpc_chttp2_flow_control_announce_debit_stream(
    grpc_chttp2_stream_flow_control_data *sfc, uint32_t val, const char *file,
    int line) {
  FLOW_CONTROL_DEBIT(file, line, sfc, announced_local_window_delta, val);
}

void _grpc_chttp2_flow_control_credit_remote_transport(
    grpc_chttp2_transport_flow_control_data *tfc, uint32_t val,
    const char *file, int line) {
  FLOW_CONTROL_CREDIT(file, line, tfc, remote_window, val);
}

void _grpc_chttp2_flow_control_debit_remote_transport(
    grpc_chttp2_transport_flow_control_data *tfc, uint32_t val,
    const char *file, int line) {
  FLOW_CONTROL_DEBIT(file, line, tfc, remote_window, val);
}

void _grpc_chttp2_flow_control_credit_remote_stream(
    grpc_chttp2_stream_flow_control_data *sfc, uint32_t val, const char *file,
    int line) {
  FLOW_CONTROL_CREDIT(file, line, sfc, remote_window_delta, val);
}

void _grpc_chttp2_flow_control_debit_remote_stream(
    grpc_chttp2_stream_flow_control_data *sfc, uint32_t val, const char *file,
    int line) {
  FLOW_CONTROL_DEBIT(file, line, sfc, remote_window_delta, val);
}

void grpc_chttp2_flowctl_trace(const char *file, int line, bool credit,
                               const char *ctx, const char *var, int64_t prev,
                               uint32_t delta) {
  if (credit) {
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "crediting %s->%s by %" PRIu32 ": %" PRId64 " -> %" PRId64, ctx,
            var, delta, prev, prev + delta);
  } else {
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "debiting  %s->%s by %" PRIu32 ": %" PRId64 " -> %" PRId64, ctx,
            var, delta, prev, prev - delta);
  }
}
