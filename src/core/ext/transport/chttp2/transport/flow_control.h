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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/pid_controller.h"

#include <stddef.h>

typedef struct {
  /** initial window change. This is tracked as we parse settings frames from
   * the remote peer. If there is a positive delta, then we will make all
   * streams readable since they may have become unstalled */
  int64_t initial_window_update;

  /** Our bookkeeping for the remote peer's available window */
  int64_t remote_window;

  /** Our bookkeeping for our window. Essentially this tracks available buffer
   * space to hold data that peer sends to us. This is our local view of the
   * window. It does not reflect how the remote peer sees it. */
  int64_t local_window;

  /** This is out window according to what we have sent to our remote peer. The
   * difference between this and local_window is what we use to decide when
   * to send WINDOW_UPDATE frames. */
  int64_t announced_local_window;

  /** calculating what we should give for incoming window: we track the total
   * amount of flow control over initial window size across all streams: this is
   * data that we want to receive right now (it has an outstanding read) and the
   * total amount of flow control under initial window size across all streams:
   * this is data we've read early we want to adjust local_window such that:
   * local_window = total_over - max(bdp - total_under, 0) */
  /* DEPRECATE ME */
  int64_t stream_total_over_local_window;
  int64_t stream_total_under_local_window;

  /* bdp estimation */
  grpc_bdp_estimator bdp_estimator;

  /* pid controller */
  grpc_pid_controller pid_controller;
  gpr_timespec last_pid_update;
} grpc_chttp2_transport_flow_control_data;

typedef struct {
  /** window available for us to send to peer, over or under the initial window
   * size of the transport... ie:
   * remote_window = remote_window_delta + transport.initial_window_size */
  int64_t remote_window_delta;

  /** window available for peer to send to us (as a delta on
   * transport.initial_window_size)
   * local_window = local_window_delta + transport.initial_window_size */
  int64_t local_window_delta;

  /** window available for peer to send to us over this stream that we have
   * announced to the peer */
  int64_t announced_local_window_delta;

  /** how much window should we announce? */
  /* DEPRECATE ME */
  uint32_t announce_window;
} grpc_chttp2_stream_flow_control_data;

void grpc_chttp2_flow_control_init(
    grpc_chttp2_transport_flow_control_data* tfc);

void grpc_chttp2_flow_control_credit_local_transport(
    grpc_chttp2_transport_flow_control_data* tfc, int64_t val);
void grpc_chttp2_flow_control_debit_local_transport(
    grpc_chttp2_transport_flow_control_data* tfc, int64_t val);
void grpc_chttp2_flow_control_credit_local_stream(
    grpc_chttp2_stream_flow_control_data* sfc, int64_t val);
void grpc_chttp2_flow_control_debit_local_stream(
    grpc_chttp2_stream_flow_control_data* sfc, int64_t val);
void grpc_chttp2_flow_control_announce_credit_transport(
    grpc_chttp2_transport_flow_control_data* tfc, int64_t val);
void grpc_chttp2_flow_control_announce_debit_transport(
    grpc_chttp2_transport_flow_control_data* tfc, int64_t val);
void grpc_chttp2_flow_control_announce_credit_stream(
    grpc_chttp2_stream_flow_control_data* sfc, int64_t val);
void grpc_chttp2_flow_control_announce_debit_stream(
    grpc_chttp2_stream_flow_control_data* sfc, int64_t val);
void grpc_chttp2_flow_control_credit_remote_transport(
    grpc_chttp2_transport_flow_control_data* tfc, int64_t val);
void grpc_chttp2_flow_control_debit_remote_transport(
    grpc_chttp2_transport_flow_control_data* tfc, int64_t val);
void grpc_chttp2_flow_control_credit_remote_stream(
    grpc_chttp2_stream_flow_control_data* sfc, int64_t val);
void grpc_chttp2_flow_control_debit_remote_stream(
    grpc_chttp2_stream_flow_control_data* sfc, int64_t val);

/*******************************************************************************
 * MACROS and TRACING
 */

extern grpc_tracer_flag grpc_flowctl_trace;

#define GRPC_CHTTP2_IF_TRACING(stmt)      \
  if (!(GRPC_TRACER_ON(grpc_http_trace))) \
    ;                                     \
  else                                    \
  stmt

typedef enum {
  GRPC_CHTTP2_FLOWCTL_MOVE,
  GRPC_CHTTP2_FLOWCTL_CREDIT,
  GRPC_CHTTP2_FLOWCTL_DEBIT
} grpc_chttp2_flowctl_op;

#define GRPC_CHTTP2_FLOW_MOVE_COMMON(phase, transport, id1, id2, dst_context, \
                                     dst_var, src_context, src_var)           \
  do {                                                                        \
    assert(id1 == id2);                                                       \
    if (GRPC_TRACER_ON(grpc_flowctl_trace)) {                                 \
      grpc_chttp2_flowctl_trace(                                              \
          __FILE__, __LINE__, phase, GRPC_CHTTP2_FLOWCTL_MOVE, #dst_context,  \
          #dst_var, #src_context, #src_var, transport->is_client, id1,        \
          dst_context->flow_control.dst_var,                                  \
          src_context->flow_control.src_var);                                 \
    }                                                                         \
    dst_context->flow_control.dst_var += src_context->flow_control.src_var;   \
    src_context->flow_control.src_var = 0;                                    \
  } while (0)

#define GRPC_CHTTP2_FLOW_MOVE_STREAM(phase, transport, dst_context, dst_var, \
                                     src_context, src_var)                   \
  GRPC_CHTTP2_FLOW_MOVE_COMMON(phase, transport, dst_context->id,            \
                               src_context->id, dst_context, dst_var,        \
                               src_context, src_var)
#define GRPC_CHTTP2_FLOW_MOVE_TRANSPORT(phase, dst_context, dst_var,           \
                                        src_context, src_var)                  \
  GRPC_CHTTP2_FLOW_MOVE_COMMON(phase, dst_context, 0, 0, dst_context, dst_var, \
                               src_context, src_var)

#define GRPC_CHTTP2_FLOW_CREDIT_COMMON(phase, transport, id, dst_context,      \
                                       dst_var, amount)                        \
  do {                                                                         \
    if (GRPC_TRACER_ON(grpc_flowctl_trace)) {                                  \
      grpc_chttp2_flowctl_trace(                                               \
          __FILE__, __LINE__, phase, GRPC_CHTTP2_FLOWCTL_CREDIT, #dst_context, \
          #dst_var, NULL, #amount, transport->is_client, id,                   \
          dst_context->flow_control.dst_var, amount);                          \
    }                                                                          \
    dst_context->flow_control.dst_var += amount;                               \
  } while (0)

#define GRPC_CHTTP2_FLOW_CREDIT_STREAM(phase, transport, dst_context, dst_var, \
                                       amount)                                 \
  GRPC_CHTTP2_FLOW_CREDIT_COMMON(phase, transport, dst_context->id,            \
                                 dst_context, dst_var, amount)
#define GRPC_CHTTP2_FLOW_CREDIT_TRANSPORT(phase, dst_context, dst_var, amount) \
  GRPC_CHTTP2_FLOW_CREDIT_COMMON(phase, dst_context, 0, dst_context, dst_var,  \
                                 amount)

#define GRPC_CHTTP2_FLOW_STREAM_LOCAL_WINDOW_DELTA_PREUPDATE(phase, transport, \
                                                             dst_context)      \
  if (dst_context->flow_control.local_window_delta < 0) {                      \
    transport->flow_control.stream_total_under_local_window +=                 \
        dst_context->flow_control.local_window_delta;                          \
  } else if (dst_context->flow_control.local_window_delta > 0) {               \
    transport->flow_control.stream_total_over_local_window -=                  \
        dst_context->flow_control.local_window_delta;                          \
  }

#define GRPC_CHTTP2_FLOW_STREAM_LOCAL_WINDOW_DELTA_POSTUPDATE(   \
    phase, transport, dst_context)                               \
  if (dst_context->flow_control.local_window_delta < 0) {        \
    transport->flow_control.stream_total_under_local_window -=   \
        dst_context->flow_control.local_window_delta;            \
  } else if (dst_context->flow_control.local_window_delta > 0) { \
    transport->flow_control.stream_total_over_local_window +=    \
        dst_context->flow_control.local_window_delta;            \
  }

#define GRPC_CHTTP2_FLOW_DEBIT_STREAM_LOCAL_WINDOW_DELTA(phase, transport,    \
                                                         dst_context, amount) \
  GRPC_CHTTP2_FLOW_STREAM_LOCAL_WINDOW_DELTA_PREUPDATE(phase, transport,      \
                                                       dst_context);          \
  GRPC_CHTTP2_FLOW_DEBIT_STREAM(phase, transport, dst_context,                \
                                local_window_delta, amount);                  \
  GRPC_CHTTP2_FLOW_STREAM_LOCAL_WINDOW_DELTA_POSTUPDATE(phase, transport,     \
                                                        dst_context);

#define GRPC_CHTTP2_FLOW_CREDIT_STREAM_LOCAL_WINDOW_DELTA(phase, transport,    \
                                                          dst_context, amount) \
  GRPC_CHTTP2_FLOW_STREAM_LOCAL_WINDOW_DELTA_PREUPDATE(phase, transport,       \
                                                       dst_context);           \
  GRPC_CHTTP2_FLOW_CREDIT_STREAM(phase, transport, dst_context,                \
                                 local_window_delta, amount);                  \
  GRPC_CHTTP2_FLOW_STREAM_LOCAL_WINDOW_DELTA_POSTUPDATE(phase, transport,      \
                                                        dst_context);

#define GRPC_CHTTP2_FLOW_DEBIT_COMMON(phase, transport, id, dst_context,      \
                                      dst_var, amount)                        \
  do {                                                                        \
    if (GRPC_TRACER_ON(grpc_flowctl_trace)) {                                 \
      grpc_chttp2_flowctl_trace(                                              \
          __FILE__, __LINE__, phase, GRPC_CHTTP2_FLOWCTL_DEBIT, #dst_context, \
          #dst_var, NULL, #amount, transport->is_client, id,                  \
          dst_context->flow_control.dst_var, amount);                         \
    }                                                                         \
    dst_context->flow_control.dst_var -= amount;                              \
  } while (0)

#define GRPC_CHTTP2_FLOW_DEBIT_STREAM(phase, transport, dst_context, dst_var, \
                                      amount)                                 \
  GRPC_CHTTP2_FLOW_DEBIT_COMMON(phase, transport, dst_context->id,            \
                                dst_context, dst_var, amount)
#define GRPC_CHTTP2_FLOW_DEBIT_TRANSPORT(phase, dst_context, dst_var, amount) \
  GRPC_CHTTP2_FLOW_DEBIT_COMMON(phase, dst_context, 0, dst_context, dst_var,  \
                                amount)

void grpc_chttp2_flowctl_trace(const char* file, int line, const char* phase,
                               grpc_chttp2_flowctl_op op, const char* context1,
                               const char* var1, const char* context2,
                               const char* var2, int is_client,
                               uint32_t stream_id, int64_t val1, int64_t val2);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H */
