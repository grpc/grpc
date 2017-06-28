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

extern grpc_tracer_flag grpc_flowctl_trace;

// Though the data structures in this module are not actually opaque, they
// should be treated as such. All manipulation and decision making bits should
// remain in this module.

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

  /** calculating what we should give for incoming window:
      we track the total amount of flow control over initial window size
      across all streams: this is data that we want to receive right now (it
      has an outstanding read)
      and the total amount of flow control under initial window size across all
      streams: this is data we've read early
      we want to adjust incoming_window such that:
      incoming_window = total_over - max(bdp - total_under, 0) */
  int64_t local_stream_total_over_incoming_window;
  int64_t local_stream_total_under_incoming_window;

  /* bdp estimation */
  grpc_bdp_estimator bdp_estimator;

  /* pid controller */
  grpc_pid_controller pid_controller;
  gpr_timespec last_pid_update;

// duplicated data for debugging/tracing ease
#ifndef NDEBUG
  bool is_client;
#endif

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

  grpc_chttp2_transport_flow_control_data* tfc;

// duplicated data for debugging/tracing ease
#ifndef NDEBUG
  bool is_client;
#endif

} grpc_chttp2_stream_flow_control_data;

uint32_t grpc_chttp2_flow_control_get_stream_announce(
    grpc_chttp2_stream_flow_control_data* sfc);
uint32_t grpc_chttp2_flow_control_get_transport_announce(
    grpc_chttp2_transport_flow_control_data* tfc, int64_t initial_window);

// All of the ugliness below is in the interest of having a consistent,
// malleable tracing system to examine flow control intricacies. For a quick
// overview of the flow control API, read the lowercase function prototypes
// below all these macros.
#ifndef NDEBUG
#define GRPC_FLOW_CONTROL_DEBUG_ARGS \
  , const char *file, int line, const char *reason
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_LOCAL_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_credit_local_transport((fc), (val), __FILE__, \
                                                  __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_LOCAL_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_debit_local_transport((fc), (val), __FILE__, \
                                                 __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_LOCAL_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_credit_local_stream((fc), (val), __FILE__, \
                                               __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_LOCAL_STREAM(fc, val, reason)           \
  grpc_chttp2_flow_control_debit_local_stream((fc), (val), __FILE__, __LINE__, \
                                              (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_CREDIT_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_announce_credit_transport((fc), (val), __FILE__, \
                                                     __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_DEBIT_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_announce_debit_transport((fc), (val), __FILE__, \
                                                    __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_CREDIT_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_announce_credit_stream((fc), (val), __FILE__, \
                                                  __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_DEBIT_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_announce_debit_stream((fc), (val), __FILE__, \
                                                 __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_REMOTE_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_credit_remote_transport((fc), (val), __FILE__, \
                                                   __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_REMOTE_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_debit_remote_transport((fc), (val), __FILE__, \
                                                  __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_REMOTE_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_credit_remote_stream((fc), (val), __FILE__, \
                                                __LINE__, (reason))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_REMOTE_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_debit_remote_stream((fc), (val), __FILE__, \
                                               __LINE__, (reason))
#else
#define GRPC_FLOW_CONTROL_DEBUG_ARGS
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_LOCAL_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_credit_local_transport((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_LOCAL_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_debit_local_transport((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_LOCAL_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_credit_local_stream((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_LOCAL_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_debit_local_stream((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_CREDIT_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_announce_credit_transport((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_DEBIT_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_announce_debit_transport((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_CREDIT_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_announce_credit_stream((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_ANNOUNCE_DEBIT_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_announce_debit_stream((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_REMOTE_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_credit_remote_transport((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_REMOTE_TRANSPORT(fc, val, reason) \
  grpc_chttp2_flow_control_debit_remote_transport((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_CREDIT_REMOTE_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_credit_remote_stream((fc), (val))
#define GRPC_CHTTP2_FLOW_CONTROL_DEBIT_REMOTE_STREAM(fc, val, reason) \
  grpc_chttp2_flow_control_debit_remote_stream((fc), (val))
#endif

void grpc_chttp2_flow_control_transport_init(
    grpc_chttp2_transport_flow_control_data* tfc);
void grpc_chttp2_flow_control_stream_init(
    grpc_chttp2_stream_flow_control_data* sfc);

void grpc_chttp2_flow_control_credit_local_transport(
    grpc_chttp2_transport_flow_control_data* tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_debit_local_transport(
    grpc_chttp2_transport_flow_control_data* tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_credit_local_stream(
    grpc_chttp2_stream_flow_control_data* sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_debit_local_stream(
    grpc_chttp2_stream_flow_control_data* sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_announce_credit_transport(
    grpc_chttp2_transport_flow_control_data* tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_announce_debit_transport(
    grpc_chttp2_transport_flow_control_data* tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_announce_credit_stream(
    grpc_chttp2_stream_flow_control_data* sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_announce_debit_stream(
    grpc_chttp2_stream_flow_control_data* sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_credit_remote_transport(
    grpc_chttp2_transport_flow_control_data* tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_debit_remote_transport(
    grpc_chttp2_transport_flow_control_data* tfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_credit_remote_stream(
    grpc_chttp2_stream_flow_control_data* sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);
void grpc_chttp2_flow_control_debit_remote_stream(
    grpc_chttp2_stream_flow_control_data* sfc,
    uint32_t val GRPC_FLOW_CONTROL_DEBUG_ARGS);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H */
