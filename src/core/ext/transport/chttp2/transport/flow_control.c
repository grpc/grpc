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

#ifndef NDEBUG
static void grpc_chttp2_flowctl_trace(const char *file, int line, bool credit,
                                      const char *ctx, const char *var,
                                      int64_t prev, uint32_t delta,
                                      const char *reason, bool is_client);
#define FLOW_CONTROL_CREDIT(ctx, var, delta)                                 \
  if (GRPC_TRACER_ON(grpc_flowctl_trace)) {                                  \
    grpc_chttp2_flowctl_trace(file, line, true, #ctx, #var, ctx->var, delta, \
                              reason, ctx->is_client);                       \
  }                                                                          \
  ctx->var += delta;

#define FLOW_CONTROL_DEBIT(ctx, var, delta)                                   \
  if (GRPC_TRACER_ON(grpc_flowctl_trace)) {                                   \
    grpc_chttp2_flowctl_trace(file, line, false, #ctx, #var, ctx->var, delta, \
                              reason, ctx->is_client);                        \
  }                                                                           \
  ctx->var -= delta;
#else
#define FLOW_CONTROL_CREDIT(ctx, var, delta) ctx->var += delta;
#define FLOW_CONTROL_DEBIT(ctx, var, delta) ctx->var -= delta;
#endif

/*******************************************************************************
 * INTERFACE
 */

/* if there is any disparity between local and announced, send an update */
// TODO(ncteisen): tune this. No need to send an update if there is plenty
// of room
uint32_t grpc_chttp2_flow_control_get_stream_announce(
    grpc_chttp2_stream_flow_control_data *sfc) {
  if (sfc->announced_local_window_delta < sfc->local_window_delta) {
    return (uint32_t)(sfc->local_window_delta -
                      sfc->announced_local_window_delta);

  } else {
    return 0;
  }
}

// TODO(ncteisen): tune this
uint32_t grpc_chttp2_flow_control_get_transport_announce(
    grpc_chttp2_transport_flow_control_data *tfc, int64_t initial_window) {
  return (uint32_t)GPR_MIN(
      (int64_t)((1u << 31) - 1),
      tfc->local_stream_total_over_incoming_window + initial_window);
}

void grpc_chttp2_flow_control_transport_init(
    grpc_chttp2_transport_flow_control_data *tfc) {
  tfc->remote_window = DEFAULT_WINDOW;
  tfc->local_window = DEFAULT_WINDOW;
  tfc->announced_local_window = DEFAULT_WINDOW;
}

void grpc_chttp2_flow_control_stream_init(
    grpc_chttp2_stream_flow_control_data *sfc) {
  GPR_ASSERT(sfc->remote_window_delta == 0);
  GPR_ASSERT(sfc->local_window_delta == 0);
  GPR_ASSERT(sfc->announced_local_window_delta == 0);
}

void grpc_chttp2_flow_control_credit_local_transport(
    grpc_chttp2_transport_flow_control_data *tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_CREDIT(tfc, local_window, val);
}

void grpc_chttp2_flow_control_debit_local_transport(
    grpc_chttp2_transport_flow_control_data *tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_DEBIT(tfc, local_window, val);
}

static void local_stream_preupdate(grpc_chttp2_stream_flow_control_data *sfc) {
  if (sfc->local_window_delta < 0) {
    sfc->tfc->local_stream_total_under_incoming_window +=
        sfc->local_window_delta;
  } else if (sfc->local_window_delta > 0) {
    sfc->tfc->local_stream_total_over_incoming_window -=
        sfc->local_window_delta;
  }
}

static void local_stream_postupdate(grpc_chttp2_stream_flow_control_data *sfc) {
  if (sfc->local_window_delta < 0) {
    sfc->tfc->local_stream_total_under_incoming_window -=
        sfc->local_window_delta;
  } else if (sfc->local_window_delta > 0) {
    sfc->tfc->local_stream_total_over_incoming_window +=
        sfc->local_window_delta;
  }
}

void grpc_chttp2_flow_control_credit_local_stream(
    grpc_chttp2_stream_flow_control_data *sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  local_stream_preupdate(sfc);
  FLOW_CONTROL_CREDIT(sfc, local_window_delta, val);
  local_stream_postupdate(sfc);
}

void grpc_chttp2_flow_control_debit_local_stream(
    grpc_chttp2_stream_flow_control_data *sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  local_stream_preupdate(sfc);
  FLOW_CONTROL_DEBIT(sfc, local_window_delta, val);
  local_stream_postupdate(sfc);
}

void grpc_chttp2_flow_control_announce_credit_transport(
    grpc_chttp2_transport_flow_control_data *tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_CREDIT(tfc, announced_local_window, val);
}

void grpc_chttp2_flow_control_announce_debit_transport(
    grpc_chttp2_transport_flow_control_data *tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_DEBIT(tfc, announced_local_window, val);
}

void grpc_chttp2_flow_control_announce_credit_stream(
    grpc_chttp2_stream_flow_control_data *sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_CREDIT(sfc, announced_local_window_delta, val);
}

void grpc_chttp2_flow_control_announce_debit_stream(
    grpc_chttp2_stream_flow_control_data *sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_DEBIT(sfc, announced_local_window_delta, val);
}

void grpc_chttp2_flow_control_credit_remote_transport(
    grpc_chttp2_transport_flow_control_data *tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_CREDIT(tfc, remote_window, val);
}

void grpc_chttp2_flow_control_debit_remote_transport(
    grpc_chttp2_transport_flow_control_data *tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_DEBIT(tfc, remote_window, val);
}

void grpc_chttp2_flow_control_credit_remote_stream(
    grpc_chttp2_stream_flow_control_data *sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_CREDIT(sfc, remote_window_delta, val);
}

void grpc_chttp2_flow_control_debit_remote_stream(
    grpc_chttp2_stream_flow_control_data *sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS) {
  FLOW_CONTROL_DEBIT(sfc, remote_window_delta, val);
}

#ifndef NDEBUG
void grpc_chttp2_flowctl_trace(const char *file, int line, bool credit,
                               const char *ctx, const char *var, int64_t prev,
                               uint32_t delta, const char *reason,
                               bool is_client) {
  if (credit) {
    gpr_log(
        file, line, GPR_LOG_SEVERITY_DEBUG,
        "%s crediting %s->%s by %" PRIu32 ": %" PRId64 " -> %" PRId64 " | %s",
        is_client ? "cli" : "svr", ctx, var, delta, prev, prev + delta, reason);
  } else {
    gpr_log(
        file, line, GPR_LOG_SEVERITY_DEBUG,
        "%s debiting  %s->%s by %" PRIu32 ": %" PRId64 " -> %" PRId64 " | %s",
        is_client ? "cli" : "svr", ctx, var, delta, prev, prev - delta, reason);
  }
}
#endif
