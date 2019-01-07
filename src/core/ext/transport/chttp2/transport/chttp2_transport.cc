/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/context_list.h"
#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/stream_compression.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/timeout_encoding.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_impl.h"
#include "src/core/lib/uri/uri_parser.h"

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
#define DEFAULT_MAX_PINGS_BETWEEN_DATA 2
#define DEFAULT_MAX_PING_STRIKES 2

static int g_default_client_keepalive_time_ms =
    DEFAULT_CLIENT_KEEPALIVE_TIME_MS;
static int g_default_client_keepalive_timeout_ms =
    DEFAULT_CLIENT_KEEPALIVE_TIMEOUT_MS;
static int g_default_server_keepalive_time_ms =
    DEFAULT_SERVER_KEEPALIVE_TIME_MS;
static int g_default_server_keepalive_timeout_ms =
    DEFAULT_SERVER_KEEPALIVE_TIMEOUT_MS;
static bool g_default_client_keepalive_permit_without_calls =
    DEFAULT_KEEPALIVE_PERMIT_WITHOUT_CALLS;
static bool g_default_server_keepalive_permit_without_calls =
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
static void write_action_begin_locked(void* t, grpc_error* error);
static void write_action(void* t, grpc_error* error);
static void write_action_end_locked(void* t, grpc_error* error);

static void read_action_locked(void* t, grpc_error* error);

static void complete_fetch_locked(void* gs, grpc_error* error);
/** Set a transport level setting, and push it to our peer */
static void queue_setting_update(grpc_chttp2_transport* t,
                                 grpc_chttp2_setting_id id, uint32_t value);

static void close_from_api(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                           grpc_error* error);

/** Start new streams that have been created if we can */
static void maybe_start_some_streams(grpc_chttp2_transport* t);

static void connectivity_state_set(grpc_chttp2_transport* t,
                                   grpc_connectivity_state state,
                                   grpc_error* error, const char* reason);

static void benign_reclaimer_locked(void* t, grpc_error* error);
static void destructive_reclaimer_locked(void* t, grpc_error* error);

static void post_benign_reclaimer(grpc_chttp2_transport* t);
static void post_destructive_reclaimer(grpc_chttp2_transport* t);

static void close_transport_locked(grpc_chttp2_transport* t, grpc_error* error);
static void end_all_the_calls(grpc_chttp2_transport* t, grpc_error* error);

static void schedule_bdp_ping_locked(grpc_chttp2_transport* t);
static void start_bdp_ping_locked(void* tp, grpc_error* error);
static void finish_bdp_ping_locked(void* tp, grpc_error* error);
static void next_bdp_ping_timer_expired_locked(void* tp, grpc_error* error);

static void cancel_pings(grpc_chttp2_transport* t, grpc_error* error);
static void send_ping_locked(grpc_chttp2_transport* t,
                             grpc_closure* on_initiate,
                             grpc_closure* on_complete);
static void retry_initiate_ping_locked(void* tp, grpc_error* error);

/** keepalive-relevant functions */
static void init_keepalive_ping_locked(void* arg, grpc_error* error);
static void start_keepalive_ping_locked(void* arg, grpc_error* error);
static void finish_keepalive_ping_locked(void* arg, grpc_error* error);
static void keepalive_watchdog_fired_locked(void* arg, grpc_error* error);

static void reset_byte_stream(void* arg, grpc_error* error);

// Flow control default enabled. Can be disabled by setting
// GRPC_EXPERIMENTAL_DISABLE_FLOW_CONTROL
bool g_flow_control_enabled = true;

/*******************************************************************************
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

grpc_chttp2_transport::~grpc_chttp2_transport() {
  size_t i;

  if (channelz_socket != nullptr) {
    channelz_socket.reset();
  }

  grpc_endpoint_destroy(ep);

  grpc_slice_buffer_destroy_internal(&qbuf);

  grpc_slice_buffer_destroy_internal(&outbuf);
  grpc_chttp2_hpack_compressor_destroy(&hpack_compressor);

  grpc_error* error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed");
  // ContextList::Execute follows semantics of a callback function and does not
  // take a ref on error
  grpc_core::ContextList::Execute(cl, nullptr, error);
  GRPC_ERROR_UNREF(error);
  cl = nullptr;

  grpc_slice_buffer_destroy_internal(&read_buffer);
  grpc_chttp2_hpack_parser_destroy(&hpack_parser);
  grpc_chttp2_goaway_parser_destroy(&goaway_parser);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(lists[i].head == nullptr);
    GPR_ASSERT(lists[i].tail == nullptr);
  }

  GRPC_ERROR_UNREF(goaway_error);

  GPR_ASSERT(grpc_chttp2_stream_map_size(&stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&stream_map);
  grpc_connectivity_state_destroy(&channel_callback.state_tracker);

  GRPC_COMBINER_UNREF(combiner, "chttp2_transport");

  cancel_pings(this,
               GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed"));

  while (write_cb_pool) {
    grpc_chttp2_write_cb* next = write_cb_pool->next;
    gpr_free(write_cb_pool);
    write_cb_pool = next;
  }

  flow_control.Destroy();

  GRPC_ERROR_UNREF(closed_with_error);
  gpr_free(ping_acks);
  gpr_free(peer_string);
}

static const grpc_transport_vtable* get_vtable(void);

/* Returns whether bdp is enabled */
static bool read_channel_args(grpc_chttp2_transport* t,
                              const grpc_channel_args* channel_args,
                              bool is_client) {
  bool enable_bdp = true;
  bool channelz_enabled = GRPC_ENABLE_CHANNELZ_DEFAULT;
  size_t i;
  int j;

  for (i = 0; i < channel_args->num_args; i++) {
    if (0 == strcmp(channel_args->args[i].key,
                    GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER)) {
      const grpc_integer_options options = {-1, 0, INT_MAX};
      const int value =
          grpc_channel_arg_get_integer(&channel_args->args[i], options);
      if (value >= 0) {
        if ((t->next_stream_id & 1) != (value & 1)) {
          gpr_log(GPR_ERROR, "%s: low bit must be %d on %s",
                  GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER, t->next_stream_id & 1,
                  is_client ? "client" : "server");
        } else {
          t->next_stream_id = static_cast<uint32_t>(value);
        }
      }
    } else if (0 == strcmp(channel_args->args[i].key,
                           GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER)) {
      const grpc_integer_options options = {-1, 0, INT_MAX};
      const int value =
          grpc_channel_arg_get_integer(&channel_args->args[i], options);
      if (value >= 0) {
        grpc_chttp2_hpack_compressor_set_max_usable_size(
            &t->hpack_compressor, static_cast<uint32_t>(value));
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
               strcmp(channel_args->args[i].key,
                      GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS)) {
      t->ping_policy.min_sent_ping_interval_without_data =
          grpc_channel_arg_get_integer(
              &channel_args->args[i],
              grpc_integer_options{
                  g_default_min_sent_ping_interval_without_data_ms, 0,
                  INT_MAX});
    } else if (0 ==
               strcmp(channel_args->args[i].key,
                      GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)) {
      t->ping_policy.min_recv_ping_interval_without_data =
          grpc_channel_arg_get_integer(
              &channel_args->args[i],
              grpc_integer_options{
                  g_default_min_recv_ping_interval_without_data_ms, 0,
                  INT_MAX});
    } else if (0 == strcmp(channel_args->args[i].key,
                           GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE)) {
      t->write_buffer_size = static_cast<uint32_t>(grpc_channel_arg_get_integer(
          &channel_args->args[i], {0, 0, MAX_WRITE_BUFFER_SIZE}));
    } else if (0 ==
               strcmp(channel_args->args[i].key, GRPC_ARG_HTTP2_BDP_PROBE)) {
      enable_bdp = grpc_channel_arg_get_bool(&channel_args->args[i], true);
    } else if (0 ==
               strcmp(channel_args->args[i].key, GRPC_ARG_KEEPALIVE_TIME_MS)) {
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
      t->keepalive_timeout = value == INT_MAX ? GRPC_MILLIS_INF_FUTURE : value;
    } else if (0 == strcmp(channel_args->args[i].key,
                           GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)) {
      t->keepalive_permit_without_calls = static_cast<uint32_t>(
          grpc_channel_arg_get_integer(&channel_args->args[i], {0, 0, 1}));
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
    } else if (0 ==
               strcmp(channel_args->args[i].key, GRPC_ARG_ENABLE_CHANNELZ)) {
      channelz_enabled = grpc_channel_arg_get_bool(
          &channel_args->args[i], GRPC_ENABLE_CHANNELZ_DEFAULT);
    } else {
      static const struct {
        const char* channel_arg_name;
        grpc_chttp2_setting_id setting_id;
        grpc_integer_options integer_options;
        bool availability[2] /* server, client */;
      } settings_map[] = {{GRPC_ARG_MAX_CONCURRENT_STREAMS,
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
      for (j = 0; j < static_cast<int> GPR_ARRAY_SIZE(settings_map); j++) {
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
              queue_setting_update(t, settings_map[j].setting_id,
                                   static_cast<uint32_t>(value));
            }
          }
          break;
        }
      }
    }
  }
  if (channelz_enabled) {
    // TODO(ncteisen): add an API to endpoint to query for local addr, and pass
    // it in here, so SocketNode knows its own address.
    t->channelz_socket =
        grpc_core::MakeRefCounted<grpc_core::channelz::SocketNode>(
            grpc_core::UniquePtr<char>(),
            grpc_core::UniquePtr<char>(gpr_strdup(t->peer_string)));
  }
  return enable_bdp;
}

static void init_transport_closures(grpc_chttp2_transport* t) {
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
}

static void init_transport_keepalive_settings(grpc_chttp2_transport* t) {
  if (t->is_client) {
    t->keepalive_time = g_default_client_keepalive_time_ms == INT_MAX
                            ? GRPC_MILLIS_INF_FUTURE
                            : g_default_client_keepalive_time_ms;
    t->keepalive_timeout = g_default_client_keepalive_timeout_ms == INT_MAX
                               ? GRPC_MILLIS_INF_FUTURE
                               : g_default_client_keepalive_timeout_ms;
    t->keepalive_permit_without_calls =
        g_default_client_keepalive_permit_without_calls;
  } else {
    t->keepalive_time = g_default_server_keepalive_time_ms == INT_MAX
                            ? GRPC_MILLIS_INF_FUTURE
                            : g_default_server_keepalive_time_ms;
    t->keepalive_timeout = g_default_server_keepalive_timeout_ms == INT_MAX
                               ? GRPC_MILLIS_INF_FUTURE
                               : g_default_server_keepalive_timeout_ms;
    t->keepalive_permit_without_calls =
        g_default_server_keepalive_permit_without_calls;
  }
}

static void configure_transport_ping_policy(grpc_chttp2_transport* t) {
  t->ping_policy.max_pings_without_data = g_default_max_pings_without_data;
  t->ping_policy.min_sent_ping_interval_without_data =
      g_default_min_sent_ping_interval_without_data_ms;
  t->ping_policy.max_ping_strikes = g_default_max_ping_strikes;
  t->ping_policy.min_recv_ping_interval_without_data =
      g_default_min_recv_ping_interval_without_data_ms;
}

static void init_keepalive_pings_if_enabled(grpc_chttp2_transport* t) {
  if (t->keepalive_time != GRPC_MILLIS_INF_FUTURE) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
    GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
    grpc_timer_init(&t->keepalive_ping_timer,
                    grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                    &t->init_keepalive_ping_locked);
  } else {
    /* Use GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED to indicate there are no
       inflight keeaplive timers */
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED;
  }
}

grpc_chttp2_transport::grpc_chttp2_transport(
    const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
    grpc_resource_user* resource_user)
    : refs(1, &grpc_trace_chttp2_refcount),
      ep(ep),
      peer_string(grpc_endpoint_get_peer(ep)),
      resource_user(resource_user),
      combiner(grpc_combiner_create()),
      is_client(is_client),
      next_stream_id(is_client ? 1 : 2),
      deframe_state(is_client ? GRPC_DTS_FH_0 : GRPC_DTS_CLIENT_PREFIX_0) {
  GPR_ASSERT(strlen(GRPC_CHTTP2_CLIENT_CONNECT_STRING) ==
             GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);
  base.vtable = get_vtable();
  /* 8 is a random stab in the dark as to a good initial size: it's small enough
     that it shouldn't waste memory for infrequently used connections, yet
     large enough that the exponential growth should happen nicely when it's
     needed.
     TODO(ctiller): tune this */
  grpc_chttp2_stream_map_init(&stream_map, 8);

  grpc_slice_buffer_init(&read_buffer);
  grpc_connectivity_state_init(
      &channel_callback.state_tracker, GRPC_CHANNEL_READY,
      is_client ? "client_transport" : "server_transport");
  grpc_slice_buffer_init(&outbuf);
  if (is_client) {
    grpc_slice_buffer_add(&outbuf, grpc_slice_from_copied_string(
                                       GRPC_CHTTP2_CLIENT_CONNECT_STRING));
  }
  grpc_chttp2_hpack_compressor_init(&hpack_compressor);
  grpc_slice_buffer_init(&qbuf);
  /* copy in initial settings to all setting sets */
  size_t i;
  int j;
  for (i = 0; i < GRPC_CHTTP2_NUM_SETTINGS; i++) {
    for (j = 0; j < GRPC_NUM_SETTING_SETS; j++) {
      settings[j][i] = grpc_chttp2_settings_parameters[i].default_value;
    }
  }
  grpc_chttp2_hpack_parser_init(&hpack_parser);
  grpc_chttp2_goaway_parser_init(&goaway_parser);

  init_transport_closures(this);

  /* configure http2 the way we like it */
  if (is_client) {
    queue_setting_update(this, GRPC_CHTTP2_SETTINGS_ENABLE_PUSH, 0);
    queue_setting_update(this, GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 0);
  }
  queue_setting_update(this, GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
                       DEFAULT_MAX_HEADER_LIST_SIZE);
  queue_setting_update(this,
                       GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA, 1);

  configure_transport_ping_policy(this);
  init_transport_keepalive_settings(this);

  bool enable_bdp = true;
  if (channel_args) {
    enable_bdp = read_channel_args(this, channel_args, is_client);
  }

  if (g_flow_control_enabled) {
    flow_control.Init<grpc_core::chttp2::TransportFlowControl>(this,
                                                               enable_bdp);
  } else {
    flow_control.Init<grpc_core::chttp2::TransportFlowControlDisabled>(this);
    enable_bdp = false;
  }

  /* No pings allowed before receiving a header or data frame. */
  ping_state.pings_before_data_required = 0;
  ping_state.is_delayed_ping_timer_set = false;
  ping_state.last_ping_sent_time = GRPC_MILLIS_INF_PAST;

  ping_recv_state.last_ping_recv_time = GRPC_MILLIS_INF_PAST;
  ping_recv_state.ping_strikes = 0;

  init_keepalive_pings_if_enabled(this);

  if (enable_bdp) {
    GRPC_CHTTP2_REF_TRANSPORT(this, "bdp_ping");
    schedule_bdp_ping_locked(this);
    grpc_chttp2_act_on_flowctl_action(flow_control->PeriodicUpdate(), this,
                                      nullptr);
  }

  grpc_chttp2_initiate_write(this, GRPC_CHTTP2_INITIATE_WRITE_INITIAL_WRITE);
  post_benign_reclaimer(this);
}

static void destroy_transport_locked(void* tp, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->destroying = 1;
  close_transport_locked(
      t, grpc_error_set_int(
             GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed"),
             GRPC_ERROR_INT_OCCURRED_DURING_WRITE, t->write_state));
  // Must be the last line.
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "destroy");
}

static void destroy_transport(grpc_transport* gt) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(destroy_transport_locked, t,
                                         grpc_combiner_scheduler(t->combiner)),
                     GRPC_ERROR_NONE);
}

static void close_transport_locked(grpc_chttp2_transport* t,
                                   grpc_error* error) {
  end_all_the_calls(t, GRPC_ERROR_REF(error));
  cancel_pings(t, GRPC_ERROR_REF(error));
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
    connectivity_state_set(t, GRPC_CHANNEL_SHUTDOWN, GRPC_ERROR_REF(error),
                           "close_transport");
    if (t->ping_state.is_delayed_ping_timer_set) {
      grpc_timer_cancel(&t->ping_state.delayed_ping_timer);
    }
    if (t->have_next_bdp_ping_timer) {
      grpc_timer_cancel(&t->next_bdp_ping_timer);
    }
    switch (t->keepalive_state) {
      case GRPC_CHTTP2_KEEPALIVE_STATE_WAITING:
        grpc_timer_cancel(&t->keepalive_ping_timer);
        break;
      case GRPC_CHTTP2_KEEPALIVE_STATE_PINGING:
        grpc_timer_cancel(&t->keepalive_ping_timer);
        grpc_timer_cancel(&t->keepalive_watchdog_timer);
        break;
      case GRPC_CHTTP2_KEEPALIVE_STATE_DYING:
      case GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED:
        /* keepalive timers are not set in these two states */
        break;
    }

    /* flush writable stream list to avoid dangling references */
    grpc_chttp2_stream* s;
    while (grpc_chttp2_list_pop_writable_stream(t, &s)) {
      GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:close");
    }
    GPR_ASSERT(t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE);
    grpc_endpoint_shutdown(t->ep, GRPC_ERROR_REF(error));
  }
  if (t->notify_on_receive_settings != nullptr) {
    GRPC_CLOSURE_SCHED(t->notify_on_receive_settings, GRPC_ERROR_CANCELLED);
    t->notify_on_receive_settings = nullptr;
  }
  GRPC_ERROR_UNREF(error);
}

#ifndef NDEBUG
void grpc_chttp2_stream_ref(grpc_chttp2_stream* s, const char* reason) {
  grpc_stream_ref(s->refcount, reason);
}
void grpc_chttp2_stream_unref(grpc_chttp2_stream* s, const char* reason) {
  grpc_stream_unref(s->refcount, reason);
}
#else
void grpc_chttp2_stream_ref(grpc_chttp2_stream* s) {
  grpc_stream_ref(s->refcount);
}
void grpc_chttp2_stream_unref(grpc_chttp2_stream* s) {
  grpc_stream_unref(s->refcount);
}
#endif

grpc_chttp2_stream::grpc_chttp2_stream(grpc_chttp2_transport* t,
                                       grpc_stream_refcount* refcount,
                                       const void* server_data,
                                       gpr_arena* arena)
    : t(t), refcount(refcount), metadata_buffer{{arena}, {arena}} {
  /* We reserve one 'active stream' that's dropped when the stream is
     read-closed. The others are for Chttp2IncomingByteStreams that are
     actively reading */
  GRPC_CHTTP2_STREAM_REF(this, "chttp2");
  GRPC_CHTTP2_REF_TRANSPORT(t, "stream");

  if (server_data) {
    id = static_cast<uint32_t>((uintptr_t)server_data);
    *t->accepting_stream = this;
    grpc_chttp2_stream_map_add(&t->stream_map, id, this);
    post_destructive_reclaimer(t);
  }
  if (t->flow_control->flow_control_enabled()) {
    flow_control.Init<grpc_core::chttp2::StreamFlowControl>(
        static_cast<grpc_core::chttp2::TransportFlowControl*>(
            t->flow_control.get()),
        this);
  } else {
    flow_control.Init<grpc_core::chttp2::StreamFlowControlDisabled>();
  }

  grpc_slice_buffer_init(&frame_storage);
  grpc_slice_buffer_init(&unprocessed_incoming_frames_buffer);
  grpc_slice_buffer_init(&flow_controlled_buffer);
  grpc_slice_buffer_init(&compressed_data_buffer);
  grpc_slice_buffer_init(&decompressed_data_buffer);

  GRPC_CLOSURE_INIT(&complete_fetch_locked, ::complete_fetch_locked, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&reset_byte_stream, ::reset_byte_stream, this,
                    grpc_combiner_scheduler(t->combiner));
}

grpc_chttp2_stream::~grpc_chttp2_stream() {
  if (t->channelz_socket != nullptr) {
    if ((t->is_client && eos_received) || (!t->is_client && eos_sent)) {
      t->channelz_socket->RecordStreamSucceeded();
    } else {
      t->channelz_socket->RecordStreamFailed();
    }
  }

  GPR_ASSERT((write_closed && read_closed) || id == 0);
  if (id != 0) {
    GPR_ASSERT(grpc_chttp2_stream_map_find(&t->stream_map, id) == nullptr);
  }

  grpc_slice_buffer_destroy_internal(&unprocessed_incoming_frames_buffer);
  grpc_slice_buffer_destroy_internal(&frame_storage);
  grpc_slice_buffer_destroy_internal(&compressed_data_buffer);
  grpc_slice_buffer_destroy_internal(&decompressed_data_buffer);

  grpc_chttp2_list_remove_stalled_by_transport(t, this);
  grpc_chttp2_list_remove_stalled_by_stream(t, this);

  for (int i = 0; i < STREAM_LIST_COUNT; i++) {
    if (GPR_UNLIKELY(included[i])) {
      gpr_log(GPR_ERROR, "%s stream %d still included in list %d",
              t->is_client ? "client" : "server", id, i);
      abort();
    }
  }

  GPR_ASSERT(send_initial_metadata_finished == nullptr);
  GPR_ASSERT(fetching_send_message == nullptr);
  GPR_ASSERT(send_trailing_metadata_finished == nullptr);
  GPR_ASSERT(recv_initial_metadata_ready == nullptr);
  GPR_ASSERT(recv_message_ready == nullptr);
  GPR_ASSERT(recv_trailing_metadata_finished == nullptr);
  grpc_slice_buffer_destroy_internal(&flow_controlled_buffer);
  GRPC_ERROR_UNREF(read_closed_error);
  GRPC_ERROR_UNREF(write_closed_error);
  GRPC_ERROR_UNREF(byte_stream_error);

  flow_control.Destroy();

  if (t->resource_user != nullptr) {
    grpc_resource_user_free(t->resource_user, GRPC_RESOURCE_QUOTA_CALL_SIZE);
  }

  GRPC_CHTTP2_UNREF_TRANSPORT(t, "stream");
  GRPC_CLOSURE_SCHED(destroy_stream_arg, GRPC_ERROR_NONE);
}

static int init_stream(grpc_transport* gt, grpc_stream* gs,
                       grpc_stream_refcount* refcount, const void* server_data,
                       gpr_arena* arena) {
  GPR_TIMER_SCOPE("init_stream", 0);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  new (gs) grpc_chttp2_stream(t, refcount, server_data, arena);
  return 0;
}

static void destroy_stream_locked(void* sp, grpc_error* error) {
  GPR_TIMER_SCOPE("destroy_stream", 0);
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(sp);
  s->~grpc_chttp2_stream();
}

static void destroy_stream(grpc_transport* gt, grpc_stream* gs,
                           grpc_closure* then_schedule_closure) {
  GPR_TIMER_SCOPE("destroy_stream", 0);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_chttp2_stream* s = reinterpret_cast<grpc_chttp2_stream*>(gs);

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
      GRPC_CLOSURE_INIT(&s->destroy_stream, destroy_stream_locked, s,
                        grpc_combiner_scheduler(t->combiner)),
      GRPC_ERROR_NONE);
}

grpc_chttp2_stream* grpc_chttp2_parsing_lookup_stream(grpc_chttp2_transport* t,
                                                      uint32_t id) {
  return static_cast<grpc_chttp2_stream*>(
      grpc_chttp2_stream_map_find(&t->stream_map, id));
}

grpc_chttp2_stream* grpc_chttp2_parsing_accept_stream(grpc_chttp2_transport* t,
                                                      uint32_t id) {
  if (t->channel_callback.accept_stream == nullptr) {
    return nullptr;
  }
  // Don't accept the stream if memory quota doesn't allow. Note that we should
  // simply refuse the stream here instead of canceling the stream after it's
  // accepted since the latter will create the call which costs much memory.
  if (t->resource_user != nullptr &&
      !grpc_resource_user_safe_alloc(t->resource_user,
                                     GRPC_RESOURCE_QUOTA_CALL_SIZE)) {
    gpr_log(GPR_ERROR, "Memory exhausted, rejecting the stream.");
    grpc_slice_buffer_add(
        &t->qbuf,
        grpc_chttp2_rst_stream_create(
            id, static_cast<uint32_t>(GRPC_HTTP2_REFUSED_STREAM), nullptr));
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
    return nullptr;
  }
  grpc_chttp2_stream* accepting = nullptr;
  GPR_ASSERT(t->accepting_stream == nullptr);
  t->accepting_stream = &accepting;
  t->channel_callback.accept_stream(t->channel_callback.accept_stream_user_data,
                                    &t->base,
                                    (void*)static_cast<uintptr_t>(id));
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

static void set_write_state(grpc_chttp2_transport* t,
                            grpc_chttp2_write_state st, const char* reason) {
  GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "W:%p %s state %s -> %s [%s]", t,
                                 t->is_client ? "CLIENT" : "SERVER",
                                 write_state_name(t->write_state),
                                 write_state_name(st), reason));
  t->write_state = st;
  /* If the state is being reset back to idle, it means a write was just
   * finished. Make sure all the run_after_write closures are scheduled.
   *
   * This is also our chance to close the transport if the transport was marked
   * to be closed after all writes finish (for example, if we received a go-away
   * from peer while we had some pending writes) */
  if (st == GRPC_CHTTP2_WRITE_STATE_IDLE) {
    GRPC_CLOSURE_LIST_SCHED(&t->run_after_write);
    if (t->close_transport_on_writes_finished != nullptr) {
      grpc_error* err = t->close_transport_on_writes_finished;
      t->close_transport_on_writes_finished = nullptr;
      close_transport_locked(t, err);
    }
  }
}

static void inc_initiate_write_reason(
    grpc_chttp2_initiate_write_reason reason) {
  switch (reason) {
    case GRPC_CHTTP2_INITIATE_WRITE_INITIAL_WRITE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_INITIAL_WRITE();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_START_NEW_STREAM:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_START_NEW_STREAM();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_MESSAGE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_MESSAGE();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_INITIAL_METADATA:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_INITIAL_METADATA();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_TRAILING_METADATA:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_TRAILING_METADATA();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_RETRY_SEND_PING();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_CONTINUE_PINGS:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_CONTINUE_PINGS();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_GOAWAY_SENT();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_RST_STREAM();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_CLOSE_FROM_API:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_CLOSE_FROM_API();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_STREAM_FLOW_CONTROL:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_STREAM_FLOW_CONTROL();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_TRANSPORT_FLOW_CONTROL();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SEND_SETTINGS();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_SETTING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_FLOW_CONTROL_UNSTALLED_BY_SETTING();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_UPDATE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_FLOW_CONTROL_UNSTALLED_BY_UPDATE();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_APPLICATION_PING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_APPLICATION_PING();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_KEEPALIVE_PING();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL_UNSTALLED:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_TRANSPORT_FLOW_CONTROL_UNSTALLED();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_PING_RESPONSE:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_PING_RESPONSE();
      break;
    case GRPC_CHTTP2_INITIATE_WRITE_FORCE_RST_STREAM:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_FORCE_RST_STREAM();
      break;
  }
}

void grpc_chttp2_initiate_write(grpc_chttp2_transport* t,
                                grpc_chttp2_initiate_write_reason reason) {
  GPR_TIMER_SCOPE("grpc_chttp2_initiate_write", 0);

  switch (t->write_state) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      inc_initiate_write_reason(reason);
      set_write_state(t, GRPC_CHTTP2_WRITE_STATE_WRITING,
                      grpc_chttp2_initiate_write_reason_string(reason));
      t->is_first_write_in_batch = true;
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      /* Note that the 'write_action_begin_locked' closure is being scheduled
       * on the 'finally_scheduler' of t->combiner. This means that
       * 'write_action_begin_locked' is called only *after* all the other
       * closures (some of which are potentially initiating more writes on the
       * transport) are executed on the t->combiner.
       *
       * The reason for scheduling on finally_scheduler is to make sure we batch
       * as many writes as possible. 'write_action_begin_locked' is the function
       * that gathers all the relevant bytes (which are at various places in the
       * grpc_chttp2_transport structure) and append them to 'outbuf' field in
       * grpc_chttp2_transport thereby batching what would have been potentially
       * multiple write operations.
       *
       * Also, 'write_action_begin_locked' only gathers the bytes into outbuf.
       * It does not call the endpoint to write the bytes. That is done by the
       * 'write_action' (which is scheduled by 'write_action_begin_locked') */
      GRPC_CLOSURE_SCHED(
          GRPC_CLOSURE_INIT(&t->write_action_begin_locked,
                            write_action_begin_locked, t,
                            grpc_combiner_finally_scheduler(t->combiner)),
          GRPC_ERROR_NONE);
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      set_write_state(t, GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE,
                      grpc_chttp2_initiate_write_reason_string(reason));
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
      break;
  }
}

void grpc_chttp2_mark_stream_writable(grpc_chttp2_transport* t,
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

static void write_action_begin_locked(void* gt, grpc_error* error_ignored) {
  GPR_TIMER_SCOPE("write_action_begin_locked", 0);
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(gt);
  GPR_ASSERT(t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE);
  grpc_chttp2_begin_write_result r;
  if (t->closed_with_error != GRPC_ERROR_NONE) {
    r.writing = false;
  } else {
    r = grpc_chttp2_begin_write(t);
  }
  if (r.writing) {
    if (r.partial) {
      GRPC_STATS_INC_HTTP2_PARTIAL_WRITES();
    }
    if (!t->is_first_write_in_batch) {
      GRPC_STATS_INC_HTTP2_WRITES_CONTINUED();
    }
    grpc_closure_scheduler* scheduler =
        write_scheduler(t, r.early_results_scheduled, r.partial);
    if (scheduler != grpc_schedule_on_exec_ctx) {
      GRPC_STATS_INC_HTTP2_WRITES_OFFLOADED();
    }
    set_write_state(
        t,
        r.partial ? GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE
                  : GRPC_CHTTP2_WRITE_STATE_WRITING,
        begin_writing_desc(r.partial, scheduler == grpc_schedule_on_exec_ctx));
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_INIT(&t->write_action, write_action, t, scheduler),
        GRPC_ERROR_NONE);
  } else {
    GRPC_STATS_INC_HTTP2_SPURIOUS_WRITES_BEGUN();
    set_write_state(t, GRPC_CHTTP2_WRITE_STATE_IDLE, "begin writing nothing");
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "writing");
  }
}

static void write_action(void* gt, grpc_error* error) {
  GPR_TIMER_SCOPE("write_action", 0);
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(gt);
  void* cl = t->cl;
  t->cl = nullptr;
  grpc_endpoint_write(
      t->ep, &t->outbuf,
      GRPC_CLOSURE_INIT(&t->write_action_end_locked, write_action_end_locked, t,
                        grpc_combiner_scheduler(t->combiner)),
      cl);
}

/* Callback from the grpc_endpoint after bytes have been written by calling
 * sendmsg */
static void write_action_end_locked(void* tp, grpc_error* error) {
  GPR_TIMER_SCOPE("terminate_writing_with_lock", 0);
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);

  if (error != GRPC_ERROR_NONE) {
    close_transport_locked(t, GRPC_ERROR_REF(error));
  }

  if (t->sent_goaway_state == GRPC_CHTTP2_GOAWAY_SEND_SCHEDULED) {
    t->sent_goaway_state = GRPC_CHTTP2_GOAWAY_SENT;
    if (grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
      close_transport_locked(
          t, GRPC_ERROR_CREATE_FROM_STATIC_STRING("goaway sent"));
    }
  }

  switch (t->write_state) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      GPR_UNREACHABLE_CODE(break);
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      GPR_TIMER_MARK("state=writing", 0);
      set_write_state(t, GRPC_CHTTP2_WRITE_STATE_IDLE, "finish writing");
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
      GPR_TIMER_MARK("state=writing_stale_no_poller", 0);
      set_write_state(t, GRPC_CHTTP2_WRITE_STATE_WRITING, "continue writing");
      t->is_first_write_in_batch = false;
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      GRPC_CLOSURE_RUN(
          GRPC_CLOSURE_INIT(&t->write_action_begin_locked,
                            write_action_begin_locked, t,
                            grpc_combiner_finally_scheduler(t->combiner)),
          GRPC_ERROR_NONE);
      break;
  }

  grpc_chttp2_end_write(t, GRPC_ERROR_REF(error));

  GRPC_CHTTP2_UNREF_TRANSPORT(t, "writing");
}

// Dirties an HTTP2 setting to be sent out next time a writing path occurs.
// If the change needs to occur immediately, manually initiate a write.
static void queue_setting_update(grpc_chttp2_transport* t,
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

void grpc_chttp2_add_incoming_goaway(grpc_chttp2_transport* t,
                                     uint32_t goaway_error,
                                     grpc_slice goaway_text) {
  // GRPC_CHTTP2_IF_TRACING(
  //     gpr_log(GPR_INFO, "got goaway [%d]: %s", goaway_error, msg));

  // Discard the error from a previous goaway frame (if any)
  if (t->goaway_error != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(t->goaway_error);
  }
  t->goaway_error = grpc_error_set_str(
      grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("GOAWAY received"),
          GRPC_ERROR_INT_HTTP2_ERROR, static_cast<intptr_t>(goaway_error)),
      GRPC_ERROR_STR_RAW_BYTES, goaway_text);

  /* When a client receives a GOAWAY with error code ENHANCE_YOUR_CALM and debug
   * data equal to "too_many_pings", it should log the occurrence at a log level
   * that is enabled by default and double the configured KEEPALIVE_TIME used
   * for new connections on that channel. */
  if (GPR_UNLIKELY(t->is_client &&
                   goaway_error == GRPC_HTTP2_ENHANCE_YOUR_CALM &&
                   grpc_slice_str_cmp(goaway_text, "too_many_pings") == 0)) {
    gpr_log(GPR_ERROR,
            "Received a GOAWAY with error code ENHANCE_YOUR_CALM and debug "
            "data equal to \"too_many_pings\"");
    double current_keepalive_time_ms = static_cast<double>(t->keepalive_time);
    t->keepalive_time =
        current_keepalive_time_ms > INT_MAX / KEEPALIVE_TIME_BACKOFF_MULTIPLIER
            ? GRPC_MILLIS_INF_FUTURE
            : static_cast<grpc_millis>(current_keepalive_time_ms *
                                       KEEPALIVE_TIME_BACKOFF_MULTIPLIER);
  }

  /* lie: use transient failure from the transport to indicate goaway has been
   * received */
  connectivity_state_set(t, GRPC_CHANNEL_TRANSIENT_FAILURE,
                         GRPC_ERROR_REF(t->goaway_error), "got_goaway");
}

static void maybe_start_some_streams(grpc_chttp2_transport* t) {
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
        GPR_INFO, "HTTP:%s: Allocating new grpc_chttp2_stream %p to id %d",
        t->is_client ? "CLI" : "SVR", s, t->next_stream_id));

    GPR_ASSERT(s->id == 0);
    s->id = t->next_stream_id;
    t->next_stream_id += 2;

    if (t->next_stream_id >= MAX_CLIENT_STREAM_ID) {
      connectivity_state_set(
          t, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Stream IDs exhausted"),
          "no_more_stream_ids");
    }

    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
    post_destructive_reclaimer(t);
    grpc_chttp2_mark_stream_writable(t, s);
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_START_NEW_STREAM);
  }
  /* cancel out streams that will never be started */
  while (t->next_stream_id >= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_list_pop_waiting_for_concurrency(t, &s)) {
    grpc_chttp2_cancel_stream(
        t, s,
        grpc_error_set_int(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Stream IDs exhausted"),
            GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
  }
}

/* Flag that this closure barrier may be covering a write in a pollset, and so
   we should not complete this closure until we can prove that the write got
   scheduled */
#define CLOSURE_BARRIER_MAY_COVER_WRITE (1 << 0)
/* First bit of the reference count, stored in the high order bits (with the low
   bits being used for flags defined above) */
#define CLOSURE_BARRIER_FIRST_REF_BIT (1 << 16)

static grpc_closure* add_closure_barrier(grpc_closure* closure) {
  closure->next_data.scratch += CLOSURE_BARRIER_FIRST_REF_BIT;
  return closure;
}

static void null_then_run_closure(grpc_closure** closure, grpc_error* error) {
  grpc_closure* c = *closure;
  *closure = nullptr;
  GRPC_CLOSURE_RUN(c, error);
}

void grpc_chttp2_complete_closure_step(grpc_chttp2_transport* t,
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
        GPR_INFO,
        "complete_closure_step: t=%p %p refs=%d flags=0x%04x desc=%s err=%s "
        "write_state=%s",
        t, closure,
        static_cast<int>(closure->next_data.scratch /
                         CLOSURE_BARRIER_FIRST_REF_BIT),
        static_cast<int>(closure->next_data.scratch %
                         CLOSURE_BARRIER_FIRST_REF_BIT),
        desc, errstr, write_state_name(t->write_state));
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
    if ((t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE) ||
        !(closure->next_data.scratch & CLOSURE_BARRIER_MAY_COVER_WRITE)) {
      GRPC_CLOSURE_RUN(closure, closure->error_data.error);
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

static void maybe_become_writable_due_to_send_msg(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s) {
  if (s->id != 0 && (!s->write_buffering ||
                     s->flow_controlled_buffer.length > t->write_buffer_size)) {
    grpc_chttp2_mark_stream_writable(t, s);
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_SEND_MESSAGE);
  }
}

static void add_fetched_slice_locked(grpc_chttp2_transport* t,
                                     grpc_chttp2_stream* s) {
  s->fetched_send_message_length +=
      static_cast<uint32_t> GRPC_SLICE_LENGTH(s->fetching_slice);
  grpc_slice_buffer_add(&s->flow_controlled_buffer, s->fetching_slice);
  maybe_become_writable_due_to_send_msg(t, s);
}

static void continue_fetching_send_locked(grpc_chttp2_transport* t,
                                          grpc_chttp2_stream* s) {
  for (;;) {
    if (s->fetching_send_message == nullptr) {
      /* Stream was cancelled before message fetch completed */
      abort(); /* TODO(ctiller): what cleanup here? */
      return;  /* early out */
    }
    if (s->fetched_send_message_length == s->fetching_send_message->length()) {
      int64_t notify_offset = s->next_message_end_offset;
      if (notify_offset <= s->flow_controlled_bytes_written) {
        grpc_chttp2_complete_closure_step(
            t, s, &s->fetching_send_message_finished, GRPC_ERROR_NONE,
            "fetching_send_message_finished");
      } else {
        grpc_chttp2_write_cb* cb = t->write_cb_pool;
        if (cb == nullptr) {
          cb = static_cast<grpc_chttp2_write_cb*>(gpr_malloc(sizeof(*cb)));
        } else {
          t->write_cb_pool = cb->next;
        }
        cb->call_at_byte = notify_offset;
        cb->closure = s->fetching_send_message_finished;
        s->fetching_send_message_finished = nullptr;
        grpc_chttp2_write_cb** list =
            s->fetching_send_message->flags() & GRPC_WRITE_THROUGH
                ? &s->on_write_finished_cbs
                : &s->on_flow_controlled_cbs;
        cb->next = *list;
        *list = cb;
      }
      s->fetching_send_message.reset();
      return; /* early out */
    } else if (s->fetching_send_message->Next(UINT32_MAX,
                                              &s->complete_fetch_locked)) {
      grpc_error* error = s->fetching_send_message->Pull(&s->fetching_slice);
      if (error != GRPC_ERROR_NONE) {
        s->fetching_send_message.reset();
        grpc_chttp2_cancel_stream(t, s, error);
      } else {
        add_fetched_slice_locked(t, s);
      }
    }
  }
}

static void complete_fetch_locked(void* gs, grpc_error* error) {
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(gs);
  grpc_chttp2_transport* t = s->t;
  if (error == GRPC_ERROR_NONE) {
    error = s->fetching_send_message->Pull(&s->fetching_slice);
    if (error == GRPC_ERROR_NONE) {
      add_fetched_slice_locked(t, s);
      continue_fetching_send_locked(t, s);
    }
  }
  if (error != GRPC_ERROR_NONE) {
    s->fetching_send_message.reset();
    grpc_chttp2_cancel_stream(t, s, error);
  }
}

static void do_nothing(void* arg, grpc_error* error) {}

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

static void perform_stream_op_locked(void* stream_op,
                                     grpc_error* error_ignored) {
  GPR_TIMER_SCOPE("perform_stream_op_locked", 0);

  grpc_transport_stream_op_batch* op =
      static_cast<grpc_transport_stream_op_batch*>(stream_op);
  grpc_chttp2_stream* s =
      static_cast<grpc_chttp2_stream*>(op->handler_private.extra_arg);
  grpc_transport_stream_op_batch_payload* op_payload = op->payload;
  grpc_chttp2_transport* t = s->t;

  GRPC_STATS_INC_HTTP2_OP_BATCHES();

  s->context = op->payload->context;
  s->traced = op->is_traced;
  if (grpc_http_trace.enabled()) {
    char* str = grpc_transport_stream_op_batch_string(op);
    gpr_log(GPR_INFO, "perform_stream_op_locked: %s; on_complete = %p", str,
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
  // TODO(roth): This is a hack needed because we use data inside of the
  // closure itself to do the barrier calculation (i.e., to ensure that
  // we don't schedule the closure until all ops in the batch have been
  // completed).  This can go away once we move to a new C++ closure API
  // that provides the ability to create a barrier closure.
  if (on_complete == nullptr) {
    on_complete = GRPC_CLOSURE_INIT(&op->handler_private.closure, do_nothing,
                                    nullptr, grpc_schedule_on_exec_ctx);
  }

  /* use final_data as a barrier until enqueue time; the inital counter is
     dropped at the end of this function */
  on_complete->next_data.scratch = CLOSURE_BARRIER_FIRST_REF_BIT;
  on_complete->error_data.error = GRPC_ERROR_NONE;

  if (op->cancel_stream) {
    GRPC_STATS_INC_HTTP2_OP_CANCEL();
    grpc_chttp2_cancel_stream(t, s, op_payload->cancel_stream.cancel_error);
  }

  if (op->send_initial_metadata) {
    if (t->is_client && t->channelz_socket != nullptr) {
      t->channelz_socket->RecordStreamStartedFromLocal();
    }
    GRPC_STATS_INC_HTTP2_OP_SEND_INITIAL_METADATA();
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
          t, s,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                         "to-be-sent initial metadata size "
                                         "exceeds peer limit"),
                                     GRPC_ERROR_INT_SIZE,
                                     static_cast<intptr_t>(metadata_size)),
                  GRPC_ERROR_INT_LIMIT,
                  static_cast<intptr_t>(metadata_peer_limit)),
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
            maybe_start_some_streams(t);
          } else {
            grpc_chttp2_cancel_stream(
                t, s,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                        "Transport closed", &t->closed_with_error, 1),
                    GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
          }
        } else {
          GPR_ASSERT(s->id != 0);
          grpc_chttp2_mark_stream_writable(t, s);
          if (!(op->send_message &&
                (op->payload->send_message.send_message->flags() &
                 GRPC_WRITE_BUFFER_HINT))) {
            grpc_chttp2_initiate_write(
                t, GRPC_CHTTP2_INITIATE_WRITE_SEND_INITIAL_METADATA);
          }
        }
      } else {
        s->send_initial_metadata = nullptr;
        grpc_chttp2_complete_closure_step(
            t, s, &s->send_initial_metadata_finished,
            GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                "Attempt to send initial metadata after stream was closed",
                &s->write_closed_error, 1),
            "send_initial_metadata_finished");
      }
    }
    if (op_payload->send_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op_payload->send_initial_metadata.peer_string,
                        (gpr_atm)t->peer_string);
    }
  }

  if (op->send_message) {
    GRPC_STATS_INC_HTTP2_OP_SEND_MESSAGE();
    t->num_messages_in_next_write++;
    GRPC_STATS_INC_HTTP2_SEND_MESSAGE_SIZE(
        op->payload->send_message.send_message->length());
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->fetching_send_message_finished = add_closure_barrier(op->on_complete);
    if (s->write_closed) {
      // Return an error unless the client has already received trailing
      // metadata from the server, since an application using a
      // streaming call might send another message before getting a
      // recv_message failure, breaking out of its loop, and then
      // starting recv_trailing_metadata.
      op->payload->send_message.send_message.reset();
      grpc_chttp2_complete_closure_step(
          t, s, &s->fetching_send_message_finished,
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
      uint32_t flags = op_payload->send_message.send_message->flags();
      frame_hdr[0] = (flags & GRPC_WRITE_INTERNAL_COMPRESS) != 0;
      size_t len = op_payload->send_message.send_message->length();
      frame_hdr[1] = static_cast<uint8_t>(len >> 24);
      frame_hdr[2] = static_cast<uint8_t>(len >> 16);
      frame_hdr[3] = static_cast<uint8_t>(len >> 8);
      frame_hdr[4] = static_cast<uint8_t>(len);
      s->fetching_send_message =
          std::move(op_payload->send_message.send_message);
      s->fetched_send_message_length = 0;
      s->next_message_end_offset =
          s->flow_controlled_bytes_written +
          static_cast<int64_t>(s->flow_controlled_buffer.length) +
          static_cast<int64_t>(len);
      if (flags & GRPC_WRITE_BUFFER_HINT) {
        s->next_message_end_offset -= t->write_buffer_size;
        s->write_buffering = true;
      } else {
        s->write_buffering = false;
      }
      continue_fetching_send_locked(t, s);
      maybe_become_writable_due_to_send_msg(t, s);
    }
  }

  if (op->send_trailing_metadata) {
    GRPC_STATS_INC_HTTP2_OP_SEND_TRAILING_METADATA();
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
          t, s,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                         "to-be-sent trailing metadata size "
                                         "exceeds peer limit"),
                                     GRPC_ERROR_INT_SIZE,
                                     static_cast<intptr_t>(metadata_size)),
                  GRPC_ERROR_INT_LIMIT,
                  static_cast<intptr_t>(metadata_peer_limit)),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
    } else {
      if (contains_non_ok_status(s->send_trailing_metadata)) {
        s->seen_error = true;
      }
      if (s->write_closed) {
        s->send_trailing_metadata = nullptr;
        grpc_chttp2_complete_closure_step(
            t, s, &s->send_trailing_metadata_finished,
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
        grpc_chttp2_mark_stream_writable(t, s);
        grpc_chttp2_initiate_write(
            t, GRPC_CHTTP2_INITIATE_WRITE_SEND_TRAILING_METADATA);
      }
    }
  }

  if (op->recv_initial_metadata) {
    GRPC_STATS_INC_HTTP2_OP_RECV_INITIAL_METADATA();
    GPR_ASSERT(s->recv_initial_metadata_ready == nullptr);
    s->recv_initial_metadata_ready =
        op_payload->recv_initial_metadata.recv_initial_metadata_ready;
    s->recv_initial_metadata =
        op_payload->recv_initial_metadata.recv_initial_metadata;
    s->trailing_metadata_available =
        op_payload->recv_initial_metadata.trailing_metadata_available;
    if (op_payload->recv_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op_payload->recv_initial_metadata.peer_string,
                        (gpr_atm)t->peer_string);
    }
    grpc_chttp2_maybe_complete_recv_initial_metadata(t, s);
  }

  if (op->recv_message) {
    GRPC_STATS_INC_HTTP2_OP_RECV_MESSAGE();
    size_t before = 0;
    GPR_ASSERT(s->recv_message_ready == nullptr);
    GPR_ASSERT(!s->pending_byte_stream);
    s->recv_message_ready = op_payload->recv_message.recv_message_ready;
    s->recv_message = op_payload->recv_message.recv_message;
    if (s->id != 0) {
      if (!s->read_closed) {
        before = s->frame_storage.length +
                 s->unprocessed_incoming_frames_buffer.length;
      }
    }
    grpc_chttp2_maybe_complete_recv_message(t, s);
    if (s->id != 0) {
      if (!s->read_closed && s->frame_storage.length == 0) {
        size_t after = s->frame_storage.length +
                       s->unprocessed_incoming_frames_buffer_cached_length;
        s->flow_control->IncomingByteStreamUpdate(GRPC_HEADER_SIZE_IN_BYTES,
                                                  before - after);
        grpc_chttp2_act_on_flowctl_action(s->flow_control->MakeAction(), t, s);
      }
    }
  }

  if (op->recv_trailing_metadata) {
    GRPC_STATS_INC_HTTP2_OP_RECV_TRAILING_METADATA();
    GPR_ASSERT(s->collecting_stats == nullptr);
    s->collecting_stats = op_payload->recv_trailing_metadata.collect_stats;
    GPR_ASSERT(s->recv_trailing_metadata_finished == nullptr);
    s->recv_trailing_metadata_finished =
        op_payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    s->recv_trailing_metadata =
        op_payload->recv_trailing_metadata.recv_trailing_metadata;
    s->final_metadata_requested = true;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
  }

  grpc_chttp2_complete_closure_step(t, s, &on_complete, GRPC_ERROR_NONE,
                                    "op->on_complete");

  GRPC_CHTTP2_STREAM_UNREF(s, "perform_stream_op");
}

static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
  GPR_TIMER_SCOPE("perform_stream_op", 0);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_chttp2_stream* s = reinterpret_cast<grpc_chttp2_stream*>(gs);

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
    gpr_log(GPR_INFO, "perform_stream_op[s=%p]: %s", s, str);
    gpr_free(str);
  }

  GRPC_CHTTP2_STREAM_REF(s, "perform_stream_op");
  op->handler_private.extra_arg = gs;
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_INIT(&op->handler_private.closure, perform_stream_op_locked,
                        op, grpc_combiner_scheduler(t->combiner)),
      GRPC_ERROR_NONE);
}

static void cancel_pings(grpc_chttp2_transport* t, grpc_error* error) {
  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  for (size_t j = 0; j < GRPC_CHTTP2_PCL_COUNT; j++) {
    grpc_closure_list_fail_all(&pq->lists[j], GRPC_ERROR_REF(error));
    GRPC_CLOSURE_LIST_SCHED(&pq->lists[j]);
  }
  GRPC_ERROR_UNREF(error);
}

static void send_ping_locked(grpc_chttp2_transport* t,
                             grpc_closure* on_initiate, grpc_closure* on_ack) {
  if (t->closed_with_error != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_SCHED(on_initiate, GRPC_ERROR_REF(t->closed_with_error));
    GRPC_CLOSURE_SCHED(on_ack, GRPC_ERROR_REF(t->closed_with_error));
    return;
  }
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_INITIATE], on_initiate,
                           GRPC_ERROR_NONE);
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_NEXT], on_ack,
                           GRPC_ERROR_NONE);
}

/*
 * Specialized form of send_ping_locked for keepalive ping. If there is already
 * a ping in progress, the keepalive ping would piggyback onto that ping,
 * instead of waiting for that ping to complete and then starting a new ping.
 */
static void send_keepalive_ping_locked(grpc_chttp2_transport* t) {
  if (t->closed_with_error != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_RUN(&t->start_keepalive_ping_locked,
                     GRPC_ERROR_REF(t->closed_with_error));
    GRPC_CLOSURE_RUN(&t->finish_keepalive_ping_locked,
                     GRPC_ERROR_REF(t->closed_with_error));
    return;
  }
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_INFLIGHT])) {
    /* There is a ping in flight. Add yourself to the inflight closure list. */
    GRPC_CLOSURE_RUN(&t->start_keepalive_ping_locked, GRPC_ERROR_NONE);
    grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_INFLIGHT],
                             &t->finish_keepalive_ping_locked, GRPC_ERROR_NONE);
    return;
  }
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_INITIATE],
                           &t->start_keepalive_ping_locked, GRPC_ERROR_NONE);
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_NEXT],
                           &t->finish_keepalive_ping_locked, GRPC_ERROR_NONE);
}

static void retry_initiate_ping_locked(void* tp, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->ping_state.is_delayed_ping_timer_set = false;
  if (error == GRPC_ERROR_NONE) {
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING);
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "retry_initiate_ping_locked");
}

void grpc_chttp2_ack_ping(grpc_chttp2_transport* t, uint64_t id) {
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (pq->inflight_id != id) {
    char* from = grpc_endpoint_get_peer(t->ep);
    gpr_log(GPR_DEBUG, "Unknown ping response from %s: %" PRIx64, from, id);
    gpr_free(from);
    return;
  }
  GRPC_CLOSURE_LIST_SCHED(&pq->lists[GRPC_CHTTP2_PCL_INFLIGHT]);
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_NEXT])) {
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_CONTINUE_PINGS);
  }
}

static void send_goaway(grpc_chttp2_transport* t, grpc_error* error) {
  t->sent_goaway_state = GRPC_CHTTP2_GOAWAY_SEND_SCHEDULED;
  grpc_http2_error_code http_error;
  grpc_slice slice;
  grpc_error_get_status(error, GRPC_MILLIS_INF_FUTURE, nullptr, &slice,
                        &http_error, nullptr);
  grpc_chttp2_goaway_append(t->last_new_stream_id,
                            static_cast<uint32_t>(http_error),
                            grpc_slice_ref_internal(slice), &t->qbuf);
  grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
  GRPC_ERROR_UNREF(error);
}

void grpc_chttp2_add_ping_strike(grpc_chttp2_transport* t) {
  if (++t->ping_recv_state.ping_strikes > t->ping_policy.max_ping_strikes &&
      t->ping_policy.max_ping_strikes != 0) {
    send_goaway(t,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("too_many_pings"),
                    GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_ENHANCE_YOUR_CALM));
    /*The transport will be closed after the write is done */
    close_transport_locked(
        t, grpc_error_set_int(
               GRPC_ERROR_CREATE_FROM_STATIC_STRING("Too many pings"),
               GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
  }
}

static void perform_transport_op_locked(void* stream_op,
                                        grpc_error* error_ignored) {
  grpc_transport_op* op = static_cast<grpc_transport_op*>(stream_op);
  grpc_chttp2_transport* t =
      static_cast<grpc_chttp2_transport*>(op->handler_private.extra_arg);

  if (op->goaway_error) {
    send_goaway(t, op->goaway_error);
  }

  if (op->set_accept_stream) {
    t->channel_callback.accept_stream = op->set_accept_stream_fn;
    t->channel_callback.accept_stream_user_data =
        op->set_accept_stream_user_data;
  }

  if (op->bind_pollset) {
    grpc_endpoint_add_to_pollset(t->ep, op->bind_pollset);
  }

  if (op->bind_pollset_set) {
    grpc_endpoint_add_to_pollset_set(t->ep, op->bind_pollset_set);
  }

  if (op->send_ping.on_initiate != nullptr || op->send_ping.on_ack != nullptr) {
    send_ping_locked(t, op->send_ping.on_initiate, op->send_ping.on_ack);
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_APPLICATION_PING);
  }

  if (op->on_connectivity_state_change != nullptr) {
    grpc_connectivity_state_notify_on_state_change(
        &t->channel_callback.state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
  }

  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    close_transport_locked(t, op->disconnect_with_error);
  }

  GRPC_CLOSURE_RUN(op->on_consumed, GRPC_ERROR_NONE);

  GRPC_CHTTP2_UNREF_TRANSPORT(t, "transport_op");
}

static void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  if (grpc_http_trace.enabled()) {
    char* msg = grpc_transport_op_string(op);
    gpr_log(GPR_INFO, "perform_transport_op[t=%p]: %s", t, msg);
    gpr_free(msg);
  }
  op->handler_private.extra_arg = gt;
  GRPC_CHTTP2_REF_TRANSPORT(t, "transport_op");
  GRPC_CLOSURE_SCHED(GRPC_CLOSURE_INIT(&op->handler_private.closure,
                                       perform_transport_op_locked, op,
                                       grpc_combiner_scheduler(t->combiner)),
                     GRPC_ERROR_NONE);
}

/*******************************************************************************
 * INPUT PROCESSING - GENERAL
 */

void grpc_chttp2_maybe_complete_recv_initial_metadata(grpc_chttp2_transport* t,
                                                      grpc_chttp2_stream* s) {
  if (s->recv_initial_metadata_ready != nullptr &&
      s->published_metadata[0] != GRPC_METADATA_NOT_PUBLISHED) {
    if (s->seen_error) {
      grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
      if (!s->pending_byte_stream) {
        grpc_slice_buffer_reset_and_unref_internal(
            &s->unprocessed_incoming_frames_buffer);
      }
    }
    grpc_chttp2_incoming_metadata_buffer_publish(&s->metadata_buffer[0],
                                                 s->recv_initial_metadata);
    null_then_run_closure(&s->recv_initial_metadata_ready, GRPC_ERROR_NONE);
  }
}

void grpc_chttp2_maybe_complete_recv_message(grpc_chttp2_transport* t,
                                             grpc_chttp2_stream* s) {
  grpc_error* error = GRPC_ERROR_NONE;
  if (s->recv_message_ready != nullptr) {
    *s->recv_message = nullptr;
    if (s->final_metadata_requested && s->seen_error) {
      grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
      if (!s->pending_byte_stream) {
        grpc_slice_buffer_reset_and_unref_internal(
            &s->unprocessed_incoming_frames_buffer);
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
            grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
            grpc_slice_buffer_reset_and_unref_internal(
                &s->unprocessed_incoming_frames_buffer);
            error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Stream decompression error.");
          } else {
            s->decompressed_header_bytes += s->decompressed_data_buffer.length;
            if (s->decompressed_header_bytes == GRPC_HEADER_SIZE_IN_BYTES) {
              s->decompressed_header_bytes = 0;
            }
            error = grpc_deframe_unprocessed_incoming_frames(
                &s->data_parser, s, &s->decompressed_data_buffer, nullptr,
                s->recv_message);
            if (end_of_context) {
              grpc_stream_compression_context_destroy(
                  s->stream_decompression_ctx);
              s->stream_decompression_ctx = nullptr;
            }
          }
        } else {
          error = grpc_deframe_unprocessed_incoming_frames(
              &s->data_parser, s, &s->unprocessed_incoming_frames_buffer,
              nullptr, s->recv_message);
        }
        if (error != GRPC_ERROR_NONE) {
          s->seen_error = true;
          grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
          grpc_slice_buffer_reset_and_unref_internal(
              &s->unprocessed_incoming_frames_buffer);
          break;
        } else if (*s->recv_message != nullptr) {
          break;
        }
      }
    }
    // save the length of the buffer before handing control back to application
    // threads. Needed to support correct flow control bookkeeping
    s->unprocessed_incoming_frames_buffer_cached_length =
        s->unprocessed_incoming_frames_buffer.length;
    if (error == GRPC_ERROR_NONE && *s->recv_message != nullptr) {
      null_then_run_closure(&s->recv_message_ready, GRPC_ERROR_NONE);
    } else if (s->published_metadata[1] != GRPC_METADATA_NOT_PUBLISHED) {
      *s->recv_message = nullptr;
      null_then_run_closure(&s->recv_message_ready, GRPC_ERROR_NONE);
    }
    GRPC_ERROR_UNREF(error);
  }
}

void grpc_chttp2_maybe_complete_recv_trailing_metadata(grpc_chttp2_transport* t,
                                                       grpc_chttp2_stream* s) {
  grpc_chttp2_maybe_complete_recv_message(t, s);
  if (s->recv_trailing_metadata_finished != nullptr && s->read_closed &&
      s->write_closed) {
    if (s->seen_error || !t->is_client) {
      grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
      if (!s->pending_byte_stream) {
        grpc_slice_buffer_reset_and_unref_internal(
            &s->unprocessed_incoming_frames_buffer);
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
        grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
        grpc_slice_buffer_reset_and_unref_internal(
            &s->unprocessed_incoming_frames_buffer);
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
      grpc_transport_move_stats(&s->stats, s->collecting_stats);
      s->collecting_stats = nullptr;
      grpc_chttp2_incoming_metadata_buffer_publish(&s->metadata_buffer[1],
                                                   s->recv_trailing_metadata);
      null_then_run_closure(&s->recv_trailing_metadata_finished,
                            GRPC_ERROR_NONE);
    }
  }
}

static void remove_stream(grpc_chttp2_transport* t, uint32_t id,
                          grpc_error* error) {
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(
      grpc_chttp2_stream_map_delete(&t->stream_map, id));
  GPR_ASSERT(s);
  if (t->incoming_stream == s) {
    t->incoming_stream = nullptr;
    grpc_chttp2_parsing_become_skip_parser(t);
  }
  if (s->pending_byte_stream) {
    if (s->on_next != nullptr) {
      grpc_core::Chttp2IncomingByteStream* bs = s->data_parser.parsing_frame;
      if (error == GRPC_ERROR_NONE) {
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
      }
      bs->PublishError(error);
      bs->Unref();
      s->data_parser.parsing_frame = nullptr;
    } else {
      GRPC_ERROR_UNREF(s->byte_stream_error);
      s->byte_stream_error = GRPC_ERROR_REF(error);
    }
  }

  if (grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    post_benign_reclaimer(t);
    if (t->sent_goaway_state == GRPC_CHTTP2_GOAWAY_SENT) {
      close_transport_locked(
          t, GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                 "Last stream closed after sending GOAWAY", &error, 1));
    }
  }
  if (grpc_chttp2_list_remove_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:remove_stream");
  }

  GRPC_ERROR_UNREF(error);

  maybe_start_some_streams(t);
}

void grpc_chttp2_cancel_stream(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                               grpc_error* due_to_error) {
  if (!t->is_client && !s->sent_trailing_metadata &&
      grpc_error_has_clear_grpc_status(due_to_error)) {
    close_from_api(t, s, due_to_error);
    return;
  }

  if (!s->read_closed || !s->write_closed) {
    if (s->id != 0) {
      grpc_http2_error_code http_error;
      grpc_error_get_status(due_to_error, s->deadline, nullptr, nullptr,
                            &http_error, nullptr);
      grpc_slice_buffer_add(
          &t->qbuf,
          grpc_chttp2_rst_stream_create(
              s->id, static_cast<uint32_t>(http_error), &s->stats.outgoing));
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
    }
  }
  if (due_to_error != GRPC_ERROR_NONE && !s->seen_error) {
    s->seen_error = true;
  }
  grpc_chttp2_mark_stream_closed(t, s, 1, 1, due_to_error);
}

void grpc_chttp2_fake_status(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                             grpc_error* error) {
  grpc_status_code status;
  grpc_slice slice;
  grpc_error_get_status(error, s->deadline, &status, &slice, nullptr, nullptr);
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
                          &s->metadata_buffer[1],
                          grpc_mdelem_from_slices(
                              GRPC_MDSTR_GRPC_STATUS,
                              grpc_slice_from_copied_string(status_string))));
    if (!GRPC_SLICE_IS_EMPTY(slice)) {
      GRPC_LOG_IF_ERROR(
          "add_status_message",
          grpc_chttp2_incoming_metadata_buffer_replace_or_add(
              &s->metadata_buffer[1],
              grpc_mdelem_create(GRPC_MDSTR_GRPC_MESSAGE, slice, nullptr)));
    }
    s->published_metadata[1] = GRPC_METADATA_SYNTHESIZED_FROM_FAKE;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
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

static void flush_write_list(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                             grpc_chttp2_write_cb** list, grpc_error* error) {
  while (*list) {
    grpc_chttp2_write_cb* cb = *list;
    *list = cb->next;
    grpc_chttp2_complete_closure_step(t, s, &cb->closure, GRPC_ERROR_REF(error),
                                      "on_write_finished_cb");
    cb->next = t->write_cb_pool;
    t->write_cb_pool = cb;
  }
  GRPC_ERROR_UNREF(error);
}

void grpc_chttp2_fail_pending_writes(grpc_chttp2_transport* t,
                                     grpc_chttp2_stream* s, grpc_error* error) {
  error =
      removal_error(error, s, "Pending writes failed due to stream closure");
  s->send_initial_metadata = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_initial_metadata_finished,
                                    GRPC_ERROR_REF(error),
                                    "send_initial_metadata_finished");

  s->send_trailing_metadata = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_trailing_metadata_finished,
                                    GRPC_ERROR_REF(error),
                                    "send_trailing_metadata_finished");

  s->fetching_send_message.reset();
  grpc_chttp2_complete_closure_step(t, s, &s->fetching_send_message_finished,
                                    GRPC_ERROR_REF(error),
                                    "fetching_send_message_finished");
  flush_write_list(t, s, &s->on_write_finished_cbs, GRPC_ERROR_REF(error));
  flush_write_list(t, s, &s->on_flow_controlled_cbs, error);
}

void grpc_chttp2_mark_stream_closed(grpc_chttp2_transport* t,
                                    grpc_chttp2_stream* s, int close_reads,
                                    int close_writes, grpc_error* error) {
  if (s->read_closed && s->write_closed) {
    /* already closed */
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
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
    grpc_chttp2_fail_pending_writes(t, s, GRPC_ERROR_REF(error));
  }
  if (s->read_closed && s->write_closed) {
    became_closed = true;
    grpc_error* overall_error =
        removal_error(GRPC_ERROR_REF(error), s, "Stream removed");
    if (s->id != 0) {
      remove_stream(t, s->id, GRPC_ERROR_REF(overall_error));
    } else {
      /* Purge streams waiting on concurrency still waiting for id assignment */
      grpc_chttp2_list_remove_waiting_for_concurrency(t, s);
    }
    if (overall_error != GRPC_ERROR_NONE) {
      grpc_chttp2_fake_status(t, s, overall_error);
    }
  }
  if (closed_read) {
    for (int i = 0; i < 2; i++) {
      if (s->published_metadata[i] == GRPC_METADATA_NOT_PUBLISHED) {
        s->published_metadata[i] = GPRC_METADATA_PUBLISHED_AT_CLOSE;
      }
    }
    grpc_chttp2_maybe_complete_recv_initial_metadata(t, s);
    grpc_chttp2_maybe_complete_recv_message(t, s);
  }
  if (became_closed) {
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
    GRPC_CHTTP2_STREAM_UNREF(s, "chttp2");
  }
  GRPC_ERROR_UNREF(error);
}

static void close_from_api(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                           grpc_error* error) {
  grpc_slice hdr;
  grpc_slice status_hdr;
  grpc_slice http_status_hdr;
  grpc_slice content_type_hdr;
  grpc_slice message_pfx;
  uint8_t* p;
  uint32_t len = 0;
  grpc_status_code grpc_status;
  grpc_slice slice;
  grpc_error_get_status(error, s->deadline, &grpc_status, &slice, nullptr,
                        nullptr);

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
    len += static_cast<uint32_t> GRPC_SLICE_LENGTH(http_status_hdr);

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
    len += static_cast<uint32_t> GRPC_SLICE_LENGTH(content_type_hdr);
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
    *p++ = static_cast<uint8_t>('0' + grpc_status);
  } else {
    *p++ = 2;
    *p++ = static_cast<uint8_t>('0' + (grpc_status / 10));
    *p++ = static_cast<uint8_t>('0' + (grpc_status % 10));
  }
  GPR_ASSERT(p == GRPC_SLICE_END_PTR(status_hdr));
  len += static_cast<uint32_t> GRPC_SLICE_LENGTH(status_hdr);

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
  len += static_cast<uint32_t> GRPC_SLICE_LENGTH(message_pfx);
  len += static_cast<uint32_t>(msg_len);

  hdr = GRPC_SLICE_MALLOC(9);
  p = GRPC_SLICE_START_PTR(hdr);
  *p++ = static_cast<uint8_t>(len >> 16);
  *p++ = static_cast<uint8_t>(len >> 8);
  *p++ = static_cast<uint8_t>(len);
  *p++ = GRPC_CHTTP2_FRAME_HEADER;
  *p++ = GRPC_CHTTP2_DATA_FLAG_END_STREAM | GRPC_CHTTP2_DATA_FLAG_END_HEADERS;
  *p++ = static_cast<uint8_t>(s->id >> 24);
  *p++ = static_cast<uint8_t>(s->id >> 16);
  *p++ = static_cast<uint8_t>(s->id >> 8);
  *p++ = static_cast<uint8_t>(s->id);
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

  grpc_chttp2_mark_stream_closed(t, s, 1, 1, error);
  grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_CLOSE_FROM_API);
}

typedef struct {
  grpc_error* error;
  grpc_chttp2_transport* t;
} cancel_stream_cb_args;

static void cancel_stream_cb(void* user_data, uint32_t key, void* stream) {
  cancel_stream_cb_args* args = static_cast<cancel_stream_cb_args*>(user_data);
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(stream);
  grpc_chttp2_cancel_stream(args->t, s, GRPC_ERROR_REF(args->error));
}

static void end_all_the_calls(grpc_chttp2_transport* t, grpc_error* error) {
  cancel_stream_cb_args args = {error, t};
  grpc_chttp2_stream_map_for_each(&t->stream_map, cancel_stream_cb, &args);
  GRPC_ERROR_UNREF(error);
}

/*******************************************************************************
 * INPUT PROCESSING - PARSING
 */

template <class F>
static void WithUrgency(grpc_chttp2_transport* t,
                        grpc_core::chttp2::FlowControlAction::Urgency urgency,
                        grpc_chttp2_initiate_write_reason reason, F action) {
  switch (urgency) {
    case grpc_core::chttp2::FlowControlAction::Urgency::NO_ACTION_NEEDED:
      break;
    case grpc_core::chttp2::FlowControlAction::Urgency::UPDATE_IMMEDIATELY:
      grpc_chttp2_initiate_write(t, reason);
    // fallthrough
    case grpc_core::chttp2::FlowControlAction::Urgency::QUEUE_UPDATE:
      action();
      break;
  }
}

void grpc_chttp2_act_on_flowctl_action(
    const grpc_core::chttp2::FlowControlAction& action,
    grpc_chttp2_transport* t, grpc_chttp2_stream* s) {
  WithUrgency(t, action.send_stream_update(),
              GRPC_CHTTP2_INITIATE_WRITE_STREAM_FLOW_CONTROL,
              [t, s]() { grpc_chttp2_mark_stream_writable(t, s); });
  WithUrgency(t, action.send_transport_update(),
              GRPC_CHTTP2_INITIATE_WRITE_TRANSPORT_FLOW_CONTROL, []() {});
  WithUrgency(t, action.send_initial_window_update(),
              GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS, [t, &action]() {
                queue_setting_update(t,
                                     GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
                                     action.initial_window_size());
              });
  WithUrgency(t, action.send_max_frame_size_update(),
              GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS, [t, &action]() {
                queue_setting_update(t, GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
                                     action.max_frame_size());
              });
}

static grpc_error* try_http_parsing(grpc_chttp2_transport* t) {
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

static void read_action_locked(void* tp, grpc_error* error) {
  GPR_TIMER_SCOPE("reading_action_locked", 0);

  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);

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
    GPR_TIMER_SCOPE("reading_action.parse", 0);
    size_t i = 0;
    grpc_error* errors[3] = {GRPC_ERROR_REF(error), GRPC_ERROR_NONE,
                             GRPC_ERROR_NONE};
    for (; i < t->read_buffer.count && errors[1] == GRPC_ERROR_NONE; i++) {
      grpc_core::BdpEstimator* bdp_est = t->flow_control->bdp_estimator();
      if (bdp_est) {
        bdp_est->AddIncomingBytes(
            static_cast<int64_t> GRPC_SLICE_LENGTH(t->read_buffer.slices[i]));
      }
      errors[1] = grpc_chttp2_perform_read(t, t->read_buffer.slices[i]);
    }
    if (errors[1] != GRPC_ERROR_NONE) {
      errors[2] = try_http_parsing(t);
      GRPC_ERROR_UNREF(error);
      error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
          "Failed parsing HTTP/2", errors, GPR_ARRAY_SIZE(errors));
    }
    for (i = 0; i < GPR_ARRAY_SIZE(errors); i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }

    GPR_TIMER_SCOPE("post_parse_locked", 0);
    if (t->initial_window_update != 0) {
      if (t->initial_window_update > 0) {
        grpc_chttp2_stream* s;
        while (grpc_chttp2_list_pop_stalled_by_stream(t, &s)) {
          grpc_chttp2_mark_stream_writable(t, s);
          grpc_chttp2_initiate_write(
              t, GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_SETTING);
        }
      }
      t->initial_window_update = 0;
    }
  }

  GPR_TIMER_SCOPE("post_reading_action_locked", 0);
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

    close_transport_locked(t, GRPC_ERROR_REF(error));
    t->endpoint_reading = 0;
  } else if (t->closed_with_error == GRPC_ERROR_NONE) {
    keep_reading = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "keep_reading");
  }
  grpc_slice_buffer_reset_and_unref_internal(&t->read_buffer);

  if (keep_reading) {
    grpc_endpoint_read(t->ep, &t->read_buffer, &t->read_action_locked);
    grpc_chttp2_act_on_flowctl_action(t->flow_control->MakeAction(), t,
                                      nullptr);
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "keep_reading");
  } else {
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "reading_action");
  }

  GRPC_ERROR_UNREF(error);
}

// t is reffed prior to calling the first time, and once the callback chain
// that kicks off finishes, it's unreffed
static void schedule_bdp_ping_locked(grpc_chttp2_transport* t) {
  t->flow_control->bdp_estimator()->SchedulePing();
  send_ping_locked(t, &t->start_bdp_ping_locked, &t->finish_bdp_ping_locked);
}

static void start_bdp_ping_locked(void* tp, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_INFO, "%s: Start BDP ping err=%s", t->peer_string,
            grpc_error_string(error));
  }
  /* Reset the keepalive ping timer */
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING) {
    grpc_timer_cancel(&t->keepalive_ping_timer);
  }
  t->flow_control->bdp_estimator()->StartPing();
}

static void finish_bdp_ping_locked(void* tp, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_INFO, "%s: Complete BDP ping err=%s", t->peer_string,
            grpc_error_string(error));
  }
  if (error != GRPC_ERROR_NONE) {
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "bdp_ping");
    return;
  }
  grpc_millis next_ping = t->flow_control->bdp_estimator()->CompletePing();
  grpc_chttp2_act_on_flowctl_action(t->flow_control->PeriodicUpdate(), t,
                                    nullptr);
  GPR_ASSERT(!t->have_next_bdp_ping_timer);
  t->have_next_bdp_ping_timer = true;
  grpc_timer_init(&t->next_bdp_ping_timer, next_ping,
                  &t->next_bdp_ping_timer_expired_locked);
}

static void next_bdp_ping_timer_expired_locked(void* tp, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  GPR_ASSERT(t->have_next_bdp_ping_timer);
  t->have_next_bdp_ping_timer = false;
  if (error != GRPC_ERROR_NONE) {
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "bdp_ping");
    return;
  }
  schedule_bdp_ping_locked(t);
}

void grpc_chttp2_config_default_keepalive_args(grpc_channel_args* args,
                                               bool is_client) {
  size_t i;
  if (args) {
    for (i = 0; i < args->num_args; i++) {
      if (0 == strcmp(args->args[i].key, GRPC_ARG_KEEPALIVE_TIME_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &args->args[i], {is_client ? g_default_client_keepalive_time_ms
                                       : g_default_server_keepalive_time_ms,
                             1, INT_MAX});
        if (is_client) {
          g_default_client_keepalive_time_ms = value;
        } else {
          g_default_server_keepalive_time_ms = value;
        }
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_ARG_KEEPALIVE_TIMEOUT_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &args->args[i], {is_client ? g_default_client_keepalive_timeout_ms
                                       : g_default_server_keepalive_timeout_ms,
                             0, INT_MAX});
        if (is_client) {
          g_default_client_keepalive_timeout_ms = value;
        } else {
          g_default_server_keepalive_timeout_ms = value;
        }
      } else if (0 == strcmp(args->args[i].key,
                             GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)) {
        const bool value = static_cast<uint32_t>(grpc_channel_arg_get_integer(
            &args->args[i],
            {is_client ? g_default_client_keepalive_permit_without_calls
                       : g_default_server_keepalive_timeout_ms,
             0, 1}));
        if (is_client) {
          g_default_client_keepalive_permit_without_calls = value;
        } else {
          g_default_server_keepalive_permit_without_calls = value;
        }
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

static void init_keepalive_ping_locked(void* arg, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  GPR_ASSERT(t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING);
  if (t->destroying || t->closed_with_error != GRPC_ERROR_NONE) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
  } else if (error == GRPC_ERROR_NONE) {
    if (t->keepalive_permit_without_calls ||
        grpc_chttp2_stream_map_size(&t->stream_map) > 0) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_PINGING;
      GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive ping end");
      grpc_timer_init_unset(&t->keepalive_watchdog_timer);
      send_keepalive_ping_locked(t);
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING);
    } else {
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      grpc_timer_init(&t->keepalive_ping_timer,
                      grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                      &t->init_keepalive_ping_locked);
    }
  } else if (error == GRPC_ERROR_CANCELLED) {
    /* The keepalive ping timer may be cancelled by bdp */
    GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
    grpc_timer_init(&t->keepalive_ping_timer,
                    grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                    &t->init_keepalive_ping_locked);
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "init keepalive ping");
}

static void start_keepalive_ping_locked(void* arg, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (error != GRPC_ERROR_NONE) {
    return;
  }
  if (t->channelz_socket != nullptr) {
    t->channelz_socket->RecordKeepaliveSent();
  }
  GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive watchdog");
  grpc_timer_init(&t->keepalive_watchdog_timer,
                  grpc_core::ExecCtx::Get()->Now() + t->keepalive_timeout,
                  &t->keepalive_watchdog_fired_locked);
}

static void finish_keepalive_ping_locked(void* arg, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    if (error == GRPC_ERROR_NONE) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
      grpc_timer_cancel(&t->keepalive_watchdog_timer);
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      grpc_timer_init(&t->keepalive_ping_timer,
                      grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                      &t->init_keepalive_ping_locked);
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive ping end");
}

static void keepalive_watchdog_fired_locked(void* arg, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    if (error == GRPC_ERROR_NONE) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
      close_transport_locked(
          t, grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                    "keepalive watchdog timeout"),
                                GRPC_ERROR_INT_GRPC_STATUS,
                                GRPC_STATUS_UNAVAILABLE));
    }
  } else {
    /* The watchdog timer should have been cancelled by
     * finish_keepalive_ping_locked. */
    if (GPR_UNLIKELY(error != GRPC_ERROR_CANCELLED)) {
      gpr_log(GPR_ERROR, "keepalive_ping_end state error: %d (expect: %d)",
              t->keepalive_state, GRPC_CHTTP2_KEEPALIVE_STATE_PINGING);
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive watchdog");
}

/*******************************************************************************
 * CALLBACK LOOP
 */

static void connectivity_state_set(grpc_chttp2_transport* t,
                                   grpc_connectivity_state state,
                                   grpc_error* error, const char* reason) {
  GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "set connectivity_state=%d", state));
  grpc_connectivity_state_set(&t->channel_callback.state_tracker, state, error,
                              reason);
}

/*******************************************************************************
 * POLLSET STUFF
 */

static void set_pollset(grpc_transport* gt, grpc_stream* gs,
                        grpc_pollset* pollset) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_endpoint_add_to_pollset(t->ep, pollset);
}

static void set_pollset_set(grpc_transport* gt, grpc_stream* gs,
                            grpc_pollset_set* pollset_set) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_endpoint_add_to_pollset_set(t->ep, pollset_set);
}

/*******************************************************************************
 * BYTE STREAM
 */

static void reset_byte_stream(void* arg, grpc_error* error) {
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(arg);
  s->pending_byte_stream = false;
  if (error == GRPC_ERROR_NONE) {
    grpc_chttp2_maybe_complete_recv_message(s->t, s);
    grpc_chttp2_maybe_complete_recv_trailing_metadata(s->t, s);
  } else {
    GPR_ASSERT(error != GRPC_ERROR_NONE);
    GRPC_CLOSURE_SCHED(s->on_next, GRPC_ERROR_REF(error));
    s->on_next = nullptr;
    GRPC_ERROR_UNREF(s->byte_stream_error);
    s->byte_stream_error = GRPC_ERROR_NONE;
    grpc_chttp2_cancel_stream(s->t, s, GRPC_ERROR_REF(error));
    s->byte_stream_error = GRPC_ERROR_REF(error);
  }
}

namespace grpc_core {

Chttp2IncomingByteStream::Chttp2IncomingByteStream(
    grpc_chttp2_transport* transport, grpc_chttp2_stream* stream,
    uint32_t frame_size, uint32_t flags)
    : ByteStream(frame_size, flags),
      transport_(transport),
      stream_(stream),
      refs_(2),
      remaining_bytes_(frame_size) {
  GRPC_ERROR_UNREF(stream->byte_stream_error);
  stream->byte_stream_error = GRPC_ERROR_NONE;
}

void Chttp2IncomingByteStream::OrphanLocked(void* arg,
                                            grpc_error* error_ignored) {
  Chttp2IncomingByteStream* bs = static_cast<Chttp2IncomingByteStream*>(arg);
  grpc_chttp2_stream* s = bs->stream_;
  grpc_chttp2_transport* t = s->t;
  bs->Unref();
  s->pending_byte_stream = false;
  grpc_chttp2_maybe_complete_recv_message(t, s);
  grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
}

void Chttp2IncomingByteStream::Orphan() {
  GPR_TIMER_SCOPE("incoming_byte_stream_destroy", 0);
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_INIT(&destroy_action_,
                        &Chttp2IncomingByteStream::OrphanLocked, this,
                        grpc_combiner_scheduler(transport_->combiner)),
      GRPC_ERROR_NONE);
}

void Chttp2IncomingByteStream::NextLocked(void* arg,
                                          grpc_error* error_ignored) {
  Chttp2IncomingByteStream* bs = static_cast<Chttp2IncomingByteStream*>(arg);
  grpc_chttp2_transport* t = bs->transport_;
  grpc_chttp2_stream* s = bs->stream_;
  size_t cur_length = s->frame_storage.length;
  if (!s->read_closed) {
    s->flow_control->IncomingByteStreamUpdate(bs->next_action_.max_size_hint,
                                              cur_length);
    grpc_chttp2_act_on_flowctl_action(s->flow_control->MakeAction(), t, s);
  }
  GPR_ASSERT(s->unprocessed_incoming_frames_buffer.length == 0);
  if (s->frame_storage.length > 0) {
    grpc_slice_buffer_swap(&s->frame_storage,
                           &s->unprocessed_incoming_frames_buffer);
    s->unprocessed_incoming_frames_decompressed = false;
    GRPC_CLOSURE_SCHED(bs->next_action_.on_complete, GRPC_ERROR_NONE);
  } else if (s->byte_stream_error != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_SCHED(bs->next_action_.on_complete,
                       GRPC_ERROR_REF(s->byte_stream_error));
    if (s->data_parser.parsing_frame != nullptr) {
      s->data_parser.parsing_frame->Unref();
      s->data_parser.parsing_frame = nullptr;
    }
  } else if (s->read_closed) {
    if (bs->remaining_bytes_ != 0) {
      s->byte_stream_error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
      GRPC_CLOSURE_SCHED(bs->next_action_.on_complete,
                         GRPC_ERROR_REF(s->byte_stream_error));
      if (s->data_parser.parsing_frame != nullptr) {
        s->data_parser.parsing_frame->Unref();
        s->data_parser.parsing_frame = nullptr;
      }
    } else {
      /* Should never reach here. */
      GPR_ASSERT(false);
    }
  } else {
    s->on_next = bs->next_action_.on_complete;
  }
  bs->Unref();
}

bool Chttp2IncomingByteStream::Next(size_t max_size_hint,
                                    grpc_closure* on_complete) {
  GPR_TIMER_SCOPE("incoming_byte_stream_next", 0);
  if (stream_->unprocessed_incoming_frames_buffer.length > 0) {
    return true;
  } else {
    Ref();
    next_action_.max_size_hint = max_size_hint;
    next_action_.on_complete = on_complete;
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_INIT(&next_action_.closure,
                          &Chttp2IncomingByteStream::NextLocked, this,
                          grpc_combiner_scheduler(transport_->combiner)),
        GRPC_ERROR_NONE);
    return false;
  }
}

void Chttp2IncomingByteStream::MaybeCreateStreamDecompressionCtx() {
  if (!stream_->stream_decompression_ctx) {
    stream_->stream_decompression_ctx = grpc_stream_compression_context_create(
        stream_->stream_decompression_method);
  }
}

grpc_error* Chttp2IncomingByteStream::Pull(grpc_slice* slice) {
  GPR_TIMER_SCOPE("incoming_byte_stream_pull", 0);
  grpc_error* error;
  if (stream_->unprocessed_incoming_frames_buffer.length > 0) {
    if (!stream_->unprocessed_incoming_frames_decompressed) {
      bool end_of_context;
      MaybeCreateStreamDecompressionCtx();
      if (!grpc_stream_decompress(stream_->stream_decompression_ctx,
                                  &stream_->unprocessed_incoming_frames_buffer,
                                  &stream_->decompressed_data_buffer, nullptr,
                                  MAX_SIZE_T, &end_of_context)) {
        error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Stream decompression error.");
        return error;
      }
      GPR_ASSERT(stream_->unprocessed_incoming_frames_buffer.length == 0);
      grpc_slice_buffer_swap(&stream_->unprocessed_incoming_frames_buffer,
                             &stream_->decompressed_data_buffer);
      stream_->unprocessed_incoming_frames_decompressed = true;
      if (end_of_context) {
        grpc_stream_compression_context_destroy(
            stream_->stream_decompression_ctx);
        stream_->stream_decompression_ctx = nullptr;
      }
      if (stream_->unprocessed_incoming_frames_buffer.length == 0) {
        *slice = grpc_empty_slice();
      }
    }
    error = grpc_deframe_unprocessed_incoming_frames(
        &stream_->data_parser, stream_,
        &stream_->unprocessed_incoming_frames_buffer, slice, nullptr);
    if (error != GRPC_ERROR_NONE) {
      return error;
    }
  } else {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
    GRPC_CLOSURE_SCHED(&stream_->reset_byte_stream, GRPC_ERROR_REF(error));
    return error;
  }
  return GRPC_ERROR_NONE;
}

void Chttp2IncomingByteStream::PublishError(grpc_error* error) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GRPC_CLOSURE_SCHED(stream_->on_next, GRPC_ERROR_REF(error));
  stream_->on_next = nullptr;
  GRPC_ERROR_UNREF(stream_->byte_stream_error);
  stream_->byte_stream_error = GRPC_ERROR_REF(error);
  grpc_chttp2_cancel_stream(transport_, stream_, GRPC_ERROR_REF(error));
}

grpc_error* Chttp2IncomingByteStream::Push(grpc_slice slice,
                                           grpc_slice* slice_out) {
  if (remaining_bytes_ < GRPC_SLICE_LENGTH(slice)) {
    grpc_error* error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Too many bytes in stream");
    GRPC_CLOSURE_SCHED(&stream_->reset_byte_stream, GRPC_ERROR_REF(error));
    grpc_slice_unref_internal(slice);
    return error;
  } else {
    remaining_bytes_ -= static_cast<uint32_t> GRPC_SLICE_LENGTH(slice);
    if (slice_out != nullptr) {
      *slice_out = slice;
    }
    return GRPC_ERROR_NONE;
  }
}

grpc_error* Chttp2IncomingByteStream::Finished(grpc_error* error,
                                               bool reset_on_error) {
  if (error == GRPC_ERROR_NONE) {
    if (remaining_bytes_ != 0) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Truncated message");
    }
  }
  if (error != GRPC_ERROR_NONE && reset_on_error) {
    GRPC_CLOSURE_SCHED(&stream_->reset_byte_stream, GRPC_ERROR_REF(error));
  }
  Unref();
  return error;
}

void Chttp2IncomingByteStream::Shutdown(grpc_error* error) {
  GRPC_ERROR_UNREF(Finished(error, true /* reset_on_error */));
}

}  // namespace grpc_core

/*******************************************************************************
 * RESOURCE QUOTAS
 */

static void post_benign_reclaimer(grpc_chttp2_transport* t) {
  if (!t->benign_reclaimer_registered) {
    t->benign_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "benign_reclaimer");
    grpc_resource_user_post_reclaimer(grpc_endpoint_get_resource_user(t->ep),
                                      false, &t->benign_reclaimer_locked);
  }
}

static void post_destructive_reclaimer(grpc_chttp2_transport* t) {
  if (!t->destructive_reclaimer_registered) {
    t->destructive_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "destructive_reclaimer");
    grpc_resource_user_post_reclaimer(grpc_endpoint_get_resource_user(t->ep),
                                      true, &t->destructive_reclaimer_locked);
  }
}

static void benign_reclaimer_locked(void* arg, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (error == GRPC_ERROR_NONE &&
      grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    /* Channel with no active streams: send a goaway to try and make it
     * disconnect cleanly */
    if (grpc_resource_quota_trace.enabled()) {
      gpr_log(GPR_INFO, "HTTP2: %s - send goaway to free memory",
              t->peer_string);
    }
    send_goaway(t,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("Buffers full"),
                    GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_ENHANCE_YOUR_CALM));
  } else if (error == GRPC_ERROR_NONE && grpc_resource_quota_trace.enabled()) {
    gpr_log(GPR_INFO,
            "HTTP2: %s - skip benign reclamation, there are still %" PRIdPTR
            " streams",
            t->peer_string, grpc_chttp2_stream_map_size(&t->stream_map));
  }
  t->benign_reclaimer_registered = false;
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_resource_user_finish_reclamation(
        grpc_endpoint_get_resource_user(t->ep));
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "benign_reclaimer");
}

static void destructive_reclaimer_locked(void* arg, grpc_error* error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  size_t n = grpc_chttp2_stream_map_size(&t->stream_map);
  t->destructive_reclaimer_registered = false;
  if (error == GRPC_ERROR_NONE && n > 0) {
    grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(
        grpc_chttp2_stream_map_rand(&t->stream_map));
    if (grpc_resource_quota_trace.enabled()) {
      gpr_log(GPR_INFO, "HTTP2: %s - abandon stream id %d", t->peer_string,
              s->id);
    }
    grpc_chttp2_cancel_stream(
        t, s,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Buffers full"),
                           GRPC_ERROR_INT_HTTP2_ERROR,
                           GRPC_HTTP2_ENHANCE_YOUR_CALM));
    if (n > 1) {
      /* Since we cancel one stream per destructive reclamation, if
         there are more streams left, we can immediately post a new
         reclaimer in case the resource quota needs to free more
         memory */
      post_destructive_reclaimer(t);
    }
  }
  if (error != GRPC_ERROR_CANCELLED) {
    grpc_resource_user_finish_reclamation(
        grpc_endpoint_get_resource_user(t->ep));
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "destructive_reclaimer");
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

static grpc_endpoint* chttp2_get_endpoint(grpc_transport* t) {
  return (reinterpret_cast<grpc_chttp2_transport*>(t))->ep;
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

grpc_core::RefCountedPtr<grpc_core::channelz::SocketNode>
grpc_chttp2_transport_get_socket_node(grpc_transport* transport) {
  grpc_chttp2_transport* t =
      reinterpret_cast<grpc_chttp2_transport*>(transport);
  return t->channelz_socket;
}

grpc_transport* grpc_create_chttp2_transport(
    const grpc_channel_args* channel_args, grpc_endpoint* ep, bool is_client,
    grpc_resource_user* resource_user) {
  auto t = grpc_core::New<grpc_chttp2_transport>(channel_args, ep, is_client,
                                                 resource_user);
  return &t->base;
}

void grpc_chttp2_transport_start_reading(
    grpc_transport* transport, grpc_slice_buffer* read_buffer,
    grpc_closure* notify_on_receive_settings) {
  grpc_chttp2_transport* t =
      reinterpret_cast<grpc_chttp2_transport*>(transport);
  GRPC_CHTTP2_REF_TRANSPORT(
      t, "reading_action"); /* matches unref inside reading_action */
  if (read_buffer != nullptr) {
    grpc_slice_buffer_move_into(read_buffer, &t->read_buffer);
    gpr_free(read_buffer);
  }
  t->notify_on_receive_settings = notify_on_receive_settings;
  GRPC_CLOSURE_SCHED(&t->read_action_locked, GRPC_ERROR_NONE);
}
