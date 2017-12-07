/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/stream_compression.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/timeout_encoding.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_impl.h"

#define DEFAULT_CONNECTION_WINDOW_TARGET (1024 * 1024)
#define MAX_WINDOW 0x7fffffffu
#define MAX_WRITE_BUFFER_SIZE (64 * 1024 * 1024)
#define DEFAULT_MAX_HEADER_LIST_SIZE (8 * 1024)

#define DEFAULT_CLIENT_KEEPALIVE_TIME_MS INT_MAX
#define DEFAULT_CLIENT_KEEPALIVE_TIMEOUT_MS 20000 /* 20 seconds */
#define DEFAULT_SERVER_KEEPALIVE_TIME_MS 7200000  /* 2 hours */
#define DEFAULT_SERVER_KEEPALIVE_TIMEOUT_MS 20000 /* 20 seconds */
#define DEFAULT_KEEPALIVE_PERMIT_WITHOUT_CALLS false
#define KEEPALIVE_TIME_BACKOFF_MULTIPLIER 2

#define DEFAULT_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS 300000 /* 5 minutes */
#define DEFAULT_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS 300000 /* 5 minutes */
#define DEFAULT_MAX_PINGS_BETWEEN_DATA 0                      /* unlimited */
#define DEFAULT_MAX_PING_STRIKES 2

static int g_default_client_keepalive_time_ms =
    DEFAULT_CLIENT_KEEPALIVE_TIME_MS;
static int g_default_client_keepalive_timeout_ms =
    DEFAULT_CLIENT_KEEPALIVE_TIMEOUT_MS;
static int g_default_server_keepalive_time_ms =
    DEFAULT_SERVER_KEEPALIVE_TIME_MS;
static int g_default_server_keepalive_timeout_ms =
    DEFAULT_SERVER_KEEPALIVE_TIMEOUT_MS;
static bool g_default_keepalive_permit_without_calls =
    DEFAULT_KEEPALIVE_PERMIT_WITHOUT_CALLS;

static int g_default_min_sent_ping_interval_without_data_ms =
    DEFAULT_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS;
static int g_default_min_recv_ping_interval_without_data_ms =
    DEFAULT_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS;
static int g_default_max_pings_without_data = DEFAULT_MAX_PINGS_BETWEEN_DATA;
static int g_default_max_ping_strikes = DEFAULT_MAX_PING_STRIKES;

#define MAX_CLIENT_STREAM_ID 0x7fffffffu
grpc_core::TraceFlag grpc_http_trace(false, "http");
grpc_core::DebugOnlyTraceFlag grpc_trace_chttp2_refcount(false,
                                                         "chttp2_refcount");

/* forward declarations of various callbacks that we'll build closures around */
static void write_action_begin_locked(grpc_exec_ctx* exec_ctx, void* t,
                                      grpc_error* error);
static void write_action(grpc_exec_ctx* exec_ctx, void* t, grpc_error* error);
static void write_action_end_locked(grpc_exec_ctx* exec_ctx, void* t,
                                    grpc_error* error);

static void read_action_locked(grpc_exec_ctx* exec_ctx, void* t,
                               grpc_error* error);

static void complete_fetch_locked(grpc_exec_ctx* exec_ctx, void* gs,
                                  grpc_error* error);
/** Set a transport level setting, and push it to our peer */
static void queue_setting_update(grpc_exec_ctx* exec_ctx,
                                 grpc_chttp2_transport* t,
                                 grpc_chttp2_setting_id id, uint32_t value);

static void close_from_api(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                           grpc_chttp2_stream* s, grpc_error* error);

/** Start new streams that have been created if we can */
static void maybe_start_some_streams(grpc_exec_ctx* exec_ctx,
                                     grpc_chttp2_transport* t);

static void connectivity_state_set(grpc_exec_ctx* exec_ctx,
                                   grpc_chttp2_transport* t,
                                   grpc_connectivity_state state,
                                   grpc_error* error, const char* reason);

static void incoming_byte_stream_destroy_locked(grpc_exec_ctx* exec_ctx,
                                                void* byte_stream,
                                                grpc_error* error_ignored);
static void incoming_byte_stream_publish_error(
    grpc_exec_ctx* exec_ctx, grpc_chttp2_incoming_byte_stream* bs,
    grpc_error* error);
static void incoming_byte_stream_unref(grpc_exec_ctx* exec_ctx,
                                       grpc_chttp2_incoming_byte_stream* bs);

static void benign_reclaimer_locked(grpc_exec_ctx* exec_ctx, void* t,
                                    grpc_error* error);
static void destructive_reclaimer_locked(grpc_exec_ctx* exec_ctx, void* t,
                                         grpc_error* error);

static void post_benign_reclaimer(grpc_exec_ctx* exec_ctx,
                                  grpc_chttp2_transport* t);
static void post_destructive_reclaimer(grpc_exec_ctx* exec_ctx,
                                       grpc_chttp2_transport* t);

static void close_transport_locked(grpc_exec_ctx* exec_ctx,
                                   grpc_chttp2_transport* t, grpc_error* error);
static void end_all_the_calls(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                              grpc_error* error);

static void schedule_bdp_ping_locked(grpc_exec_ctx* exec_ctx,
                                     grpc_chttp2_transport* t);
static void start_bdp_ping_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                  grpc_error* error);
static void finish_bdp_ping_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                   grpc_error* error);
static void next_bdp_ping_timer_expired_locked(grpc_exec_ctx* exec_ctx,
                                               void* tp, grpc_error* error);

static void cancel_pings(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                         grpc_error* error);
static void send_ping_locked(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                             grpc_closure* on_initiate,
                             grpc_closure* on_complete);
static void retry_initiate_ping_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                       grpc_error* error);

/** keepalive-relevant functions */
static void init_keepalive_ping_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                       grpc_error* error);
static void start_keepalive_ping_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                        grpc_error* error);
static void finish_keepalive_ping_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                         grpc_error* error);
static void keepalive_watchdog_fired_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                            grpc_error* error);

static void reset_byte_stream(grpc_exec_ctx* exec_ctx, void* arg,
                              grpc_error* error);

/*******************************************************************************
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

static void destruct_transport(grpc_exec_ctx* exec_ctx,
                               grpc_chttp2_transport* t) {
  size_t i;

  grpc_endpoint_destroy(exec_ctx, t->ep);

  grpc_slice_buffer_destroy_internal(exec_ctx, &t->qbuf);

  grpc_slice_buffer_destroy_internal(exec_ctx, &t->outbuf);
  grpc_chttp2_hpack_compressor_destroy(exec_ctx, &t->hpack_compressor);

  grpc_slice_buffer_destroy_internal(exec_ctx, &t->read_buffer);
  grpc_chttp2_hpack_parser_destroy(exec_ctx, &t->hpack_parser);
  grpc_chttp2_goaway_parser_destroy(&t->goaway_parser);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(t->lists[i].head == nullptr);
    GPR_ASSERT(t->lists[i].tail == nullptr);
  }

  GRPC_ERROR_UNREF(t->goaway_error);

  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&t->stream_map);
  grpc_connectivity_state_destroy(exec_ctx, &t->channel_callback.state_tracker);

  GRPC_COMBINER_UNREF(exec_ctx, t->combiner, "chttp2_transport");

  cancel_pings(exec_ctx, t,
               GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed"));

  while (t->write_cb_pool) {
    grpc_chttp2_write_cb* next = t->write_cb_pool->next;
    gpr_free(t->write_cb_pool);
    t->write_cb_pool = next;
  }

  t->flow_control.Destroy();

  GRPC_ERROR_UNREF(t->closed_with_error);
  gpr_free(t->ping_acks);
  gpr_free(t->peer_string);
  gpr_free(t);
}

#ifndef NDEBUG
void grpc_chttp2_unref_transport(grpc_exec_ctx* exec_ctx,
                                 grpc_chttp2_transport* t, const char* reason,
                                 const char* file, int line) {
  if (grpc_trace_chttp2_refcount.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&t->refs.count);
    gpr_log(GPR_DEBUG, "chttp2:unref:%p %" PRIdPTR "->%" PRIdPTR " %s [%s:%d]",
            t, val, val - 1, reason, file, line);
  }
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(exec_ctx, t);
}

void grpc_chttp2_ref_transport(grpc_chttp2_transport* t, const char* reason,
                               const char* file, int line) {
  if (grpc_trace_chttp2_refcount.enabled()) {
    gpr_atm val = gpr_atm_no_barrier_load(&t->refs.count);
    gpr_log(GPR_DEBUG, "chttp2:  ref:%p %" PRIdPTR "->%" PRIdPTR " %s [%s:%d]",
            t, val, val + 1, reason, file, line);
  }
  gpr_ref(&t->refs);
}
#else
void grpc_chttp2_unref_transport(grpc_exec_ctx* exec_ctx,
                                 grpc_chttp2_transport* t) {
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(exec_ctx, t);
}

void grpc_chttp2_ref_transport(grpc_chttp2_transport* t) { gpr_ref(&t->refs); }
#endif

static const grpc_transport_vtable* get_vtable(void);

static void init_transport(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                           const grpc_channel_args* channel_args,
                           grpc_endpoint* ep, bool is_client) {
  size_t i;
  int j;

  GPR_ASSERT(strlen(GRPC_CHTTP2_CLIENT_CONNECT_STRING) ==
             GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);

  t->base.vtable = get_vtable();
  t->ep = ep;
  /* one ref is for destroy */
  gpr_ref_init(&t->refs, 1);
  t->combiner = grpc_combiner_create();
  t->peer_string = grpc_endpoint_get_peer(ep);
  t->endpoint_reading = 1;
  t->next_stream_id = is_client ? 1 : 2;
  t->is_client = is_client;
  t->deframe_state = is_client ? GRPC_DTS_FH_0 : GRPC_DTS_CLIENT_PREFIX_0;
  t->is_first_frame = true;
  grpc_connectivity_state_init(
      &t->channel_callback.state_tracker, GRPC_CHANNEL_READY,
      is_client ? "client_transport" : "server_transport");

  grpc_slice_buffer_init(&t->qbuf);

  grpc_slice_buffer_init(&t->outbuf);
  grpc_chttp2_hpack_compressor_init(&t->hpack_compressor);

  GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->benign_reclaimer_locked, benign_reclaimer_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->destructive_reclaimer_locked,
                    destructive_reclaimer_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->retry_initiate_ping_locked, retry_initiate_ping_locked,
                    t, grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->start_bdp_ping_locked, start_bdp_ping_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->finish_bdp_ping_locked, finish_bdp_ping_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->next_bdp_ping_timer_expired_locked,
                    next_bdp_ping_timer_expired_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->init_keepalive_ping_locked, init_keepalive_ping_locked,
                    t, grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked,
                    start_keepalive_ping_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                    finish_keepalive_ping_locked, t,
                    grpc_combiner_scheduler(t->combiner));
  GRPC_CLOSURE_INIT(&t->keepalive_watchdog_fired_locked,
                    keepalive_watchdog_fired_locked, t,
                    grpc_combiner_scheduler(t->combiner));

  t->goaway_error = GRPC_ERROR_NONE;
  grpc_chttp2_goaway_parser_init(&t->goaway_parser);
  grpc_chttp2_hpack_parser_init(exec_ctx, &t->hpack_parser);

  grpc_slice_buffer_init(&t->read_buffer);

  /* 8 is a random stab in the dark as to a good initial size: it's small enough
     that it shouldn't waste memory for infrequently used connections, yet
     large enough that the exponential growth should happen nicely when it's
     needed.
     TODO(ctiller): tune this */
  grpc_chttp2_stream_map_init(&t->stream_map, 8);

  /* copy in initial settings to all setting sets */
  for (i = 0; i < GRPC_CHTTP2_NUM_SETTINGS; i++) {
    for (j = 0; j < GRPC_NUM_SETTING_SETS; j++) {
      t->settings[j][i] = grpc_chttp2_settings_parameters[i].default_value;
    }
  }
  t->dirtied_local_settings = 1;
  /* Hack: it's common for implementations to assume 65536 bytes initial send
     window -- this should by rights be 0 */
  t->force_send_settings = 1 << GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
  t->sent_local_settings = 0;
  t->write_buffer_size = grpc_core::chttp2::kDefaultWindow;

  if (is_client) {
    grpc_slice_buffer_add(&t->outbuf, grpc_slice_from_copied_string(
                                          GRPC_CHTTP2_CLIENT_CONNECT_STRING));
  }

  /* configure http2 the way we like it */
  if (is_client) {
    queue_setting_update(exec_ctx, t, GRPC_CHTTP2_SETTINGS_ENABLE_PUSH, 0);
    queue_setting_update(exec_ctx, t,
                         GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 0);
  }
  queue_setting_update(exec_ctx, t, GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
                       DEFAULT_MAX_HEADER_LIST_SIZE);
  queue_setting_update(exec_ctx, t,
                       GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA, 1);

  t->ping_policy.max_pings_without_data = g_default_max_pings_without_data;
  t->ping_policy.min_sent_ping_interval_without_data =
      g_default_min_sent_ping_interval_without_data_ms;
  t->ping_policy.max_ping_strikes = g_default_max_ping_strikes;
  t->ping_policy.min_recv_ping_interval_without_data =
      g_default_min_recv_ping_interval_without_data_ms;

  /* Keepalive setting */
  if (t->is_client) {
    t->keepalive_time = g_default_client_keepalive_time_ms == INT_MAX
                            ? GRPC_MILLIS_INF_FUTURE
                            : g_default_client_keepalive_time_ms;
    t->keepalive_timeout = g_default_client_keepalive_timeout_ms == INT_MAX
                               ? GRPC_MILLIS_INF_FUTURE
                               : g_default_client_keepalive_timeout_ms;
  } else {
    t->keepalive_time = g_default_server_keepalive_time_ms == INT_MAX
                            ? GRPC_MILLIS_INF_FUTURE
                            : g_default_server_keepalive_time_ms;
    t->keepalive_timeout = g_default_server_keepalive_timeout_ms == INT_MAX
                               ? GRPC_MILLIS_INF_FUTURE
                               : g_default_server_keepalive_timeout_ms;
  }
  t->keepalive_permit_without_calls = g_default_keepalive_permit_without_calls;

  t->opt_target = GRPC_CHTTP2_OPTIMIZE_FOR_LATENCY;

  bool enable_bdp = true;

  if (channel_args) {
    for (i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key,
                      GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER)) {
        const grpc_integer_options options = {-1, 0, INT_MAX};
        const int value =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
        if (value >= 0) {
          if ((t->next_stream_id & 1) != (value & 1)) {
            gpr_log(GPR_ERROR, "%s: low bit must be %d on %s",
                    GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER,
                    t->next_stream_id & 1, is_client ? "client" : "server");
          } else {
            t->next_stream_id = (uint32_t)value;
          }
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER)) {
        const grpc_integer_options options = {-1, 0, INT_MAX};
        const int value =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
        if (value >= 0) {
          grpc_chttp2_hpack_compressor_set_max_usable_size(&t->hpack_compressor,
                                                           (uint32_t)value);
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA)) {
        t->ping_policy.max_pings_without_data = grpc_channel_arg_get_integer(
            &channel_args->args[i],
            {g_default_max_pings_without_data, 0, INT_MAX});
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_MAX_PING_STRIKES)) {
        t->ping_policy.max_ping_strikes = grpc_channel_arg_get_integer(
            &channel_args->args[i], {g_default_max_ping_strikes, 0, INT_MAX});
      } else if (0 ==
                 strcmp(
                     channel_args->args[i].key,
                     GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS)) {
        t->ping_policy.min_sent_ping_interval_without_data =
            grpc_channel_arg_get_integer(
                &channel_args->args[i],
                grpc_integer_options{
                    g_default_min_sent_ping_interval_without_data_ms, 0,
                    INT_MAX});
      } else if (0 ==
                 strcmp(
                     channel_args->args[i].key,
                     GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)) {
        t->ping_policy.min_recv_ping_interval_without_data =
            grpc_channel_arg_get_integer(
                &channel_args->args[i],
                grpc_integer_options{
                    g_default_min_recv_ping_interval_without_data_ms, 0,
                    INT_MAX});
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE)) {
        t->write_buffer_size = (uint32_t)grpc_channel_arg_get_integer(
            &channel_args->args[i], {0, 0, MAX_WRITE_BUFFER_SIZE});
      } else if (0 ==
                 strcmp(channel_args->args[i].key, GRPC_ARG_HTTP2_BDP_PROBE)) {
        enable_bdp = grpc_channel_arg_get_bool(&channel_args->args[i], true);
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_KEEPALIVE_TIME_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &channel_args->args[i],
            grpc_integer_options{t->is_client
                                     ? g_default_client_keepalive_time_ms
                                     : g_default_server_keepalive_time_ms,
                                 1, INT_MAX});
        t->keepalive_time = value == INT_MAX ? GRPC_MILLIS_INF_FUTURE : value;
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_KEEPALIVE_TIMEOUT_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &channel_args->args[i],
            grpc_integer_options{t->is_client
                                     ? g_default_client_keepalive_timeout_ms
                                     : g_default_server_keepalive_timeout_ms,
                                 0, INT_MAX});
        t->keepalive_timeout =
            value == INT_MAX ? GRPC_MILLIS_INF_FUTURE : value;
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)) {
        t->keepalive_permit_without_calls =
            (uint32_t)grpc_channel_arg_get_integer(&channel_args->args[i],
                                                   {0, 0, 1});
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_OPTIMIZATION_TARGET)) {
        if (channel_args->args[i].type != GRPC_ARG_STRING) {
          gpr_log(GPR_ERROR, "%s should be a string",
                  GRPC_ARG_OPTIMIZATION_TARGET);
        } else if (0 == strcmp(channel_args->args[i].value.string, "blend")) {
          t->opt_target = GRPC_CHTTP2_OPTIMIZE_FOR_LATENCY;
        } else if (0 == strcmp(channel_args->args[i].value.string, "latency")) {
          t->opt_target = GRPC_CHTTP2_OPTIMIZE_FOR_LATENCY;
        } else if (0 ==
                   strcmp(channel_args->args[i].value.string, "throughput")) {
          t->opt_target = GRPC_CHTTP2_OPTIMIZE_FOR_THROUGHPUT;
        } else {
          gpr_log(GPR_ERROR, "%s value '%s' unknown, assuming 'blend'",
                  GRPC_ARG_OPTIMIZATION_TARGET,
                  channel_args->args[i].value.string);
        }
      } else {
        static const struct {
          const char* channel_arg_name;
          grpc_chttp2_setting_id setting_id;
          grpc_integer_options integer_options;
          bool availability[2] /* server, client */;
        } settings_map[] = {
            {GRPC_ARG_MAX_CONCURRENT_STREAMS,
             GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
             {-1, 0, INT32_MAX},
             {true, false}},
            {GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER,
             GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE,
             {-1, 0, INT32_MAX},
             {true, true}},
            {GRPC_ARG_MAX_METADATA_SIZE,
             GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
             {-1, 0, INT32_MAX},
             {true, true}},
            {GRPC_ARG_HTTP2_MAX_FRAME_SIZE,
             GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
             {-1, 16384, 16777215},
             {true, true}},
            {GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY,
             GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA,
             {1, 0, 1},
             {true, true}},
            {GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES,
             GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
             {-1, 5, INT32_MAX},
             {true, true}}};
        for (j = 0; j < (int)GPR_ARRAY_SIZE(settings_map); j++) {
          if (0 == strcmp(channel_args->args[i].key,
                          settings_map[j].channel_arg_name)) {
            if (!settings_map[j].availability[is_client]) {
              gpr_log(GPR_DEBUG, "%s is not available on %s",
                      settings_map[j].channel_arg_name,
                      is_client ? "clients" : "servers");
            } else {
              int value = grpc_channel_arg_get_integer(
                  &channel_args->args[i], settings_map[j].integer_options);
              if (value >= 0) {
                queue_setting_update(exec_ctx, t, settings_map[j].setting_id,
                                     (uint32_t)value);
              }
            }
            break;
          }
        }
      }
    }
  }

  t->flow_control.Init(exec_ctx, t, enable_bdp);

  /* No pings allowed before receiving a header or data frame. */
  t->ping_state.pings_before_data_required = 0;
  t->ping_state.is_delayed_ping_timer_set = false;
  t->ping_state.last_ping_sent_time = GRPC_MILLIS_INF_PAST;

  t->ping_recv_state.last_ping_recv_time = GRPC_MILLIS_INF_PAST;
  t->ping_recv_state.ping_strikes = 0;

  /* Start keepalive pings */
  if (t->keepalive_time != GRPC_MILLIS_INF_FUTURE) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
    GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
    grpc_timer_init(exec_ctx, &t->keepalive_ping_timer,
                    grpc_exec_ctx_now(exec_ctx) + t->keepalive_time,
                    &t->init_keepalive_ping_locked);
  } else {
    /* Use GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED to indicate there are no
       inflight keeaplive timers */
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED;
  }

  if (enable_bdp) {
    GRPC_CHTTP2_REF_TRANSPORT(t, "bdp_ping");
    schedule_bdp_ping_locked(exec_ctx, t);

    grpc_chttp2_act_on_flowctl_action(
        exec_ctx, t->flow_control->PeriodicUpdate(exec_ctx), t, nullptr);
  }

  grpc_chttp2_initiate_write(exec_ctx, t,
                             GRPC_CHTTP2_INITIATE_WRITE_INITIAL_WRITE);
  post_benign_reclaimer(exec_ctx, t);
}

static void destroy_transport_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                     grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)tp;
  t->destroying = 1;
  close_transport_locked(
      exec_ctx, t,
      grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed"),
          GRPC_ERROR_INT_OCCURRED_DURING_WRITE, t->write_state));
  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "destroy");
}

static void destroy_transport(grpc_exec_ctx* exec_ctx, grpc_transport* gt) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  GRPC_CLOSURE_SCHED(exec_ctx,
                     GRPC_CLOSURE_CREATE(destroy_transport_locked, t,
                                         grpc_combiner_scheduler(t->combiner)),
                     GRPC_ERROR_NONE);
}

static void close_transport_locked(grpc_exec_ctx* exec_ctx,
                                   grpc_chttp2_transport* t,
                                   grpc_error* error) {
  end_all_the_calls(exec_ctx, t, GRPC_ERROR_REF(error));
  cancel_pings(exec_ctx, t, GRPC_ERROR_REF(error));
  if (t->closed_with_error == GRPC_ERROR_NONE) {
    if (!grpc_error_has_clear_grpc_status(error)) {
      error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                                 GRPC_STATUS_UNAVAILABLE);
    }
    if (t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE) {
      if (t->close_transport_on_writes_finished == nullptr) {
        t->close_transport_on_writes_finished =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Delayed close due to in-progress write");
      }
      t->close_transport_on_writes_finished =
          grpc_error_add_child(t->close_transport_on_writes_finished, error);
      return;
    }
    GPR_ASSERT(error != GRPC_ERROR_NONE);
    t->closed_with_error = GRPC_ERROR_REF(error);
    connectivity_state_set(exec_ctx, t, GRPC_CHANNEL_SHUTDOWN,
                           GRPC_ERROR_REF(error), "close_transport");
    if (t->ping_state.is_delayed_ping_timer_set) {
      grpc_timer_cancel(exec_ctx, &t->ping_state.delayed_ping_timer);
    }
    if (t->have_next_bdp_ping_timer) {
      grpc_timer_cancel(exec_ctx, &t->next_bdp_ping_timer);
    }
    switch (t->keepalive_state) {
      case GRPC_CHTTP2_KEEPALIVE_STATE_WAITING:
        grpc_timer_cancel(exec_ctx, &t->keepalive_ping_timer);
        break;
      case GRPC_CHTTP2_KEEPALIVE_STATE_PINGING:
        grpc_timer_cancel(exec_ctx, &t->keepalive_ping_timer);
        grpc_timer_cancel(exec_ctx, &t->keepalive_watchdog_timer);
        break;
      case GRPC_CHTTP2_KEEPALIVE_STATE_DYING:
      case GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED:
        /* keepalive timers are not set in these two states */
        break;
    }

    /* flush writable stream list to avoid dangling references */
    grpc_chttp2_stream* s;
    while (grpc_chttp2_list_pop_writable_stream(t, &s)) {
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2_writing:close");
    }
    GPR_ASSERT(t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE);
    grpc_endpoint_shutdown(exec_ctx, t->ep, GRPC_ERROR_REF(error));
  }
  if (t->notify_on_receive_settings != nullptr) {
    GRPC_CLOSURE_SCHED(exec_ctx, t->notify_on_receive_settings,
                       GRPC_ERROR_CANCELLED);
    t->notify_on_receive_settings = nullptr;
  }
  GRPC_ERROR_UNREF(error);
}

#ifndef NDEBUG
void grpc_chttp2_stream_ref(grpc_chttp2_stream* s, const char* reason) {
  grpc_stream_ref(s->refcount, reason);
}
void grpc_chttp2_stream_unref(grpc_exec_ctx* exec_ctx, grpc_chttp2_stream* s,
                              const char* reason) {
  grpc_stream_unref(exec_ctx, s->refcount, reason);
}
#else
void grpc_chttp2_stream_ref(grpc_chttp2_stream* s) {
  grpc_stream_ref(s->refcount);
}
void grpc_chttp2_stream_unref(grpc_exec_ctx* exec_ctx, grpc_chttp2_stream* s) {
  grpc_stream_unref(exec_ctx, s->refcount);
}
#endif

static int init_stream(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                       grpc_stream* gs, grpc_stream_refcount* refcount,
                       const void* server_data, gpr_arena* arena) {
  GPR_TIMER_BEGIN("init_stream", 0);
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)gs;

  s->t = t;
  s->refcount = refcount;
  /* We reserve one 'active stream' that's dropped when the stream is
     read-closed. The others are for incoming_byte_streams that are actively
     reading */
  GRPC_CHTTP2_STREAM_REF(s, "chttp2");

  grpc_chttp2_incoming_metadata_buffer_init(&s->metadata_buffer[0], arena);
  grpc_chttp2_incoming_metadata_buffer_init(&s->metadata_buffer[1], arena);
  grpc_chttp2_data_parser_init(&s->data_parser);
  grpc_slice_buffer_init(&s->flow_controlled_buffer);
  s->deadline = GRPC_MILLIS_INF_FUTURE;
  GRPC_CLOSURE_INIT(&s->complete_fetch_locked, complete_fetch_locked, s,
                    grpc_schedule_on_exec_ctx);
  grpc_slice_buffer_init(&s->unprocessed_incoming_frames_buffer);
  grpc_slice_buffer_init(&s->frame_storage);
  grpc_slice_buffer_init(&s->compressed_data_buffer);
  grpc_slice_buffer_init(&s->decompressed_data_buffer);
  s->pending_byte_stream = false;
  s->decompressed_header_bytes = 0;
  GRPC_CLOSURE_INIT(&s->reset_byte_stream, reset_byte_stream, s,
                    grpc_combiner_scheduler(t->combiner));

  GRPC_CHTTP2_REF_TRANSPORT(t, "stream");

  if (server_data) {
    s->id = (uint32_t)(uintptr_t)server_data;
    *t->accepting_stream = s;
    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
    post_destructive_reclaimer(exec_ctx, t);
  }

  s->flow_control.Init(t->flow_control.get(), s);
  GPR_TIMER_END("init_stream", 0);

  return 0;
}

static void destroy_stream_locked(grpc_exec_ctx* exec_ctx, void* sp,
                                  grpc_error* error) {
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)sp;
  grpc_chttp2_transport* t = s->t;

  GPR_TIMER_BEGIN("destroy_stream", 0);

  GPR_ASSERT((s->write_closed && s->read_closed) || s->id == 0);
  if (s->id != 0) {
    GPR_ASSERT(grpc_chttp2_stream_map_find(&t->stream_map, s->id) == nullptr);
  }

  grpc_slice_buffer_destroy_internal(exec_ctx,
                                     &s->unprocessed_incoming_frames_buffer);
  grpc_slice_buffer_destroy_internal(exec_ctx, &s->frame_storage);
  grpc_slice_buffer_destroy_internal(exec_ctx, &s->compressed_data_buffer);
  grpc_slice_buffer_destroy_internal(exec_ctx, &s->decompressed_data_buffer);

  grpc_chttp2_list_remove_stalled_by_transport(t, s);
  grpc_chttp2_list_remove_stalled_by_stream(t, s);

  for (int i = 0; i < STREAM_LIST_COUNT; i++) {
    if (s->included[i]) {
      gpr_log(GPR_ERROR, "%s stream %d still included in list %d",
              t->is_client ? "client" : "server", s->id, i);
      abort();
    }
  }

  GPR_ASSERT(s->send_initial_metadata_finished == nullptr);
  GPR_ASSERT(s->fetching_send_message == nullptr);
  GPR_ASSERT(s->send_trailing_metadata_finished == nullptr);
  GPR_ASSERT(s->recv_initial_metadata_ready == nullptr);
  GPR_ASSERT(s->recv_message_ready == nullptr);
  GPR_ASSERT(s->recv_trailing_metadata_finished == nullptr);
  grpc_chttp2_data_parser_destroy(exec_ctx, &s->data_parser);
  grpc_chttp2_incoming_metadata_buffer_destroy(exec_ctx,
                                               &s->metadata_buffer[0]);
  grpc_chttp2_incoming_metadata_buffer_destroy(exec_ctx,
                                               &s->metadata_buffer[1]);
  grpc_slice_buffer_destroy_internal(exec_ctx, &s->flow_controlled_buffer);
  GRPC_ERROR_UNREF(s->read_closed_error);
  GRPC_ERROR_UNREF(s->write_closed_error);
  GRPC_ERROR_UNREF(s->byte_stream_error);

  s->flow_control.Destroy();

  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "stream");

  GPR_TIMER_END("destroy_stream", 0);

  GRPC_CLOSURE_SCHED(exec_ctx, s->destroy_stream_arg, GRPC_ERROR_NONE);
}

static void destroy_stream(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                           grpc_stream* gs,
                           grpc_closure* then_schedule_closure) {
  GPR_TIMER_BEGIN("destroy_stream", 0);
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)gs;

  if (s->stream_compression_ctx != nullptr) {
    grpc_stream_compression_context_destroy(s->stream_compression_ctx);
    s->stream_compression_ctx = nullptr;
  }
  if (s->stream_decompression_ctx != nullptr) {
    grpc_stream_compression_context_destroy(s->stream_decompression_ctx);
    s->stream_decompression_ctx = nullptr;
  }

  s->destroy_stream_arg = then_schedule_closure;
  GRPC_CLOSURE_SCHED(
      exec_ctx,
      GRPC_CLOSURE_INIT(&s->destroy_stream, destroy_stream_locked, s,
                        grpc_combiner_scheduler(t->combiner)),
      GRPC_ERROR_NONE);
  GPR_TIMER_END("destroy_stream", 0);
}

grpc_chttp2_stream* grpc_chttp2_parsing_lookup_stream(grpc_chttp2_transport* t,
                                                      uint32_t id) {
  return (grpc_chttp2_stream*)grpc_chttp2_stream_map_find(&t->stream_map, id);
}

grpc_chttp2_stream* grpc_chttp2_parsing_accept_stream(grpc_exec_ctx* exec_ctx,
                                                      grpc_chttp2_transport* t,
                                                      uint32_t id) {
  if (t->channel_callback.accept_stream == nullptr) {
    return nullptr;
  }
  grpc_chttp2_stream* accepting;
  GPR_ASSERT(t->accepting_stream == nullptr);
  t->accepting_stream = &accepting;
  t->channel_callback.accept_stream(exec_ctx,
                                    t->channel_callback.accept_stream_user_data,
                                    &t->base, (void*)(uintptr_t)id);
  t->accepting_stream = nullptr;
  return accepting;
}

/*******************************************************************************
 * OUTPUT PROCESSING
 */

static const char* write_state_name(grpc_chttp2_write_state st) {
  switch (st) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      return "IDLE";
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      return "WRITING";
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
      return "WRITING+MORE";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

static void set_write_state(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                            grpc_chttp2_write_state st, const char* reason) {
  GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_DEBUG, "W:%p %s state %s -> %s [%s]", t,
                                 t->is_client ? "CLIENT" : "SERVER",
                                 write_state_name(t->write_state),
                                 write_state_name(st), reason));
  t->write_state = st;
  if (st == GRPC_CHTTP2_WRITE_STATE_IDLE) {
    GRPC_CLOSURE_LIST_SCHED(exec_ctx, &t->run_after_write);
    if (t->close_transport_on_writes_finished != nullptr) {
      grpc_error* err = t->close_transport_on_writes_finished;
      t->close_transport_on_writes_finished = nullptr;
      close_transport_locked(exec_ctx, t, err);
    }
  }
}

static void inc_initiate_write_reason(
    grpc_exec_ctx* exec_ctx, grpc_chttp2_initiate_write_reason reason) {
  switch (reason) {
    case GRPC_CHTTP2_INITIATE_WRITE_INITIAL_WRITE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_INITIAL_WRITE(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_START_NEW_STREAM:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_START_NEW_STREAM(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_MESSAGE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_MESSAGE(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_INITIAL_METADATA:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_INITIAL_METADATA(
          exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_TRAILING_METADATA:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_TRAILING_METADATA(
          exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_RETRY_SEND_PING(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_CONTINUE_PINGS:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_CONTINUE_PINGS(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_GOAWAY_SENT(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_RST_STREAM(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_CLOSE_FROM_API:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_CLOSE_FROM_API(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_STREAM_FLOW_CONTROL:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_STREAM_FLOW_CONTROL(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_TRANSPORT_FLOW_CONTROL(
          exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_SETTINGS(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_SETTING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_FLOW_CONTROL_UNSTALLED_BY_SETTING(
          exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_UPDATE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_FLOW_CONTROL_UNSTALLED_BY_UPDATE(
          exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_APPLICATION_PING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_APPLICATION_PING(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_KEEPALIVE_PING(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL_UNSTALLED:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_TRANSPORT_FLOW_CONTROL_UNSTALLED(
          exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_PING_RESPONSE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_PING_RESPONSE(exec_ctx);
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_FORCE_RST_STREAM:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_FORCE_RST_STREAM(exec_ctx);
      break;
  }
}

void grpc_chttp2_initiate_write(grpc_exec_ctx* exec_ctx,
                                grpc_chttp2_transport* t,
                                grpc_chttp2_initiate_write_reason reason) {
  GPR_TIMER_BEGIN("grpc_chttp2_initiate_write", 0);

  switch (t->write_state) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      inc_initiate_write_reason(exec_ctx, reason);
      set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_WRITING,
                      grpc_chttp2_initiate_write_reason_string(reason));
      t->is_first_write_in_batch = true;
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      GRPC_CLOSURE_SCHED(
          exec_ctx,
          GRPC_CLOSURE_INIT(&t->write_action_begin_locked,
                            write_action_begin_locked, t,
                            grpc_combiner_finally_scheduler(t->combiner)),
          GRPC_ERROR_NONE);
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE,
                      grpc_chttp2_initiate_write_reason_string(reason));
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
      break;
  }
  GPR_TIMER_END("grpc_chttp2_initiate_write", 0);
}

void grpc_chttp2_mark_stream_writable(grpc_exec_ctx* exec_ctx,
                                      grpc_chttp2_transport* t,
                                      grpc_chttp2_stream* s) {
  if (t->closed_with_error == GRPC_ERROR_NONE &&
      grpc_chttp2_list_add_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_REF(s, "chttp2_writing:become");
  }
}

static grpc_closure_scheduler* write_scheduler(grpc_chttp2_transport* t,
                                               bool early_results_scheduled,
                                               bool partial_write) {
  /* if it's not the first write in a batch, always offload to the executor:
     we'll probably end up queuing against the kernel anyway, so we'll likely
     get better latency overall if we switch writing work elsewhere and continue
     with application work above */
  if (!t->is_first_write_in_batch) {
    return grpc_executor_scheduler(GRPC_EXECUTOR_SHORT);
  }
  /* equivalently, if it's a partial write, we *know* we're going to be taking a
     thread jump to write it because of the above, may as well do so
     immediately */
  if (partial_write) {
    return grpc_executor_scheduler(GRPC_EXECUTOR_SHORT);
  }
  switch (t->opt_target) {
    case GRPC_CHTTP2_OPTIMIZE_FOR_THROUGHPUT:
      /* executor gives us the largest probability of being able to batch a
       * write with others on this transport */
      return grpc_executor_scheduler(GRPC_EXECUTOR_SHORT);
    case GRPC_CHTTP2_OPTIMIZE_FOR_LATENCY:
      return grpc_schedule_on_exec_ctx;
  }
  GPR_UNREACHABLE_CODE(return nullptr);
}

#define WRITE_STATE_TUPLE_TO_INT(p, i) (2 * (int)(p) + (int)(i))
static const char* begin_writing_desc(bool partial, bool inlined) {
  switch (WRITE_STATE_TUPLE_TO_INT(partial, inlined)) {
    case WRITE_STATE_TUPLE_TO_INT(false, false):
      return "begin write in background";
    case WRITE_STATE_TUPLE_TO_INT(false, true):
      return "begin write in current thread";
    case WRITE_STATE_TUPLE_TO_INT(true, false):
      return "begin partial write in background";
    case WRITE_STATE_TUPLE_TO_INT(true, true):
      return "begin partial write in current thread";
  }
  GPR_UNREACHABLE_CODE(return "bad state tuple");
}

static void write_action_begin_locked(grpc_exec_ctx* exec_ctx, void* gt,
                                      grpc_error* error_ignored) {
  GPR_TIMER_BEGIN("write_action_begin_locked", 0);
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  GPR_ASSERT(t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE);
  grpc_chttp2_begin_write_result r;
  if (t->closed_with_error != GRPC_ERROR_NONE) {
    r.writing = false;
  } else {
    r = grpc_chttp2_begin_write(exec_ctx, t);
  }
  if (r.writing) {
    if (r.partial) {
      GRPC_STATS_INC_HTTP2_PARTIAL_WRITES(exec_ctx);
    }
    if (!t->is_first_write_in_batch) {
      GRPC_STATS_INC_HTTP2_WRITES_CONTINUED(exec_ctx);
    }
    grpc_closure_scheduler* scheduler =
        write_scheduler(t, r.early_results_scheduled, r.partial);
    if (scheduler != grpc_schedule_on_exec_ctx) {
      GRPC_STATS_INC_HTTP2_WRITES_OFFLOADED(exec_ctx);
    }
    set_write_state(
        exec_ctx, t,
        r.partial ? GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE
                  : GRPC_CHTTP2_WRITE_STATE_WRITING,
        begin_writing_desc(r.partial, scheduler == grpc_schedule_on_exec_ctx));
    GRPC_CLOSURE_SCHED(
        exec_ctx,
        GRPC_CLOSURE_INIT(&t->write_action, write_action, t, scheduler),
        GRPC_ERROR_NONE);
  } else {
    GRPC_STATS_INC_HTTP2_SPURIOUS_WRITES_BEGUN(exec_ctx);
    set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_IDLE,
                    "begin writing nothing");
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "writing");
  }
  GPR_TIMER_END("write_action_begin_locked", 0);
}

static void write_action(grpc_exec_ctx* exec_ctx, void* gt, grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  GPR_TIMER_BEGIN("write_action", 0);
  grpc_endpoint_write(
      exec_ctx, t->ep, &t->outbuf,
      GRPC_CLOSURE_INIT(&t->write_action_end_locked, write_action_end_locked, t,
                        grpc_combiner_scheduler(t->combiner)));
  GPR_TIMER_END("write_action", 0);
}

static void write_action_end_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                    grpc_error* error) {
  GPR_TIMER_BEGIN("terminate_writing_with_lock", 0);
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)tp;

  if (error != GRPC_ERROR_NONE) {
    close_transport_locked(exec_ctx, t, GRPC_ERROR_REF(error));
  }

  if (t->sent_goaway_state == GRPC_CHTTP2_GOAWAY_SEND_SCHEDULED) {
    t->sent_goaway_state = GRPC_CHTTP2_GOAWAY_SENT;
    if (grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
      close_transport_locked(
          exec_ctx, t, GRPC_ERROR_CREATE_FROM_STATIC_STRING("goaway sent"));
    }
  }

  switch (t->write_state) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      GPR_UNREACHABLE_CODE(break);
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      GPR_TIMER_MARK("state=writing", 0);
      set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_IDLE,
                      "finish writing");
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
      GPR_TIMER_MARK("state=writing_stale_no_poller", 0);
      set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_WRITING,
                      "continue writing");
      t->is_first_write_in_batch = false;
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      GRPC_CLOSURE_RUN(
          exec_ctx,
          GRPC_CLOSURE_INIT(&t->write_action_begin_locked,
                            write_action_begin_locked, t,
                            grpc_combiner_finally_scheduler(t->combiner)),
          GRPC_ERROR_NONE);
      break;
  }

  grpc_chttp2_end_write(exec_ctx, t, GRPC_ERROR_REF(error));

  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "writing");
  GPR_TIMER_END("terminate_writing_with_lock", 0);
}

// Dirties an HTTP2 setting to be sent out next time a writing path occurs.
// If the change needs to occur immediately, manually initiate a write.
static void queue_setting_update(grpc_exec_ctx* exec_ctx,
                                 grpc_chttp2_transport* t,
                                 grpc_chttp2_setting_id id, uint32_t value) {
  const grpc_chttp2_setting_parameters* sp =
      &grpc_chttp2_settings_parameters[id];
  uint32_t use_value = GPR_CLAMP(value, sp->min_value, sp->max_value);
  if (use_value != value) {
    gpr_log(GPR_INFO, "Requested parameter %s clamped from %d to %d", sp->name,
            value, use_value);
  }
  if (use_value != t->settings[GRPC_LOCAL_SETTINGS][id]) {
    t->settings[GRPC_LOCAL_SETTINGS][id] = use_value;
    t->dirtied_local_settings = 1;
  }
}

void grpc_chttp2_add_incoming_goaway(grpc_exec_ctx* exec_ctx,
                                     grpc_chttp2_transport* t,
                                     uint32_t goaway_error,
                                     grpc_slice goaway_text) {
  // GRPC_CHTTP2_IF_TRACING(
  //     gpr_log(GPR_DEBUG, "got goaway [%d]: %s", goaway_error, msg));

  // Discard the error from a previous goaway frame (if any)
  if (t->goaway_error != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(t->goaway_error);
  }
  t->goaway_error = grpc_error_set_str(
      grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("GOAWAY received"),
          GRPC_ERROR_INT_HTTP2_ERROR, (intptr_t)goaway_error),
      GRPC_ERROR_STR_RAW_BYTES, goaway_text);

  /* When a client receives a GOAWAY with error code ENHANCE_YOUR_CALM and debug
   * data equal to "too_many_pings", it should log the occurrence at a log level
   * that is enabled by default and double the configured KEEPALIVE_TIME used
   * for new connections on that channel. */
  if (t->is_client && goaway_error == GRPC_HTTP2_ENHANCE_YOUR_CALM &&
      grpc_slice_str_cmp(goaway_text, "too_many_pings") == 0) {
    gpr_log(GPR_ERROR,
            "Received a GOAWAY with error code ENHANCE_YOUR_CALM and debug "
            "data equal to \"too_many_pings\"");
    double current_keepalive_time_ms = (double)t->keepalive_time;
    t->keepalive_time =
        current_keepalive_time_ms > INT_MAX / KEEPALIVE_TIME_BACKOFF_MULTIPLIER
            ? GRPC_MILLIS_INF_FUTURE
            : (grpc_millis)(current_keepalive_time_ms *
                            KEEPALIVE_TIME_BACKOFF_MULTIPLIER);
  }

  /* lie: use transient failure from the transport to indicate goaway has been
   * received */
  connectivity_state_set(exec_ctx, t, GRPC_CHANNEL_TRANSIENT_FAILURE,
                         GRPC_ERROR_REF(t->goaway_error), "got_goaway");
}

static void maybe_start_some_streams(grpc_exec_ctx* exec_ctx,
                                     grpc_chttp2_transport* t) {
  grpc_chttp2_stream* s;
  /* start streams where we have free grpc_chttp2_stream ids and free
   * concurrency */
  while (t->next_stream_id <= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_stream_map_size(&t->stream_map) <
             t->settings[GRPC_PEER_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS] &&
         grpc_chttp2_list_pop_waiting_for_concurrency(t, &s)) {
    /* safe since we can't (legally) be parsing this stream yet */
    GRPC_CHTTP2_IF_TRACING(gpr_log(
        GPR_DEBUG, "HTTP:%s: Allocating new grpc_chttp2_stream %p to id %d",
        t->is_client ? "CLI" : "SVR", s, t->next_stream_id));

    GPR_ASSERT(s->id == 0);
    s->id = t->next_stream_id;
    t->next_stream_id += 2;

    if (t->next_stream_id >= MAX_CLIENT_STREAM_ID) {
      connectivity_state_set(
          exec_ctx, t, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Stream IDs exhausted"),
          "no_more_stream_ids");
    }

    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
    post_destructive_reclaimer(exec_ctx, t);
    grpc_chttp2_mark_stream_writable(exec_ctx, t, s);
    grpc_chttp2_initiate_write(exec_ctx, t,
                               GRPC_CHTTP2_INITIATE_WRITE_START_NEW_STREAM);
  }
  /* cancel out streams that will never be started */
  while (t->next_stream_id >= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_list_pop_waiting_for_concurrency(t, &s)) {
    grpc_chttp2_cancel_stream(
        exec_ctx, t, s,
        grpc_error_set_int(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Stream IDs exhausted"),
            GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
  }
}

/* Flag that this closure barrier wants stats to be updated before finishing */
#define CLOSURE_BARRIER_STATS_BIT (1 << 0)
/* Flag that this closure barrier may be covering a write in a pollset, and so
   we should not complete this closure until we can prove that the write got
   scheduled */
#define CLOSURE_BARRIER_MAY_COVER_WRITE (1 << 1)
/* First bit of the reference count, stored in the high order bits (with the low
   bits being used for flags defined above) */
#define CLOSURE_BARRIER_FIRST_REF_BIT (1 << 16)

static grpc_closure* add_closure_barrier(grpc_closure* closure) {
  closure->next_data.scratch += CLOSURE_BARRIER_FIRST_REF_BIT;
  return closure;
}

static void null_then_run_closure(grpc_exec_ctx* exec_ctx,
                                  grpc_closure** closure, grpc_error* error) {
  grpc_closure* c = *closure;
  *closure = nullptr;
  GRPC_CLOSURE_RUN(exec_ctx, c, error);
}

void grpc_chttp2_complete_closure_step(grpc_exec_ctx* exec_ctx,
                                       grpc_chttp2_transport* t,
                                       grpc_chttp2_stream* s,
                                       grpc_closure** pclosure,
                                       grpc_error* error, const char* desc) {
  grpc_closure* closure = *pclosure;
  *pclosure = nullptr;
  if (closure == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  closure->next_data.scratch -= CLOSURE_BARRIER_FIRST_REF_BIT;
  if (grpc_http_trace.enabled()) {
    const char* errstr = grpc_error_string(error);
    gpr_log(
        GPR_DEBUG,
        "complete_closure_step: t=%p %p refs=%d flags=0x%04x desc=%s err=%s "
        "write_state=%s",
        t, closure,
        (int)(closure->next_data.scratch / CLOSURE_BARRIER_FIRST_REF_BIT),
        (int)(closure->next_data.scratch % CLOSURE_BARRIER_FIRST_REF_BIT), desc,
        errstr, write_state_name(t->write_state));
  }
  if (error != GRPC_ERROR_NONE) {
    if (closure->error_data.error == GRPC_ERROR_NONE) {
      closure->error_data.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error in HTTP transport completing operation");
      closure->error_data.error = grpc_error_set_str(
          closure->error_data.error, GRPC_ERROR_STR_TARGET_ADDRESS,
          grpc_slice_from_copied_string(t->peer_string));
    }
    closure->error_data.error =
        grpc_error_add_child(closure->error_data.error, error);
  }
  if (closure->next_data.scratch < CLOSURE_BARRIER_FIRST_REF_BIT) {
    if (closure->next_data.scratch & CLOSURE_BARRIER_STATS_BIT) {
      grpc_transport_move_stats(&s->stats, s->collecting_stats);
      s->collecting_stats = nullptr;
    }
    if ((t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE) ||
        !(closure->next_data.scratch & CLOSURE_BARRIER_MAY_COVER_WRITE)) {
      GRPC_CLOSURE_RUN(exec_ctx, closure, closure->error_data.error);
    } else {
      grpc_closure_list_append(&t->run_after_write, closure,
                               closure->error_data.error);
    }
  }
}

static bool contains_non_ok_status(grpc_metadata_batch* batch) {
  if (batch->idx.named.grpc_status != nullptr) {
    return !grpc_mdelem_eq(batch->idx.named.grpc_status->md,
                           GRPC_MDELEM_GRPC_STATUS_0);
  }
  return false;
}

static void maybe_become_writable_due_to_send_msg(grpc_exec_ctx* exec_ctx,
                                                  grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s) {
  if (s->id != 0 && (!s->write_buffering ||
                     s->flow_controlled_buffer.length > t->write_buffer_size)) {
    grpc_chttp2_mark_stream_writable(exec_ctx, t, s);
    grpc_chttp2_initiate_write(exec_ctx, t,
                               GRPC_CHTTP2_INITIATE_WRITE_SEND_MESSAGE);
  }
}

static void add_fetched_slice_locked(grpc_exec_ctx* exec_ctx,
                                     grpc_chttp2_transport* t,
                                     grpc_chttp2_stream* s) {
  s->fetched_send_message_length +=
      (uint32_t)GRPC_SLICE_LENGTH(s->fetching_slice);
  grpc_slice_buffer_add(&s->flow_controlled_buffer, s->fetching_slice);
  maybe_become_writable_due_to_send_msg(exec_ctx, t, s);
}

static void continue_fetching_send_locked(grpc_exec_ctx* exec_ctx,
                                          grpc_chttp2_transport* t,
                                          grpc_chttp2_stream* s) {
  for (;;) {
    if (s->fetching_send_message == nullptr) {
      /* Stream was cancelled before message fetch completed */
      abort(); /* TODO(ctiller): what cleanup here? */
      return;  /* early out */
    }
    if (s->fetched_send_message_length == s->fetching_send_message->length) {
      grpc_byte_stream_destroy(exec_ctx, s->fetching_send_message);
      int64_t notify_offset = s->next_message_end_offset;
      if (notify_offset <= s->flow_controlled_bytes_written) {
        grpc_chttp2_complete_closure_step(
            exec_ctx, t, s, &s->fetching_send_message_finished, GRPC_ERROR_NONE,
            "fetching_send_message_finished");
      } else {
        grpc_chttp2_write_cb* cb = t->write_cb_pool;
        if (cb == nullptr) {
          cb = (grpc_chttp2_write_cb*)gpr_malloc(sizeof(*cb));
        } else {
          t->write_cb_pool = cb->next;
        }
        cb->call_at_byte = notify_offset;
        cb->closure = s->fetching_send_message_finished;
        s->fetching_send_message_finished = nullptr;
        grpc_chttp2_write_cb** list =
            s->fetching_send_message->flags & GRPC_WRITE_THROUGH
                ? &s->on_write_finished_cbs
                : &s->on_flow_controlled_cbs;
        cb->next = *list;
        *list = cb;
      }
      s->fetching_send_message = nullptr;
      return; /* early out */
    } else if (grpc_byte_stream_next(exec_ctx, s->fetching_send_message,
                                     UINT32_MAX, &s->complete_fetch_locked)) {
      grpc_error* error = grpc_byte_stream_pull(
          exec_ctx, s->fetching_send_message, &s->fetching_slice);
      if (error != GRPC_ERROR_NONE) {
        grpc_byte_stream_destroy(exec_ctx, s->fetching_send_message);
        grpc_chttp2_cancel_stream(exec_ctx, t, s, error);
      } else {
        add_fetched_slice_locked(exec_ctx, t, s);
      }
    }
  }
}

static void complete_fetch_locked(grpc_exec_ctx* exec_ctx, void* gs,
                                  grpc_error* error) {
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)gs;
  grpc_chttp2_transport* t = s->t;
  if (error == GRPC_ERROR_NONE) {
    error = grpc_byte_stream_pull(exec_ctx, s->fetching_send_message,
                                  &s->fetching_slice);
    if (error == GRPC_ERROR_NONE) {
      add_fetched_slice_locked(exec_ctx, t, s);
      continue_fetching_send_locked(exec_ctx, t, s);
    }
  }
  if (error != GRPC_ERROR_NONE) {
    grpc_byte_stream_destroy(exec_ctx, s->fetching_send_message);
    grpc_chttp2_cancel_stream(exec_ctx, t, s, error);
  }
}

static void do_nothing(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {}

static void log_metadata(const grpc_metadata_batch* md_batch, uint32_t id,
                         bool is_client, bool is_initial) {
  for (grpc_linked_mdelem* md = md_batch->list.head; md != nullptr;
       md = md->next) {
    char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
    char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
    gpr_log(GPR_INFO, "HTTP:%d:%s:%s: %s: %s", id, is_initial ? "HDR" : "TRL",
            is_client ? "CLI" : "SVR", key, value);
    gpr_free(key);
    gpr_free(value);
  }
}

static void perform_stream_op_locked(grpc_exec_ctx* exec_ctx, void* stream_op,
                                     grpc_error* error_ignored) {
  GPR_TIMER_BEGIN("perform_stream_op_locked", 0);

  grpc_transport_stream_op_batch* op =
      (grpc_transport_stream_op_batch*)stream_op;
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)op->handler_private.extra_arg;
  grpc_transport_stream_op_batch_payload* op_payload = op->payload;
  grpc_chttp2_transport* t = s->t;

  GRPC_STATS_INC_HTTP2_OP_BATCHES(exec_ctx);

  if (grpc_http_trace.enabled()) {
    char* str = grpc_transport_stream_op_batch_string(op);
    gpr_log(GPR_DEBUG, "perform_stream_op_locked: %s; on_complete = %p", str,
            op->on_complete);
    gpr_free(str);
    if (op->send_initial_metadata) {
      log_metadata(op_payload->send_initial_metadata.send_initial_metadata,
                   s->id, t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op_payload->send_trailing_metadata.send_trailing_metadata,
                   s->id, t->is_client, false);
    }
  }

  grpc_closure* on_complete = op->on_complete;
  if (on_complete == nullptr) {
    on_complete =
        GRPC_CLOSURE_CREATE(do_nothing, nullptr, grpc_schedule_on_exec_ctx);
  }

  /* use final_data as a barrier until enqueue time; the inital counter is
     dropped at the end of this function */
  on_complete->next_data.scratch = CLOSURE_BARRIER_FIRST_REF_BIT;
  on_complete->error_data.error = GRPC_ERROR_NONE;

  if (op->collect_stats) {
    GPR_ASSERT(s->collecting_stats == nullptr);
    s->collecting_stats = op_payload->collect_stats.collect_stats;
    on_complete->next_data.scratch |= CLOSURE_BARRIER_STATS_BIT;
  }

  if (op->cancel_stream) {
    GRPC_STATS_INC_HTTP2_OP_CANCEL(exec_ctx);
    grpc_chttp2_cancel_stream(exec_ctx, t, s,
                              op_payload->cancel_stream.cancel_error);
  }

  if (op->send_initial_metadata) {
    GRPC_STATS_INC_HTTP2_OP_SEND_INITIAL_METADATA(exec_ctx);
    GPR_ASSERT(s->send_initial_metadata_finished == nullptr);
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;

    /* Identify stream compression */
    if (op_payload->send_initial_metadata.send_initial_metadata->idx.named
                .content_encoding == nullptr ||
        grpc_stream_compression_method_parse(
            GRPC_MDVALUE(
                op_payload->send_initial_metadata.send_initial_metadata->idx
                    .named.content_encoding->md),
            true, &s->stream_compression_method) == 0) {
      s->stream_compression_method = GRPC_STREAM_COMPRESSION_IDENTITY_COMPRESS;
    }

    s->send_initial_metadata_finished = add_closure_barrier(on_complete);
    s->send_initial_metadata =
        op_payload->send_initial_metadata.send_initial_metadata;
    const size_t metadata_size =
        grpc_metadata_batch_size(s->send_initial_metadata);
    const size_t metadata_peer_limit =
        t->settings[GRPC_PEER_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (t->is_client) {
      s->deadline = GPR_MIN(s->deadline, s->send_initial_metadata->deadline);
    }
    if (metadata_size > metadata_peer_limit) {
      grpc_chttp2_cancel_stream(
          exec_ctx, t, s,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                         "to-be-sent initial metadata size "
                                         "exceeds peer limit"),
                                     GRPC_ERROR_INT_SIZE,
                                     (intptr_t)metadata_size),
                  GRPC_ERROR_INT_LIMIT, (intptr_t)metadata_peer_limit),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
    } else {
      if (contains_non_ok_status(s->send_initial_metadata)) {
        s->seen_error = true;
      }
      if (!s->write_closed) {
        if (t->is_client) {
          if (t->closed_with_error == GRPC_ERROR_NONE) {
            GPR_ASSERT(s->id == 0);
            grpc_chttp2_list_add_waiting_for_concurrency(t, s);
            maybe_start_some_streams(exec_ctx, t);
          } else {
            grpc_chttp2_cancel_stream(
                exec_ctx, t, s,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                        "Transport closed", &t->closed_with_error, 1),
                    GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
          }
        } else {
          GPR_ASSERT(s->id != 0);
          grpc_chttp2_mark_stream_writable(exec_ctx, t, s);
          if (!(op->send_message &&
                (op->payload->send_message.send_message->flags &
                 GRPC_WRITE_BUFFER_HINT))) {
            grpc_chttp2_initiate_write(
                exec_ctx, t, GRPC_CHTTP2_INITIATE_WRITE_SEND_INITIAL_METADATA);
          }
        }
      } else {
        s->send_initial_metadata = nullptr;
        grpc_chttp2_complete_closure_step(
            exec_ctx, t, s, &s->send_initial_metadata_finished,
            GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                "Attempt to send initial metadata after stream was closed",
                &s->write_closed_error, 1),
            "send_initial_metadata_finished");
      }
    }
    if (op_payload->send_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op_payload->send_initial_metadata.peer_string,
                        (gpr_atm)gpr_strdup(t->peer_string));
    }
  }

  if (op->send_message) {
    GRPC_STATS_INC_HTTP2_OP_SEND_MESSAGE(exec_ctx);
    GRPC_STATS_INC_HTTP2_SEND_MESSAGE_SIZE(
        exec_ctx, op->payload->send_message.send_message->length);
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->fetching_send_message_finished = add_closure_barrier(op->on_complete);
    if (s->write_closed) {
      // Return an error unless the client has already received trailing
      // metadata from the server, since an application using a
      // streaming call might send another message before getting a
      // recv_message failure, breaking out of its loop, and then
      // starting recv_trailing_metadata.
      grpc_chttp2_complete_closure_step(
          exec_ctx, t, s, &s->fetching_send_message_finished,
          t->is_client && s->received_trailing_metadata
              ? GRPC_ERROR_NONE
              : GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                    "Attempt to send message after stream was closed",
                    &s->write_closed_error, 1),
          "fetching_send_message_finished");
    } else {
      GPR_ASSERT(s->fetching_send_message == nullptr);
      uint8_t* frame_hdr = grpc_slice_buffer_tiny_add(
          &s->flow_controlled_buffer, GRPC_HEADER_SIZE_IN_BYTES);
      uint32_t flags = op_payload->send_message.send_message->flags;
      frame_hdr[0] = (flags & GRPC_WRITE_INTERNAL_COMPRESS) != 0;
      size_t len = op_payload->send_message.send_message->length;
      frame_hdr[1] = (uint8_t)(len >> 24);
      frame_hdr[2] = (uint8_t)(len >> 16);
      frame_hdr[3] = (uint8_t)(len >> 8);
      frame_hdr[4] = (uint8_t)(len);
      s->fetching_send_message = op_payload->send_message.send_message;
      s->fetched_send_message_length = 0;
      s->next_message_end_offset = s->flow_controlled_bytes_written +
                                   (int64_t)s->flow_controlled_buffer.length +
                                   (int64_t)len;
      if (flags & GRPC_WRITE_BUFFER_HINT) {
        s->next_message_end_offset -= t->write_buffer_size;
        s->write_buffering = true;
      } else {
        s->write_buffering = false;
      }
      continue_fetching_send_locked(exec_ctx, t, s);
      maybe_become_writable_due_to_send_msg(exec_ctx, t, s);
    }
  }

  if (op->send_trailing_metadata) {
    GRPC_STATS_INC_HTTP2_OP_SEND_TRAILING_METADATA(exec_ctx);
    GPR_ASSERT(s->send_trailing_metadata_finished == nullptr);
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->send_trailing_metadata_finished = add_closure_barrier(on_complete);
    s->send_trailing_metadata =
        op_payload->send_trailing_metadata.send_trailing_metadata;
    s->write_buffering = false;
    const size_t metadata_size =
        grpc_metadata_batch_size(s->send_trailing_metadata);
    const size_t metadata_peer_limit =
        t->settings[GRPC_PEER_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (metadata_size > metadata_peer_limit) {
      grpc_chttp2_cancel_stream(
          exec_ctx, t, s,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                         "to-be-sent trailing metadata size "
                                         "exceeds peer limit"),
                                     GRPC_ERROR_INT_SIZE,
                                     (intptr_t)metadata_size),
                  GRPC_ERROR_INT_LIMIT, (intptr_t)metadata_peer_limit),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
    } else {
      if (contains_non_ok_status(s->send_trailing_metadata)) {
        s->seen_error = true;
      }
      if (s->write_closed) {
        s->send_trailing_metadata = nullptr;
        grpc_chttp2_complete_closure_step(
            exec_ctx, t, s, &s->send_trailing_metadata_finished,
            grpc_metadata_batch_is_empty(
                op->payload->send_trailing_metadata.send_trailing_metadata)
                ? GRPC_ERROR_NONE
                : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                      "Attempt to send trailing metadata after "
                      "stream was closed"),
            "send_trailing_metadata_finished");
      } else if (s->id != 0) {
        /* TODO(ctiller): check if there's flow control for any outstanding
           bytes before going writable */
        grpc_chttp2_mark_stream_writable(exec_ctx, t, s);
        grpc_chttp2_initiate_write(
            exec_ctx, t, GRPC_CHTTP2_INITIATE_WRITE_SEND_TRAILING_METADATA);
      }
    }
  }

  if (op->recv_initial_metadata) {
    GRPC_STATS_INC_HTTP2_OP_RECV_INITIAL_METADATA(exec_ctx);
    GPR_ASSERT(s->recv_initial_metadata_ready == nullptr);
    s->recv_initial_metadata_ready =
        op_payload->recv_initial_metadata.recv_initial_metadata_ready;
    s->recv_initial_metadata =
        op_payload->recv_initial_metadata.recv_initial_metadata;
    s->trailing_metadata_available =
        op_payload->recv_initial_metadata.trailing_metadata_available;
    if (op_payload->recv_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op_payload->recv_initial_metadata.peer_string,
                        (gpr_atm)gpr_strdup(t->peer_string));
    }
    grpc_chttp2_maybe_complete_recv_initial_metadata(exec_ctx, t, s);
  }

  if (op->recv_message) {
    GRPC_STATS_INC_HTTP2_OP_RECV_MESSAGE(exec_ctx);
    size_t already_received;
    GPR_ASSERT(s->recv_message_ready == nullptr);
    GPR_ASSERT(!s->pending_byte_stream);
    s->recv_message_ready = op_payload->recv_message.recv_message_ready;
    s->recv_message = op_payload->recv_message.recv_message;
    if (s->id != 0) {
      if (!s->read_closed) {
        already_received = s->frame_storage.length;
        s->flow_control->IncomingByteStreamUpdate(GRPC_HEADER_SIZE_IN_BYTES,
                                                  already_received);
        grpc_chttp2_act_on_flowctl_action(exec_ctx,
                                          s->flow_control->MakeAction(), t, s);
      }
    }
    grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  }

  if (op->recv_trailing_metadata) {
    GRPC_STATS_INC_HTTP2_OP_RECV_TRAILING_METADATA(exec_ctx);
    GPR_ASSERT(s->recv_trailing_metadata_finished == nullptr);
    s->recv_trailing_metadata_finished = add_closure_barrier(on_complete);
    s->recv_trailing_metadata =
        op_payload->recv_trailing_metadata.recv_trailing_metadata;
    s->final_metadata_requested = true;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }

  grpc_chttp2_complete_closure_step(exec_ctx, t, s, &on_complete,
                                    GRPC_ERROR_NONE, "op->on_complete");

  GPR_TIMER_END("perform_stream_op_locked", 0);
  GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "perform_stream_op");
}

static void perform_stream_op(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                              grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
  GPR_TIMER_BEGIN("perform_stream_op", 0);
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)gs;

  if (!t->is_client) {
    if (op->send_initial_metadata) {
      grpc_millis deadline =
          op->payload->send_initial_metadata.send_initial_metadata->deadline;
      GPR_ASSERT(deadline == GRPC_MILLIS_INF_FUTURE);
    }
    if (op->send_trailing_metadata) {
      grpc_millis deadline =
          op->payload->send_trailing_metadata.send_trailing_metadata->deadline;
      GPR_ASSERT(deadline == GRPC_MILLIS_INF_FUTURE);
    }
  }

  if (grpc_http_trace.enabled()) {
    char* str = grpc_transport_stream_op_batch_string(op);
    gpr_log(GPR_DEBUG, "perform_stream_op[s=%p]: %s", s, str);
    gpr_free(str);
  }

  op->handler_private.extra_arg = gs;
  GRPC_CHTTP2_STREAM_REF(s, "perform_stream_op");
  GRPC_CLOSURE_SCHED(
      exec_ctx,
      GRPC_CLOSURE_INIT(&op->handler_private.closure, perform_stream_op_locked,
                        op, grpc_combiner_scheduler(t->combiner)),
      GRPC_ERROR_NONE);
  GPR_TIMER_END("perform_stream_op", 0);
}

static void cancel_pings(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                         grpc_error* error) {
  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  for (size_t j = 0; j < GRPC_CHTTP2_PCL_COUNT; j++) {
    grpc_closure_list_fail_all(&pq->lists[j], GRPC_ERROR_REF(error));
    GRPC_CLOSURE_LIST_SCHED(exec_ctx, &pq->lists[j]);
  }
  GRPC_ERROR_UNREF(error);
}

static void send_ping_locked(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                             grpc_closure* on_initiate, grpc_closure* on_ack) {
  if (t->closed_with_error != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_SCHED(exec_ctx, on_initiate,
                       GRPC_ERROR_REF(t->closed_with_error));
    GRPC_CLOSURE_SCHED(exec_ctx, on_ack, GRPC_ERROR_REF(t->closed_with_error));
    return;
  }
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_INITIATE], on_initiate,
                           GRPC_ERROR_NONE);
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_NEXT], on_ack,
                           GRPC_ERROR_NONE);
}

static void retry_initiate_ping_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                       grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)tp;
  t->ping_state.is_delayed_ping_timer_set = false;
  if (error == GRPC_ERROR_NONE) {
    grpc_chttp2_initiate_write(exec_ctx, t,
                               GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING);
  }
}

void grpc_chttp2_ack_ping(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                          uint64_t id) {
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (pq->inflight_id != id) {
    char* from = grpc_endpoint_get_peer(t->ep);
    gpr_log(GPR_DEBUG, "Unknown ping response from %s: %" PRIx64, from, id);
    gpr_free(from);
    return;
  }
  GRPC_CLOSURE_LIST_SCHED(exec_ctx, &pq->lists[GRPC_CHTTP2_PCL_INFLIGHT]);
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_NEXT])) {
    grpc_chttp2_initiate_write(exec_ctx, t,
                               GRPC_CHTTP2_INITIATE_WRITE_CONTINUE_PINGS);
  }
}

static void send_goaway(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                        grpc_error* error) {
  t->sent_goaway_state = GRPC_CHTTP2_GOAWAY_SEND_SCHEDULED;
  grpc_http2_error_code http_error;
  grpc_slice slice;
  grpc_error_get_status(exec_ctx, error, GRPC_MILLIS_INF_FUTURE, nullptr,
                        &slice, &http_error, nullptr);
  grpc_chttp2_goaway_append(t->last_new_stream_id, (uint32_t)http_error,
                            grpc_slice_ref_internal(slice), &t->qbuf);
  grpc_chttp2_initiate_write(exec_ctx, t,
                             GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
  GRPC_ERROR_UNREF(error);
}

void grpc_chttp2_add_ping_strike(grpc_exec_ctx* exec_ctx,
                                 grpc_chttp2_transport* t) {
  t->ping_recv_state.ping_strikes++;
  if (++t->ping_recv_state.ping_strikes > t->ping_policy.max_ping_strikes &&
      t->ping_policy.max_ping_strikes != 0) {
    send_goaway(exec_ctx, t,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("too_many_pings"),
                    GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_ENHANCE_YOUR_CALM));
    /*The transport will be closed after the write is done */
    close_transport_locked(
        exec_ctx, t,
        grpc_error_set_int(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Too many pings"),
            GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
  }
}

static void perform_transport_op_locked(grpc_exec_ctx* exec_ctx,
                                        void* stream_op,
                                        grpc_error* error_ignored) {
  grpc_transport_op* op = (grpc_transport_op*)stream_op;
  grpc_chttp2_transport* t =
      (grpc_chttp2_transport*)op->handler_private.extra_arg;

  if (op->goaway_error) {
    send_goaway(exec_ctx, t, op->goaway_error);
  }

  if (op->set_accept_stream) {
    t->channel_callback.accept_stream = op->set_accept_stream_fn;
    t->channel_callback.accept_stream_user_data =
        op->set_accept_stream_user_data;
  }

  if (op->bind_pollset) {
    grpc_endpoint_add_to_pollset(exec_ctx, t->ep, op->bind_pollset);
  }

  if (op->bind_pollset_set) {
    grpc_endpoint_add_to_pollset_set(exec_ctx, t->ep, op->bind_pollset_set);
  }

  if (op->send_ping) {
    send_ping_locked(exec_ctx, t, nullptr, op->send_ping);
    grpc_chttp2_initiate_write(exec_ctx, t,
                               GRPC_CHTTP2_INITIATE_WRITE_APPLICATION_PING);
  }

  if (op->on_connectivity_state_change != nullptr) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &t->channel_callback.state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    close_transport_locked(exec_ctx, t, op->disconnect_with_error);
  }

  GRPC_CLOSURE_RUN(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);

  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "transport_op");
}

static void perform_transport_op(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                                 grpc_transport_op* op) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  char* msg = grpc_transport_op_string(op);
  gpr_free(msg);
  op->handler_private.extra_arg = gt;
  GRPC_CHTTP2_REF_TRANSPORT(t, "transport_op");
  GRPC_CLOSURE_SCHED(exec_ctx,
                     GRPC_CLOSURE_INIT(&op->handler_private.closure,
                                       perform_transport_op_locked, op,
                                       grpc_combiner_scheduler(t->combiner)),
                     GRPC_ERROR_NONE);
}

/*******************************************************************************
 * INPUT PROCESSING - GENERAL
 */

void grpc_chttp2_maybe_complete_recv_initial_metadata(grpc_exec_ctx* exec_ctx,
                                                      grpc_chttp2_transport* t,
                                                      grpc_chttp2_stream* s) {
  if (s->recv_initial_metadata_ready != nullptr &&
      s->published_metadata[0] != GRPC_METADATA_NOT_PUBLISHED) {
    if (s->seen_error) {
      grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &s->frame_storage);
      if (!s->pending_byte_stream) {
        grpc_slice_buffer_reset_and_unref_internal(
            exec_ctx, &s->unprocessed_incoming_frames_buffer);
      }
    }
    grpc_chttp2_incoming_metadata_buffer_publish(
        exec_ctx, &s->metadata_buffer[0], s->recv_initial_metadata);
    null_then_run_closure(exec_ctx, &s->recv_initial_metadata_ready,
                          GRPC_ERROR_NONE);
  }
}

void grpc_chttp2_maybe_complete_recv_message(grpc_exec_ctx* exec_ctx,
                                             grpc_chttp2_transport* t,
                                             grpc_chttp2_stream* s) {
  grpc_error* error = GRPC_ERROR_NONE;
  if (s->recv_message_ready != nullptr) {
    *s->recv_message = nullptr;
    if (s->final_metadata_requested && s->seen_error) {
      grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &s->frame_storage);
      if (!s->pending_byte_stream) {
        grpc_slice_buffer_reset_and_unref_internal(
            exec_ctx, &s->unprocessed_incoming_frames_buffer);
      }
    }
    if (!s->pending_byte_stream) {
      while (s->unprocessed_incoming_frames_buffer.length > 0 ||
             s->frame_storage.length > 0) {
        if (s->unprocessed_incoming_frames_buffer.length == 0) {
          grpc_slice_buffer_swap(&s->unprocessed_incoming_frames_buffer,
                                 &s->frame_storage);
          s->unprocessed_incoming_frames_decompressed = false;
        }
        if (!s->unprocessed_incoming_frames_decompressed &&
            s->stream_decompression_method !=
                GRPC_STREAM_COMPRESSION_IDENTITY_DECOMPRESS) {
          GPR_ASSERT(s->decompressed_data_buffer.length == 0);
          bool end_of_context;
          if (!s->stream_decompression_ctx) {
            s->stream_decompression_ctx =
                grpc_stream_compression_context_create(
                    s->stream_decompression_method);
          }
          if (!grpc_stream_decompress(
                  s->stream_decompression_ctx,
                  &s->unprocessed_incoming_frames_buffer,
                  &s->decompressed_data_buffer, nullptr,
                  GRPC_HEADER_SIZE_IN_BYTES - s->decompressed_header_bytes,
                  &end_of_context)) {
            grpc_slice_buffer_reset_and_unref_internal(exec_ctx,
                                                       &s->frame_storage);
            grpc_slice_buffer_reset_and_unref_internal(
                exec_ctx, &s->unprocessed_incoming_frames_buffer);
            error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Stream decompression error.");
          } else {
            s->decompressed_header_bytes += s->decompressed_data_buffer.length;
            if (s->decompressed_header_bytes == GRPC_HEADER_SIZE_IN_BYTES) {
              s->decompressed_header_bytes = 0;
            }
            error = grpc_deframe_unprocessed_incoming_frames(
                exec_ctx, &s->data_parser, s, &s->decompressed_data_buffer,
                nullptr, s->recv_message);
            if (end_of_context) {
              grpc_stream_compression_context_destroy(
                  s->stream_decompression_ctx);
              s->stream_decompression_ctx = nullptr;
            }
          }
        } else {
          error = grpc_deframe_unprocessed_incoming_frames(
              exec_ctx, &s->data_parser, s,
              &s->unprocessed_incoming_frames_buffer, nullptr, s->recv_message);
        }
        if (error != GRPC_ERROR_NONE) {
          s->seen_error = true;
          grpc_slice_buffer_reset_and_unref_internal(exec_ctx,
                                                     &s->frame_storage);
          grpc_slice_buffer_reset_and_unref_internal(
              exec_ctx, &s->unprocessed_incoming_frames_buffer);
          break;
        } else if (*s->recv_message != nullptr) {
          break;
        }
      }
    }
    if (error == GRPC_ERROR_NONE && *s->recv_message != nullptr) {
      null_then_run_closure(exec_ctx, &s->recv_message_ready, GRPC_ERROR_NONE);
    } else if (s->published_metadata[1] != GRPC_METADATA_NOT_PUBLISHED) {
      *s->recv_message = nullptr;
      null_then_run_closure(exec_ctx, &s->recv_message_ready, GRPC_ERROR_NONE);
    }
    GRPC_ERROR_UNREF(error);
  }
}

void grpc_chttp2_maybe_complete_recv_trailing_metadata(grpc_exec_ctx* exec_ctx,
                                                       grpc_chttp2_transport* t,
                                                       grpc_chttp2_stream* s) {
  grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  if (s->recv_trailing_metadata_finished != nullptr && s->read_closed &&
      s->write_closed) {
    if (s->seen_error) {
      grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &s->frame_storage);
      if (!s->pending_byte_stream) {
        grpc_slice_buffer_reset_and_unref_internal(
            exec_ctx, &s->unprocessed_incoming_frames_buffer);
      }
    }
    bool pending_data = s->pending_byte_stream ||
                        s->unprocessed_incoming_frames_buffer.length > 0;
    if (s->read_closed && s->frame_storage.length > 0 && !pending_data &&
        !s->seen_error && s->recv_trailing_metadata_finished != nullptr) {
      /* Maybe some SYNC_FLUSH data is left in frame_storage. Consume them and
       * maybe decompress the next 5 bytes in the stream. */
      bool end_of_context;
      if (!s->stream_decompression_ctx) {
        s->stream_decompression_ctx = grpc_stream_compression_context_create(
            s->stream_decompression_method);
      }
      if (!grpc_stream_decompress(
              s->stream_decompression_ctx, &s->frame_storage,
              &s->unprocessed_incoming_frames_buffer, nullptr,
              GRPC_HEADER_SIZE_IN_BYTES, &end_of_context)) {
        grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &s->frame_storage);
        grpc_slice_buffer_reset_and_unref_internal(
            exec_ctx, &s->unprocessed_incoming_frames_buffer);
        s->seen_error = true;
      } else {
        if (s->unprocessed_incoming_frames_buffer.length > 0) {
          s->unprocessed_incoming_frames_decompressed = true;
          pending_data = true;
        }
        if (end_of_context) {
          grpc_stream_compression_context_destroy(s->stream_decompression_ctx);
          s->stream_decompression_ctx = nullptr;
        }
      }
    }
    if (s->read_closed && s->frame_storage.length == 0 && !pending_data &&
        s->recv_trailing_metadata_finished != nullptr) {
      grpc_chttp2_incoming_metadata_buffer_publish(
          exec_ctx, &s->metadata_buffer[1], s->recv_trailing_metadata);
      grpc_chttp2_complete_closure_step(
          exec_ctx, t, s, &s->recv_trailing_metadata_finished, GRPC_ERROR_NONE,
          "recv_trailing_metadata_finished");
    }
  }
}

static void remove_stream(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                          uint32_t id, grpc_error* error) {
  grpc_chttp2_stream* s =
      (grpc_chttp2_stream*)grpc_chttp2_stream_map_delete(&t->stream_map, id);
  GPR_ASSERT(s);
  if (t->incoming_stream == s) {
    t->incoming_stream = nullptr;
    grpc_chttp2_parsing_become_skip_parser(exec_ctx, t);
  }
  if (s->pending_byte_stream) {
    if (s->on_next != nullptr) {
      grpc_chttp2_incoming_byte_stream* bs = s->data_parser.parsing_frame;
      if (error == GRPC_ERROR_NONE) {
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
      }
      incoming_byte_stream_publish_error(exec_ctx, bs, error);
      incoming_byte_stream_unref(exec_ctx, bs);
      s->data_parser.parsing_frame = nullptr;
    } else {
      GRPC_ERROR_UNREF(s->byte_stream_error);
      s->byte_stream_error = GRPC_ERROR_REF(error);
    }
  }

  if (grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    post_benign_reclaimer(exec_ctx, t);
    if (t->sent_goaway_state == GRPC_CHTTP2_GOAWAY_SENT) {
      close_transport_locked(
          exec_ctx, t,
          GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
              "Last stream closed after sending GOAWAY", &error, 1));
    }
  }
  if (grpc_chttp2_list_remove_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2_writing:remove_stream");
  }

  GRPC_ERROR_UNREF(error);

  maybe_start_some_streams(exec_ctx, t);
}

void grpc_chttp2_cancel_stream(grpc_exec_ctx* exec_ctx,
                               grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                               grpc_error* due_to_error) {
  if (!t->is_client && !s->sent_trailing_metadata &&
      grpc_error_has_clear_grpc_status(due_to_error)) {
    close_from_api(exec_ctx, t, s, due_to_error);
    return;
  }

  if (!s->read_closed || !s->write_closed) {
    if (s->id != 0) {
      grpc_http2_error_code http_error;
      grpc_error_get_status(exec_ctx, due_to_error, s->deadline, nullptr,
                            nullptr, &http_error, nullptr);
      grpc_slice_buffer_add(
          &t->qbuf, grpc_chttp2_rst_stream_create(s->id, (uint32_t)http_error,
                                                  &s->stats.outgoing));
      grpc_chttp2_initiate_write(exec_ctx, t,
                                 GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
    }
  }
  if (due_to_error != GRPC_ERROR_NONE && !s->seen_error) {
    s->seen_error = true;
  }
  grpc_chttp2_mark_stream_closed(exec_ctx, t, s, 1, 1, due_to_error);
}

void grpc_chttp2_fake_status(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                             grpc_chttp2_stream* s, grpc_error* error) {
  grpc_status_code status;
  grpc_slice slice;
  grpc_error_get_status(exec_ctx, error, s->deadline, &status, &slice, nullptr,
                        nullptr);
  if (status != GRPC_STATUS_OK) {
    s->seen_error = true;
  }
  /* stream_global->recv_trailing_metadata_finished gives us a
     last chance replacement: we've received trailing metadata,
     but something more important has become available to signal
     to the upper layers - drop what we've got, and then publish
     what we want - which is safe because we haven't told anyone
     about the metadata yet */
  if (s->published_metadata[1] == GRPC_METADATA_NOT_PUBLISHED ||
      s->recv_trailing_metadata_finished != nullptr) {
    char status_string[GPR_LTOA_MIN_BUFSIZE];
    gpr_ltoa(status, status_string);
    GRPC_LOG_IF_ERROR("add_status",
                      grpc_chttp2_incoming_metadata_buffer_replace_or_add(
                          exec_ctx, &s->metadata_buffer[1],
                          grpc_mdelem_from_slices(
                              exec_ctx, GRPC_MDSTR_GRPC_STATUS,
                              grpc_slice_from_copied_string(status_string))));
    if (!GRPC_SLICE_IS_EMPTY(slice)) {
      GRPC_LOG_IF_ERROR(
          "add_status_message",
          grpc_chttp2_incoming_metadata_buffer_replace_or_add(
              exec_ctx, &s->metadata_buffer[1],
              grpc_mdelem_from_slices(exec_ctx, GRPC_MDSTR_GRPC_MESSAGE,
                                      grpc_slice_ref_internal(slice))));
    }
    s->published_metadata[1] = GRPC_METADATA_SYNTHESIZED_FROM_FAKE;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }

  GRPC_ERROR_UNREF(error);
}

static void add_error(grpc_error* error, grpc_error** refs, size_t* nrefs) {
  if (error == GRPC_ERROR_NONE) return;
  for (size_t i = 0; i < *nrefs; i++) {
    if (error == refs[i]) {
      return;
    }
  }
  refs[*nrefs] = error;
  ++*nrefs;
}

static grpc_error* removal_error(grpc_error* extra_error, grpc_chttp2_stream* s,
                                 const char* master_error_msg) {
  grpc_error* refs[3];
  size_t nrefs = 0;
  add_error(s->read_closed_error, refs, &nrefs);
  add_error(s->write_closed_error, refs, &nrefs);
  add_error(extra_error, refs, &nrefs);
  grpc_error* error = GRPC_ERROR_NONE;
  if (nrefs > 0) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(master_error_msg,
                                                             refs, nrefs);
  }
  GRPC_ERROR_UNREF(extra_error);
  return error;
}

static void flush_write_list(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                             grpc_chttp2_stream* s, grpc_chttp2_write_cb** list,
                             grpc_error* error) {
  while (*list) {
    grpc_chttp2_write_cb* cb = *list;
    *list = cb->next;
    grpc_chttp2_complete_closure_step(exec_ctx, t, s, &cb->closure,
                                      GRPC_ERROR_REF(error),
                                      "on_write_finished_cb");
    cb->next = t->write_cb_pool;
    t->write_cb_pool = cb;
  }
  GRPC_ERROR_UNREF(error);
}

void grpc_chttp2_fail_pending_writes(grpc_exec_ctx* exec_ctx,
                                     grpc_chttp2_transport* t,
                                     grpc_chttp2_stream* s, grpc_error* error) {
  error =
      removal_error(error, s, "Pending writes failed due to stream closure");
  s->send_initial_metadata = nullptr;
  grpc_chttp2_complete_closure_step(
      exec_ctx, t, s, &s->send_initial_metadata_finished, GRPC_ERROR_REF(error),
      "send_initial_metadata_finished");

  s->send_trailing_metadata = nullptr;
  grpc_chttp2_complete_closure_step(
      exec_ctx, t, s, &s->send_trailing_metadata_finished,
      GRPC_ERROR_REF(error), "send_trailing_metadata_finished");

  s->fetching_send_message = nullptr;
  grpc_chttp2_complete_closure_step(
      exec_ctx, t, s, &s->fetching_send_message_finished, GRPC_ERROR_REF(error),
      "fetching_send_message_finished");
  flush_write_list(exec_ctx, t, s, &s->on_write_finished_cbs,
                   GRPC_ERROR_REF(error));
  flush_write_list(exec_ctx, t, s, &s->on_flow_controlled_cbs, error);
}

void grpc_chttp2_mark_stream_closed(grpc_exec_ctx* exec_ctx,
                                    grpc_chttp2_transport* t,
                                    grpc_chttp2_stream* s, int close_reads,
                                    int close_writes, grpc_error* error) {
  if (s->read_closed && s->write_closed) {
    /* already closed */
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
    GRPC_ERROR_UNREF(error);
    return;
  }
  bool closed_read = false;
  bool became_closed = false;
  if (close_reads && !s->read_closed) {
    s->read_closed_error = GRPC_ERROR_REF(error);
    s->read_closed = true;
    closed_read = true;
  }
  if (close_writes && !s->write_closed) {
    s->write_closed_error = GRPC_ERROR_REF(error);
    s->write_closed = true;
    grpc_chttp2_fail_pending_writes(exec_ctx, t, s, GRPC_ERROR_REF(error));
  }
  if (s->read_closed && s->write_closed) {
    became_closed = true;
    grpc_error* overall_error =
        removal_error(GRPC_ERROR_REF(error), s, "Stream removed");
    if (s->id != 0) {
      remove_stream(exec_ctx, t, s->id, GRPC_ERROR_REF(overall_error));
    } else {
      /* Purge streams waiting on concurrency still waiting for id assignment */
      grpc_chttp2_list_remove_waiting_for_concurrency(t, s);
    }
    if (overall_error != GRPC_ERROR_NONE) {
      grpc_chttp2_fake_status(exec_ctx, t, s, overall_error);
    }
  }
  if (closed_read) {
    for (int i = 0; i < 2; i++) {
      if (s->published_metadata[i] == GRPC_METADATA_NOT_PUBLISHED) {
        s->published_metadata[i] = GPRC_METADATA_PUBLISHED_AT_CLOSE;
      }
    }
    grpc_chttp2_maybe_complete_recv_initial_metadata(exec_ctx, t, s);
    grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  }
  if (became_closed) {
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2");
  }
  GRPC_ERROR_UNREF(error);
}

static void close_from_api(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                           grpc_chttp2_stream* s, grpc_error* error) {
  grpc_slice hdr;
  grpc_slice status_hdr;
  grpc_slice http_status_hdr;
  grpc_slice content_type_hdr;
  grpc_slice message_pfx;
  uint8_t* p;
  uint32_t len = 0;
  grpc_status_code grpc_status;
  grpc_slice slice;
  grpc_error_get_status(exec_ctx, error, s->deadline, &grpc_status, &slice,
                        nullptr, nullptr);

  GPR_ASSERT(grpc_status >= 0 && (int)grpc_status < 100);

  /* Hand roll a header block.
     This is unnecessarily ugly - at some point we should find a more
     elegant solution.
     It's complicated by the fact that our send machinery would be dead by
     the time we got around to sending this, so instead we ignore HPACK
     compression and just write the uncompressed bytes onto the wire. */
  if (!s->sent_initial_metadata) {
    http_status_hdr = GRPC_SLICE_MALLOC(13);
    p = GRPC_SLICE_START_PTR(http_status_hdr);
    *p++ = 0x00;
    *p++ = 7;
    *p++ = ':';
    *p++ = 's';
    *p++ = 't';
    *p++ = 'a';
    *p++ = 't';
    *p++ = 'u';
    *p++ = 's';
    *p++ = 3;
    *p++ = '2';
    *p++ = '0';
    *p++ = '0';
    GPR_ASSERT(p == GRPC_SLICE_END_PTR(http_status_hdr));
    len += (uint32_t)GRPC_SLICE_LENGTH(http_status_hdr);

    content_type_hdr = GRPC_SLICE_MALLOC(31);
    p = GRPC_SLICE_START_PTR(content_type_hdr);
    *p++ = 0x00;
    *p++ = 12;
    *p++ = 'c';
    *p++ = 'o';
    *p++ = 'n';
    *p++ = 't';
    *p++ = 'e';
    *p++ = 'n';
    *p++ = 't';
    *p++ = '-';
    *p++ = 't';
    *p++ = 'y';
    *p++ = 'p';
    *p++ = 'e';
    *p++ = 16;
    *p++ = 'a';
    *p++ = 'p';
    *p++ = 'p';
    *p++ = 'l';
    *p++ = 'i';
    *p++ = 'c';
    *p++ = 'a';
    *p++ = 't';
    *p++ = 'i';
    *p++ = 'o';
    *p++ = 'n';
    *p++ = '/';
    *p++ = 'g';
    *p++ = 'r';
    *p++ = 'p';
    *p++ = 'c';
    GPR_ASSERT(p == GRPC_SLICE_END_PTR(content_type_hdr));
    len += (uint32_t)GRPC_SLICE_LENGTH(content_type_hdr);
  }

  status_hdr = GRPC_SLICE_MALLOC(15 + (grpc_status >= 10));
  p = GRPC_SLICE_START_PTR(status_hdr);
  *p++ = 0x00; /* literal header, not indexed */
  *p++ = 11;   /* len(grpc-status) */
  *p++ = 'g';
  *p++ = 'r';
  *p++ = 'p';
  *p++ = 'c';
  *p++ = '-';
  *p++ = 's';
  *p++ = 't';
  *p++ = 'a';
  *p++ = 't';
  *p++ = 'u';
  *p++ = 's';
  if (grpc_status < 10) {
    *p++ = 1;
    *p++ = (uint8_t)('0' + grpc_status);
  } else {
    *p++ = 2;
    *p++ = (uint8_t)('0' + (grpc_status / 10));
    *p++ = (uint8_t)('0' + (grpc_status % 10));
  }
  GPR_ASSERT(p == GRPC_SLICE_END_PTR(status_hdr));
  len += (uint32_t)GRPC_SLICE_LENGTH(status_hdr);

  size_t msg_len = GRPC_SLICE_LENGTH(slice);
  GPR_ASSERT(msg_len <= UINT32_MAX);
  uint32_t msg_len_len = GRPC_CHTTP2_VARINT_LENGTH((uint32_t)msg_len, 1);
  message_pfx = GRPC_SLICE_MALLOC(14 + msg_len_len);
  p = GRPC_SLICE_START_PTR(message_pfx);
  *p++ = 0x00; /* literal header, not indexed */
  *p++ = 12;   /* len(grpc-message) */
  *p++ = 'g';
  *p++ = 'r';
  *p++ = 'p';
  *p++ = 'c';
  *p++ = '-';
  *p++ = 'm';
  *p++ = 'e';
  *p++ = 's';
  *p++ = 's';
  *p++ = 'a';
  *p++ = 'g';
  *p++ = 'e';
  GRPC_CHTTP2_WRITE_VARINT((uint32_t)msg_len, 1, 0, p, (uint32_t)msg_len_len);
  p += msg_len_len;
  GPR_ASSERT(p == GRPC_SLICE_END_PTR(message_pfx));
  len += (uint32_t)GRPC_SLICE_LENGTH(message_pfx);
  len += (uint32_t)msg_len;

  hdr = GRPC_SLICE_MALLOC(9);
  p = GRPC_SLICE_START_PTR(hdr);
  *p++ = (uint8_t)(len >> 16);
  *p++ = (uint8_t)(len >> 8);
  *p++ = (uint8_t)(len);
  *p++ = GRPC_CHTTP2_FRAME_HEADER;
  *p++ = GRPC_CHTTP2_DATA_FLAG_END_STREAM | GRPC_CHTTP2_DATA_FLAG_END_HEADERS;
  *p++ = (uint8_t)(s->id >> 24);
  *p++ = (uint8_t)(s->id >> 16);
  *p++ = (uint8_t)(s->id >> 8);
  *p++ = (uint8_t)(s->id);
  GPR_ASSERT(p == GRPC_SLICE_END_PTR(hdr));

  grpc_slice_buffer_add(&t->qbuf, hdr);
  if (!s->sent_initial_metadata) {
    grpc_slice_buffer_add(&t->qbuf, http_status_hdr);
    grpc_slice_buffer_add(&t->qbuf, content_type_hdr);
  }
  grpc_slice_buffer_add(&t->qbuf, status_hdr);
  grpc_slice_buffer_add(&t->qbuf, message_pfx);
  grpc_slice_buffer_add(&t->qbuf, grpc_slice_ref_internal(slice));
  grpc_slice_buffer_add(
      &t->qbuf, grpc_chttp2_rst_stream_create(s->id, GRPC_HTTP2_NO_ERROR,
                                              &s->stats.outgoing));

  grpc_chttp2_mark_stream_closed(exec_ctx, t, s, 1, 1, error);
  grpc_chttp2_initiate_write(exec_ctx, t,
                             GRPC_CHTTP2_INITIATE_WRITE_CLOSE_FROM_API);
}

typedef struct {
  grpc_exec_ctx* exec_ctx;
  grpc_error* error;
  grpc_chttp2_transport* t;
} cancel_stream_cb_args;

static void cancel_stream_cb(void* user_data, uint32_t key, void* stream) {
  cancel_stream_cb_args* args = (cancel_stream_cb_args*)user_data;
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)stream;
  grpc_chttp2_cancel_stream(args->exec_ctx, args->t, s,
                            GRPC_ERROR_REF(args->error));
}

static void end_all_the_calls(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                              grpc_error* error) {
  cancel_stream_cb_args args = {exec_ctx, error, t};
  grpc_chttp2_stream_map_for_each(&t->stream_map, cancel_stream_cb, &args);
  GRPC_ERROR_UNREF(error);
}

/*******************************************************************************
 * INPUT PROCESSING - PARSING
 */

template <class F>
static void WithUrgency(grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t,
                        grpc_core::chttp2::FlowControlAction::Urgency urgency,
                        grpc_chttp2_initiate_write_reason reason, F action) {
  switch (urgency) {
    case grpc_core::chttp2::FlowControlAction::Urgency::NO_ACTION_NEEDED:
      break;
    case grpc_core::chttp2::FlowControlAction::Urgency::UPDATE_IMMEDIATELY:
      grpc_chttp2_initiate_write(exec_ctx, t, reason);
    // fallthrough
    case grpc_core::chttp2::FlowControlAction::Urgency::QUEUE_UPDATE:
      action();
      break;
  }
}

void grpc_chttp2_act_on_flowctl_action(
    grpc_exec_ctx* exec_ctx, const grpc_core::chttp2::FlowControlAction& action,
    grpc_chttp2_transport* t, grpc_chttp2_stream* s) {
  WithUrgency(
      exec_ctx, t, action.send_stream_update(),
      GRPC_CHTTP2_INITIATE_WRITE_STREAM_FLOW_CONTROL,
      [exec_ctx, t, s]() { grpc_chttp2_mark_stream_writable(exec_ctx, t, s); });
  WithUrgency(exec_ctx, t, action.send_transport_update(),
              GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL, []() {});
  WithUrgency(exec_ctx, t, action.send_initial_window_update(),
              GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS,
              [exec_ctx, t, &action]() {
                queue_setting_update(exec_ctx, t,
                                     GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
                                     action.initial_window_size());
              });
  WithUrgency(
      exec_ctx, t, action.send_max_frame_size_update(),
      GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS, [exec_ctx, t, &action]() {
        queue_setting_update(exec_ctx, t, GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
                             action.max_frame_size());
      });
}

static grpc_error* try_http_parsing(grpc_exec_ctx* exec_ctx,
                                    grpc_chttp2_transport* t) {
  grpc_http_parser parser;
  size_t i = 0;
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_http_response response;
  memset(&response, 0, sizeof(response));

  grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, &response);

  grpc_error* parse_error = GRPC_ERROR_NONE;
  for (; i < t->read_buffer.count && parse_error == GRPC_ERROR_NONE; i++) {
    parse_error =
        grpc_http_parser_parse(&parser, t->read_buffer.slices[i], nullptr);
  }
  if (parse_error == GRPC_ERROR_NONE &&
      (parse_error = grpc_http_parser_eof(&parser)) == GRPC_ERROR_NONE) {
    error = grpc_error_set_int(
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "Trying to connect an http1.x server"),
                           GRPC_ERROR_INT_HTTP_STATUS, response.status),
        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
  }
  GRPC_ERROR_UNREF(parse_error);

  grpc_http_parser_destroy(&parser);
  grpc_http_response_destroy(&response);
  return error;
}

static void read_action_locked(grpc_exec_ctx* exec_ctx, void* tp,
                               grpc_error* error) {
  GPR_TIMER_BEGIN("reading_action_locked", 0);

  grpc_chttp2_transport* t = (grpc_chttp2_transport*)tp;

  GRPC_ERROR_REF(error);

  grpc_error* err = error;
  if (err != GRPC_ERROR_NONE) {
    err = grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                 "Endpoint read failed", &err, 1),
                             GRPC_ERROR_INT_OCCURRED_DURING_WRITE,
                             t->write_state);
  }
  GPR_SWAP(grpc_error*, err, error);
  GRPC_ERROR_UNREF(err);
  if (t->closed_with_error == GRPC_ERROR_NONE) {
    GPR_TIMER_BEGIN("reading_action.parse", 0);
    size_t i = 0;
    grpc_error* errors[3] = {GRPC_ERROR_REF(error), GRPC_ERROR_NONE,
                             GRPC_ERROR_NONE};
    for (; i < t->read_buffer.count && errors[1] == GRPC_ERROR_NONE; i++) {
      t->flow_control->bdp_estimator()->AddIncomingBytes(
          (int64_t)GRPC_SLICE_LENGTH(t->read_buffer.slices[i]));
      errors[1] =
          grpc_chttp2_perform_read(exec_ctx, t, t->read_buffer.slices[i]);
    }
    if (errors[1] != GRPC_ERROR_NONE) {
      errors[2] = try_http_parsing(exec_ctx, t);
      GRPC_ERROR_UNREF(error);
      error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Failed parsing HTTP/2", errors, GPR_ARRAY_SIZE(errors));
    }
    for (i = 0; i < GPR_ARRAY_SIZE(errors); i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
    GPR_TIMER_END("reading_action.parse", 0);

    GPR_TIMER_BEGIN("post_parse_locked", 0);
    if (t->initial_window_update != 0) {
      if (t->initial_window_update > 0) {
        grpc_chttp2_stream* s;
        while (grpc_chttp2_list_pop_stalled_by_stream(t, &s)) {
          grpc_chttp2_mark_stream_writable(exec_ctx, t, s);
          grpc_chttp2_initiate_write(
              exec_ctx, t,
              GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_SETTING);
        }
      }
      t->initial_window_update = 0;
    }
    GPR_TIMER_END("post_parse_locked", 0);
  }

  GPR_TIMER_BEGIN("post_reading_action_locked", 0);
  bool keep_reading = false;
  if (error == GRPC_ERROR_NONE && t->closed_with_error != GRPC_ERROR_NONE) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Transport closed", &t->closed_with_error, 1);
  }
  if (error != GRPC_ERROR_NONE) {
    /* If a goaway frame was received, this might be the reason why the read
     * failed. Add this info to the error */
    if (t->goaway_error != GRPC_ERROR_NONE) {
      error = grpc_error_add_child(error, GRPC_ERROR_REF(t->goaway_error));
    }

    close_transport_locked(exec_ctx, t, GRPC_ERROR_REF(error));
    t->endpoint_reading = 0;
  } else if (t->closed_with_error == GRPC_ERROR_NONE) {
    keep_reading = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "keep_reading");
  }
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &t->read_buffer);

  if (keep_reading) {
    grpc_endpoint_read(exec_ctx, t->ep, &t->read_buffer,
                       &t->read_action_locked);
    grpc_chttp2_act_on_flowctl_action(exec_ctx, t->flow_control->MakeAction(),
                                      t, nullptr);
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "keep_reading");
  } else {
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "reading_action");
  }

  GPR_TIMER_END("post_reading_action_locked", 0);

  GRPC_ERROR_UNREF(error);

  GPR_TIMER_END("reading_action_locked", 0);
}

// t is reffed prior to calling the first time, and once the callback chain
// that kicks off finishes, it's unreffed
static void schedule_bdp_ping_locked(grpc_exec_ctx* exec_ctx,
                                     grpc_chttp2_transport* t) {
  t->flow_control->bdp_estimator()->SchedulePing();
  send_ping_locked(exec_ctx, t, &t->start_bdp_ping_locked,
                   &t->finish_bdp_ping_locked);
}

static void start_bdp_ping_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                  grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)tp;
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s: Start BDP ping err=%s", t->peer_string,
            grpc_error_string(error));
  }
  /* Reset the keepalive ping timer */
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING) {
    grpc_timer_cancel(exec_ctx, &t->keepalive_ping_timer);
  }
  t->flow_control->bdp_estimator()->StartPing();
}

static void finish_bdp_ping_locked(grpc_exec_ctx* exec_ctx, void* tp,
                                   grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)tp;
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s: Complete BDP ping err=%s", t->peer_string,
            grpc_error_string(error));
  }
  if (error != GRPC_ERROR_NONE) {
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "bdp_ping");
    return;
  }
  grpc_millis next_ping =
      t->flow_control->bdp_estimator()->CompletePing(exec_ctx);
  grpc_chttp2_act_on_flowctl_action(
      exec_ctx, t->flow_control->PeriodicUpdate(exec_ctx), t, nullptr);
  GPR_ASSERT(!t->have_next_bdp_ping_timer);
  t->have_next_bdp_ping_timer = true;
  grpc_timer_init(exec_ctx, &t->next_bdp_ping_timer, next_ping,
                  &t->next_bdp_ping_timer_expired_locked);
}

static void next_bdp_ping_timer_expired_locked(grpc_exec_ctx* exec_ctx,
                                               void* tp, grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)tp;
  GPR_ASSERT(t->have_next_bdp_ping_timer);
  t->have_next_bdp_ping_timer = false;
  if (error != GRPC_ERROR_NONE) {
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "bdp_ping");
    return;
  }
  schedule_bdp_ping_locked(exec_ctx, t);
}

void grpc_chttp2_config_default_keepalive_args(grpc_channel_args* args,
                                               bool is_client) {
  size_t i;
  if (args) {
    for (i = 0; i < args->num_args; i++) {
      if (0 == strcmp(args->args[i].key, GRPC_ARG_KEEPALIVE_TIME_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &args->args[i], {g_default_client_keepalive_time_ms, 1, INT_MAX});
        if (is_client) {
          g_default_client_keepalive_time_ms = value;
        } else {
          g_default_server_keepalive_time_ms = value;
        }
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_ARG_KEEPALIVE_TIMEOUT_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &args->args[i],
            {g_default_client_keepalive_timeout_ms, 0, INT_MAX});
        if (is_client) {
          g_default_client_keepalive_timeout_ms = value;
        } else {
          g_default_server_keepalive_timeout_ms = value;
        }
      } else if (0 == strcmp(args->args[i].key,
                             GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)) {
        g_default_keepalive_permit_without_calls =
            (uint32_t)grpc_channel_arg_get_integer(
                &args->args[i],
                {g_default_keepalive_permit_without_calls, 0, 1});
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_ARG_HTTP2_MAX_PING_STRIKES)) {
        g_default_max_ping_strikes = grpc_channel_arg_get_integer(
            &args->args[i], {g_default_max_ping_strikes, 0, INT_MAX});
      } else if (0 == strcmp(args->args[i].key,
                             GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA)) {
        g_default_max_pings_without_data = grpc_channel_arg_get_integer(
            &args->args[i], {g_default_max_pings_without_data, 0, INT_MAX});
      } else if (0 ==
                 strcmp(
                     args->args[i].key,
                     GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS)) {
        g_default_min_sent_ping_interval_without_data_ms =
            grpc_channel_arg_get_integer(
                &args->args[i],
                {g_default_min_sent_ping_interval_without_data_ms, 0, INT_MAX});
      } else if (0 ==
                 strcmp(
                     args->args[i].key,
                     GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)) {
        g_default_min_recv_ping_interval_without_data_ms =
            grpc_channel_arg_get_integer(
                &args->args[i],
                {g_default_min_recv_ping_interval_without_data_ms, 0, INT_MAX});
      }
    }
  }
}

static void init_keepalive_ping_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                       grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)arg;
  GPR_ASSERT(t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING);
  if (t->destroying || t->closed_with_error != GRPC_ERROR_NONE) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
  } else if (error == GRPC_ERROR_NONE) {
    if (t->keepalive_permit_without_calls ||
        grpc_chttp2_stream_map_size(&t->stream_map) > 0) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_PINGING;
      GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive ping end");
      send_ping_locked(exec_ctx, t, &t->start_keepalive_ping_locked,
                       &t->finish_keepalive_ping_locked);
      grpc_chttp2_initiate_write(exec_ctx, t,
                                 GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING);
    } else {
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      grpc_timer_init(exec_ctx, &t->keepalive_ping_timer,
                      grpc_exec_ctx_now(exec_ctx) + t->keepalive_time,
                      &t->init_keepalive_ping_locked);
    }
  } else if (error == GRPC_ERROR_CANCELLED) {
    /* The keepalive ping timer may be cancelled by bdp */
    GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
    grpc_timer_init(exec_ctx, &t->keepalive_ping_timer,
                    grpc_exec_ctx_now(exec_ctx) + t->keepalive_time,
                    &t->init_keepalive_ping_locked);
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "init keepalive ping");
}

static void start_keepalive_ping_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                        grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)arg;
  GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive watchdog");
  grpc_timer_init(exec_ctx, &t->keepalive_watchdog_timer,
                  grpc_exec_ctx_now(exec_ctx) + t->keepalive_time,
                  &t->keepalive_watchdog_fired_locked);
}

static void finish_keepalive_ping_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                         grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)arg;
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    if (error == GRPC_ERROR_NONE) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
      grpc_timer_cancel(exec_ctx, &t->keepalive_watchdog_timer);
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      grpc_timer_init(exec_ctx, &t->keepalive_ping_timer,
                      grpc_exec_ctx_now(exec_ctx) + t->keepalive_time,
                      &t->init_keepalive_ping_locked);
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "keepalive ping end");
}

static void keepalive_watchdog_fired_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                            grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)arg;
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    if (error == GRPC_ERROR_NONE) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
      close_transport_locked(
          exec_ctx, t,
          grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                 "keepalive watchdog timeout"),
                             GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_INTERNAL));
    }
  } else {
    /* The watchdog timer should have been cancelled by
     * finish_keepalive_ping_locked. */
    if (error != GRPC_ERROR_CANCELLED) {
      gpr_log(GPR_ERROR, "keepalive_ping_end state error: %d (expect: %d)",
              t->keepalive_state, GRPC_CHTTP2_KEEPALIVE_STATE_PINGING);
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "keepalive watchdog");
}

/*******************************************************************************
 * CALLBACK LOOP
 */

static void connectivity_state_set(grpc_exec_ctx* exec_ctx,
                                   grpc_chttp2_transport* t,
                                   grpc_connectivity_state state,
                                   grpc_error* error, const char* reason) {
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_DEBUG, "set connectivity_state=%d", state));
  grpc_connectivity_state_set(exec_ctx, &t->channel_callback.state_tracker,
                              state, error, reason);
}

/*******************************************************************************
 * POLLSET STUFF
 */

static void set_pollset(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                        grpc_stream* gs, grpc_pollset* pollset) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  grpc_endpoint_add_to_pollset(exec_ctx, t->ep, pollset);
}

static void set_pollset_set(grpc_exec_ctx* exec_ctx, grpc_transport* gt,
                            grpc_stream* gs, grpc_pollset_set* pollset_set) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)gt;
  grpc_endpoint_add_to_pollset_set(exec_ctx, t->ep, pollset_set);
}

/*******************************************************************************
 * BYTE STREAM
 */

static void reset_byte_stream(grpc_exec_ctx* exec_ctx, void* arg,
                              grpc_error* error) {
  grpc_chttp2_stream* s = (grpc_chttp2_stream*)arg;

  s->pending_byte_stream = false;
  if (error == GRPC_ERROR_NONE) {
    grpc_chttp2_maybe_complete_recv_message(exec_ctx, s->t, s);
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, s->t, s);
  } else {
    GPR_ASSERT(error != GRPC_ERROR_NONE);
    GRPC_CLOSURE_SCHED(exec_ctx, s->on_next, GRPC_ERROR_REF(error));
    s->on_next = nullptr;
    GRPC_ERROR_UNREF(s->byte_stream_error);
    s->byte_stream_error = GRPC_ERROR_NONE;
    grpc_chttp2_cancel_stream(exec_ctx, s->t, s, GRPC_ERROR_REF(error));
    s->byte_stream_error = GRPC_ERROR_REF(error);
  }
}

static void incoming_byte_stream_unref(grpc_exec_ctx* exec_ctx,
                                       grpc_chttp2_incoming_byte_stream* bs) {
  if (gpr_unref(&bs->refs)) {
    gpr_free(bs);
  }
}

static void incoming_byte_stream_next_locked(grpc_exec_ctx* exec_ctx,
                                             void* argp,
                                             grpc_error* error_ignored) {
  grpc_chttp2_incoming_byte_stream* bs =
      (grpc_chttp2_incoming_byte_stream*)argp;
  grpc_chttp2_transport* t = bs->transport;
  grpc_chttp2_stream* s = bs->stream;

  size_t cur_length = s->frame_storage.length;
  if (!s->read_closed) {
    s->flow_control->IncomingByteStreamUpdate(bs->next_action.max_size_hint,
                                              cur_length);
    grpc_chttp2_act_on_flowctl_action(exec_ctx, s->flow_control->MakeAction(),
                                      t, s);
  }
  GPR_ASSERT(s->unprocessed_incoming_frames_buffer.length == 0);
  if (s->frame_storage.length > 0) {
    grpc_slice_buffer_swap(&s->frame_storage,
                           &s->unprocessed_incoming_frames_buffer);
    s->unprocessed_incoming_frames_decompressed = false;
    GRPC_CLOSURE_SCHED(exec_ctx, bs->next_action.on_complete, GRPC_ERROR_NONE);
  } else if (s->byte_stream_error != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_SCHED(exec_ctx, bs->next_action.on_complete,
                       GRPC_ERROR_REF(s->byte_stream_error));
    if (s->data_parser.parsing_frame != nullptr) {
      incoming_byte_stream_unref(exec_ctx, s->data_parser.parsing_frame);
      s->data_parser.parsing_frame = nullptr;
    }
  } else if (s->read_closed) {
    if (bs->remaining_bytes != 0) {
      s->byte_stream_error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
      GRPC_CLOSURE_SCHED(exec_ctx, bs->next_action.on_complete,
                         GRPC_ERROR_REF(s->byte_stream_error));
      if (s->data_parser.parsing_frame != nullptr) {
        incoming_byte_stream_unref(exec_ctx, s->data_parser.parsing_frame);
        s->data_parser.parsing_frame = nullptr;
      }
    } else {
      /* Should never reach here. */
      GPR_ASSERT(false);
    }
  } else {
    s->on_next = bs->next_action.on_complete;
  }
  incoming_byte_stream_unref(exec_ctx, bs);
}

static bool incoming_byte_stream_next(grpc_exec_ctx* exec_ctx,
                                      grpc_byte_stream* byte_stream,
                                      size_t max_size_hint,
                                      grpc_closure* on_complete) {
  GPR_TIMER_BEGIN("incoming_byte_stream_next", 0);
  grpc_chttp2_incoming_byte_stream* bs =
      (grpc_chttp2_incoming_byte_stream*)byte_stream;
  grpc_chttp2_stream* s = bs->stream;
  if (s->unprocessed_incoming_frames_buffer.length > 0) {
    GPR_TIMER_END("incoming_byte_stream_next", 0);
    return true;
  } else {
    gpr_ref(&bs->refs);
    bs->next_action.max_size_hint = max_size_hint;
    bs->next_action.on_complete = on_complete;
    GRPC_CLOSURE_SCHED(
        exec_ctx,
        GRPC_CLOSURE_INIT(&bs->next_action.closure,
                          incoming_byte_stream_next_locked, bs,
                          grpc_combiner_scheduler(bs->transport->combiner)),
        GRPC_ERROR_NONE);
    GPR_TIMER_END("incoming_byte_stream_next", 0);
    return false;
  }
}

static grpc_error* incoming_byte_stream_pull(grpc_exec_ctx* exec_ctx,
                                             grpc_byte_stream* byte_stream,
                                             grpc_slice* slice) {
  GPR_TIMER_BEGIN("incoming_byte_stream_pull", 0);
  grpc_chttp2_incoming_byte_stream* bs =
      (grpc_chttp2_incoming_byte_stream*)byte_stream;
  grpc_chttp2_stream* s = bs->stream;
  grpc_error* error;

  if (s->unprocessed_incoming_frames_buffer.length > 0) {
    if (!s->unprocessed_incoming_frames_decompressed) {
      bool end_of_context;
      if (!s->stream_decompression_ctx) {
        s->stream_decompression_ctx = grpc_stream_compression_context_create(
            s->stream_decompression_method);
      }
      if (!grpc_stream_decompress(s->stream_decompression_ctx,
                                  &s->unprocessed_incoming_frames_buffer,
                                  &s->decompressed_data_buffer, nullptr,
                                  MAX_SIZE_T, &end_of_context)) {
        error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Stream decompression error.");
        return error;
      }
      GPR_ASSERT(s->unprocessed_incoming_frames_buffer.length == 0);
      grpc_slice_buffer_swap(&s->unprocessed_incoming_frames_buffer,
                             &s->decompressed_data_buffer);
      s->unprocessed_incoming_frames_decompressed = true;
      if (end_of_context) {
        grpc_stream_compression_context_destroy(s->stream_decompression_ctx);
        s->stream_decompression_ctx = nullptr;
      }
      if (s->unprocessed_incoming_frames_buffer.length == 0) {
        *slice = grpc_empty_slice();
      }
    }
    error = grpc_deframe_unprocessed_incoming_frames(
        exec_ctx, &s->data_parser, s, &s->unprocessed_incoming_frames_buffer,
        slice, nullptr);
    if (error != GRPC_ERROR_NONE) {
      return error;
    }
  } else {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
    GRPC_CLOSURE_SCHED(exec_ctx, &s->reset_byte_stream, GRPC_ERROR_REF(error));
    return error;
  }
  GPR_TIMER_END("incoming_byte_stream_pull", 0);
  return GRPC_ERROR_NONE;
}

static void incoming_byte_stream_destroy_locked(grpc_exec_ctx* exec_ctx,
                                                void* byte_stream,
                                                grpc_error* error_ignored);

static void incoming_byte_stream_destroy(grpc_exec_ctx* exec_ctx,
                                         grpc_byte_stream* byte_stream) {
  GPR_TIMER_BEGIN("incoming_byte_stream_destroy", 0);
  grpc_chttp2_incoming_byte_stream* bs =
      (grpc_chttp2_incoming_byte_stream*)byte_stream;
  GRPC_CLOSURE_SCHED(
      exec_ctx,
      GRPC_CLOSURE_INIT(&bs->destroy_action,
                        incoming_byte_stream_destroy_locked, bs,
                        grpc_combiner_scheduler(bs->transport->combiner)),
      GRPC_ERROR_NONE);
  GPR_TIMER_END("incoming_byte_stream_destroy", 0);
}

static void incoming_byte_stream_publish_error(
    grpc_exec_ctx* exec_ctx, grpc_chttp2_incoming_byte_stream* bs,
    grpc_error* error) {
  grpc_chttp2_stream* s = bs->stream;

  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_CLOSURE_SCHED(exec_ctx, s->on_next, GRPC_ERROR_REF(error));
  s->on_next = nullptr;
  GRPC_ERROR_UNREF(s->byte_stream_error);
  s->byte_stream_error = GRPC_ERROR_REF(error);
  grpc_chttp2_cancel_stream(exec_ctx, bs->transport, bs->stream,
                            GRPC_ERROR_REF(error));
}

grpc_error* grpc_chttp2_incoming_byte_stream_push(
    grpc_exec_ctx* exec_ctx, grpc_chttp2_incoming_byte_stream* bs,
    grpc_slice slice, grpc_slice* slice_out) {
  grpc_chttp2_stream* s = bs->stream;

  if (bs->remaining_bytes < GRPC_SLICE_LENGTH(slice)) {
    grpc_error* error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Too many bytes in stream");

    GRPC_CLOSURE_SCHED(exec_ctx, &s->reset_byte_stream, GRPC_ERROR_REF(error));
    grpc_slice_unref_internal(exec_ctx, slice);
    return error;
  } else {
    bs->remaining_bytes -= (uint32_t)GRPC_SLICE_LENGTH(slice);
    if (slice_out != nullptr) {
      *slice_out = slice;
    }
    return GRPC_ERROR_NONE;
  }
}

grpc_error* grpc_chttp2_incoming_byte_stream_finished(
    grpc_exec_ctx* exec_ctx, grpc_chttp2_incoming_byte_stream* bs,
    grpc_error* error, bool reset_on_error) {
  grpc_chttp2_stream* s = bs->stream;

  if (error == GRPC_ERROR_NONE) {
    if (bs->remaining_bytes != 0) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
    }
  }
  if (error != GRPC_ERROR_NONE && reset_on_error) {
    GRPC_CLOSURE_SCHED(exec_ctx, &s->reset_byte_stream, GRPC_ERROR_REF(error));
  }
  incoming_byte_stream_unref(exec_ctx, bs);
  return error;
}

static void incoming_byte_stream_shutdown(grpc_exec_ctx* exec_ctx,
                                          grpc_byte_stream* byte_stream,
                                          grpc_error* error) {
  grpc_chttp2_incoming_byte_stream* bs =
      (grpc_chttp2_incoming_byte_stream*)byte_stream;
  GRPC_ERROR_UNREF(grpc_chttp2_incoming_byte_stream_finished(
      exec_ctx, bs, error, true /* reset_on_error */));
}

static const grpc_byte_stream_vtable grpc_chttp2_incoming_byte_stream_vtable = {
    incoming_byte_stream_next, incoming_byte_stream_pull,
    incoming_byte_stream_shutdown, incoming_byte_stream_destroy};

static void incoming_byte_stream_destroy_locked(grpc_exec_ctx* exec_ctx,
                                                void* byte_stream,
                                                grpc_error* error_ignored) {
  grpc_chttp2_incoming_byte_stream* bs =
      (grpc_chttp2_incoming_byte_stream*)byte_stream;
  grpc_chttp2_stream* s = bs->stream;
  grpc_chttp2_transport* t = s->t;

  GPR_ASSERT(bs->base.vtable == &grpc_chttp2_incoming_byte_stream_vtable);
  incoming_byte_stream_unref(exec_ctx, bs);
  s->pending_byte_stream = false;
  grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
}

grpc_chttp2_incoming_byte_stream* grpc_chttp2_incoming_byte_stream_create(
    grpc_exec_ctx* exec_ctx, grpc_chttp2_transport* t, grpc_chttp2_stream* s,
    uint32_t frame_size, uint32_t flags) {
  grpc_chttp2_incoming_byte_stream* incoming_byte_stream =
      (grpc_chttp2_incoming_byte_stream*)gpr_malloc(
          sizeof(*incoming_byte_stream));
  incoming_byte_stream->base.length = frame_size;
  incoming_byte_stream->remaining_bytes = frame_size;
  incoming_byte_stream->base.flags = flags;
  incoming_byte_stream->base.vtable = &grpc_chttp2_incoming_byte_stream_vtable;
  gpr_ref_init(&incoming_byte_stream->refs, 2);
  incoming_byte_stream->transport = t;
  incoming_byte_stream->stream = s;
  GRPC_ERROR_UNREF(s->byte_stream_error);
  s->byte_stream_error = GRPC_ERROR_NONE;
  return incoming_byte_stream;
}

/*******************************************************************************
 * RESOURCE QUOTAS
 */

static void post_benign_reclaimer(grpc_exec_ctx* exec_ctx,
                                  grpc_chttp2_transport* t) {
  if (!t->benign_reclaimer_registered) {
    t->benign_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "benign_reclaimer");
    grpc_resource_user_post_reclaimer(exec_ctx,
                                      grpc_endpoint_get_resource_user(t->ep),
                                      false, &t->benign_reclaimer_locked);
  }
}

static void post_destructive_reclaimer(grpc_exec_ctx* exec_ctx,
                                       grpc_chttp2_transport* t) {
  if (!t->destructive_reclaimer_registered) {
    t->destructive_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "destructive_reclaimer");
    grpc_resource_user_post_reclaimer(exec_ctx,
                                      grpc_endpoint_get_resource_user(t->ep),
                                      true, &t->destructive_reclaimer_locked);
  }
}

static void benign_reclaimer_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                    grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)arg;
  if (error == GRPC_ERROR_NONE &&
      grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    /* Channel with no active streams: send a goaway to try and make it
     * disconnect cleanly */
    if (grpc_resource_quota_trace.enabled()) {
      gpr_log(GPR_DEBUG, "HTTP2: %s - send goaway to free memory",
              t->peer_string);
    }
    send_goaway(exec_ctx, t,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("Buffers full"),
                    GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_ENHANCE_YOUR_CALM));
  } else if (error == GRPC_ERROR_NONE && grpc_resource_quota_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "HTTP2: %s - skip benign reclamation, there are still %" PRIdPTR
            " streams",
            t->peer_string, grpc_chttp2_stream_map_size(&t->stream_map));
  }
  t->benign_reclaimer_registered = false;
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_resource_user_finish_reclamation(
        exec_ctx, grpc_endpoint_get_resource_user(t->ep));
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "benign_reclaimer");
}

static void destructive_reclaimer_locked(grpc_exec_ctx* exec_ctx, void* arg,
                                         grpc_error* error) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)arg;
  size_t n = grpc_chttp2_stream_map_size(&t->stream_map);
  t->destructive_reclaimer_registered = false;
  if (error == GRPC_ERROR_NONE && n > 0) {
    grpc_chttp2_stream* s =
        (grpc_chttp2_stream*)grpc_chttp2_stream_map_rand(&t->stream_map);
    if (grpc_resource_quota_trace.enabled()) {
      gpr_log(GPR_DEBUG, "HTTP2: %s - abandon stream id %d", t->peer_string,
              s->id);
    }
    grpc_chttp2_cancel_stream(
        exec_ctx, t, s,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Buffers full"),
                           GRPC_ERROR_INT_HTTP2_ERROR,
                           GRPC_HTTP2_ENHANCE_YOUR_CALM));
    if (n > 1) {
      /* Since we cancel one stream per destructive reclamation, if
         there are more streams left, we can immediately post a new
         reclaimer in case the resource quota needs to free more
         memory */
      post_destructive_reclaimer(exec_ctx, t);
    }
  }
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_resource_user_finish_reclamation(
        exec_ctx, grpc_endpoint_get_resource_user(t->ep));
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "destructive_reclaimer");
}

/*******************************************************************************
 * MONITORING
 */

const char* grpc_chttp2_initiate_write_reason_string(
    grpc_chttp2_initiate_write_reason reason) {
  switch (reason) {
    case GRPC_CHTTP2_INITIATE_WRITE_INITIAL_WRITE:
      return "INITIAL_WRITE";
    case GRPC_CHTTP2_INITIATE_WRITE_START_NEW_STREAM:
      return "START_NEW_STREAM";
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_MESSAGE:
      return "SEND_MESSAGE";
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_INITIAL_METADATA:
      return "SEND_INITIAL_METADATA";
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_TRAILING_METADATA:
      return "SEND_TRAILING_METADATA";
    case GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING:
      return "RETRY_SEND_PING";
    case GRPC_CHTTP2_INITIATE_WRITE_CONTINUE_PINGS:
      return "CONTINUE_PINGS";
    case GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT:
      return "GOAWAY_SENT";
    case GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM:
      return "RST_STREAM";
    case GRPC_CHTTP2_INITIATE_WRITE_CLOSE_FROM_API:
      return "CLOSE_FROM_API";
    case GRPC_CHTTP2_INITIATE_WRITE_STREAM_FLOW_CONTROL:
      return "STREAM_FLOW_CONTROL";
    case GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL:
      return "TRANSPORT_FLOW_CONTROL";
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS:
      return "SEND_SETTINGS";
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_SETTING:
      return "FLOW_CONTROL_UNSTALLED_BY_SETTING";
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_UPDATE:
      return "FLOW_CONTROL_UNSTALLED_BY_UPDATE";
    case GRPC_CHTTP2_INITIATE_WRITE_APPLICATION_PING:
      return "APPLICATION_PING";
    case GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING:
      return "KEEPALIVE_PING";
    case GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL_UNSTALLED:
      return "TRANSPORT_FLOW_CONTROL_UNSTALLED";
    case GRPC_CHTTP2_INITIATE_WRITE_PING_RESPONSE:
      return "PING_RESPONSE";
    case GRPC_CHTTP2_INITIATE_WRITE_FORCE_RST_STREAM:
      return "FORCE_RST_STREAM";
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

static grpc_endpoint* chttp2_get_endpoint(grpc_exec_ctx* exec_ctx,
                                          grpc_transport* t) {
  return ((grpc_chttp2_transport*)t)->ep;
}

static const grpc_transport_vtable vtable = {sizeof(grpc_chttp2_stream),
                                             "chttp2",
                                             init_stream,
                                             set_pollset,
                                             set_pollset_set,
                                             perform_stream_op,
                                             perform_transport_op,
                                             destroy_stream,
                                             destroy_transport,
                                             chttp2_get_endpoint};

static const grpc_transport_vtable* get_vtable(void) { return &vtable; }

grpc_transport* grpc_create_chttp2_transport(
    grpc_exec_ctx* exec_ctx, const grpc_channel_args* channel_args,
    grpc_endpoint* ep, bool is_client) {
  grpc_chttp2_transport* t =
      (grpc_chttp2_transport*)gpr_zalloc(sizeof(grpc_chttp2_transport));
  init_transport(exec_ctx, t, channel_args, ep, is_client);
  return &t->base;
}

void grpc_chttp2_transport_start_reading(
    grpc_exec_ctx* exec_ctx, grpc_transport* transport,
    grpc_slice_buffer* read_buffer, grpc_closure* notify_on_receive_settings) {
  grpc_chttp2_transport* t = (grpc_chttp2_transport*)transport;
  GRPC_CHTTP2_REF_TRANSPORT(
      t, "reading_action"); /* matches unref inside reading_action */
  if (read_buffer != nullptr) {
    grpc_slice_buffer_move_into(read_buffer, &t->read_buffer);
    gpr_free(read_buffer);
  }
  t->notify_on_receive_settings = notify_on_receive_settings;
  GRPC_CLOSURE_SCHED(exec_ctx, &t->read_action_locked, GRPC_ERROR_NONE);
}
