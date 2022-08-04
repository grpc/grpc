//
// Copyright 2018 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/context_list.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/stream_map.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/global_config_env.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/resource_quota/trace.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_impl.h"

GPR_GLOBAL_CONFIG_DEFINE_BOOL(
    grpc_experimental_enable_peer_state_based_framing, false,
    "If set, the max sizes of frames sent to lower layers is controlled based "
    "on the peer's memory pressure which is reflected in its max http2 frame "
    "size.");

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

#define DEFAULT_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS 300000 /* 5 minutes */
#define DEFAULT_MAX_PINGS_BETWEEN_DATA 2
#define DEFAULT_MAX_PING_STRIKES 2

#define DEFAULT_MAX_PENDING_INDUCED_FRAMES 10000

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

static int g_default_min_recv_ping_interval_without_data_ms =
    DEFAULT_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS;
static int g_default_max_pings_without_data = DEFAULT_MAX_PINGS_BETWEEN_DATA;
static int g_default_max_ping_strikes = DEFAULT_MAX_PING_STRIKES;

#define MAX_CLIENT_STREAM_ID 0x7fffffffu
grpc_core::TraceFlag grpc_http_trace(false, "http");
grpc_core::TraceFlag grpc_keepalive_trace(false, "http_keepalive");
grpc_core::DebugOnlyTraceFlag grpc_trace_chttp2_refcount(false,
                                                         "chttp2_refcount");

// forward declarations of various callbacks that we'll build closures around
static void write_action_begin_locked(void* t, grpc_error_handle error);
static void write_action(void* t, grpc_error_handle error);
static void write_action_end(void* t, grpc_error_handle error);
static void write_action_end_locked(void* t, grpc_error_handle error);

static void read_action(void* t, grpc_error_handle error);
static void read_action_locked(void* t, grpc_error_handle error);
static void continue_read_action_locked(grpc_chttp2_transport* t);

// Set a transport level setting, and push it to our peer
static void queue_setting_update(grpc_chttp2_transport* t,
                                 grpc_chttp2_setting_id id, uint32_t value);

static void close_from_api(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                           grpc_error_handle error);

// Start new streams that have been created if we can
static void maybe_start_some_streams(grpc_chttp2_transport* t);

static void connectivity_state_set(grpc_chttp2_transport* t,
                                   grpc_connectivity_state state,
                                   const absl::Status& status,
                                   const char* reason);

static void benign_reclaimer_locked(void* arg, grpc_error_handle error);
static void destructive_reclaimer_locked(void* arg, grpc_error_handle error);

static void post_benign_reclaimer(grpc_chttp2_transport* t);
static void post_destructive_reclaimer(grpc_chttp2_transport* t);

static void close_transport_locked(grpc_chttp2_transport* t,
                                   grpc_error_handle error);
static void end_all_the_calls(grpc_chttp2_transport* t,
                              grpc_error_handle error);

static void start_bdp_ping(void* tp, grpc_error_handle error);
static void finish_bdp_ping(void* tp, grpc_error_handle error);
static void start_bdp_ping_locked(void* tp, grpc_error_handle error);
static void finish_bdp_ping_locked(void* tp, grpc_error_handle error);
static void next_bdp_ping_timer_expired(void* tp, grpc_error_handle error);
static void next_bdp_ping_timer_expired_locked(void* tp,
                                               grpc_error_handle error);

static void cancel_pings(grpc_chttp2_transport* t, grpc_error_handle error);
static void send_ping_locked(grpc_chttp2_transport* t,
                             grpc_closure* on_initiate, grpc_closure* on_ack);
static void retry_initiate_ping_locked(void* tp, grpc_error_handle error);

// keepalive-relevant functions
static void init_keepalive_ping(void* arg, grpc_error_handle error);
static void init_keepalive_ping_locked(void* arg, grpc_error_handle error);
static void start_keepalive_ping(void* arg, grpc_error_handle error);
static void finish_keepalive_ping(void* arg, grpc_error_handle error);
static void start_keepalive_ping_locked(void* arg, grpc_error_handle error);
static void finish_keepalive_ping_locked(void* arg, grpc_error_handle error);
static void keepalive_watchdog_fired(void* arg, grpc_error_handle error);
static void keepalive_watchdog_fired_locked(void* arg, grpc_error_handle error);

namespace grpc_core {

namespace {
TestOnlyGlobalHttp2TransportInitCallback test_only_init_callback = nullptr;
TestOnlyGlobalHttp2TransportDestructCallback test_only_destruct_callback =
    nullptr;
bool test_only_disable_transient_failure_state_notification = false;
}  // namespace

void TestOnlySetGlobalHttp2TransportInitCallback(
    TestOnlyGlobalHttp2TransportInitCallback callback) {
  test_only_init_callback = callback;
}

void TestOnlySetGlobalHttp2TransportDestructCallback(
    TestOnlyGlobalHttp2TransportDestructCallback callback) {
  test_only_destruct_callback = callback;
}

void TestOnlyGlobalHttp2TransportDisableTransientFailureStateNotification(
    bool disable) {
  test_only_disable_transient_failure_state_notification = disable;
}

}  // namespace grpc_core

//
// CONSTRUCTION/DESTRUCTION/REFCOUNTING
//

grpc_chttp2_transport::~grpc_chttp2_transport() {
  size_t i;

  if (channelz_socket != nullptr) {
    channelz_socket.reset();
  }

  grpc_endpoint_destroy(ep);

  grpc_slice_buffer_destroy_internal(&qbuf);

  grpc_slice_buffer_destroy_internal(&outbuf);

  grpc_error_handle error =
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed");
  // ContextList::Execute follows semantics of a callback function and does not
  // take a ref on error
  grpc_core::ContextList::Execute(cl, nullptr, error);
  GRPC_ERROR_UNREF(error);
  cl = nullptr;

  grpc_slice_buffer_destroy_internal(&read_buffer);
  grpc_chttp2_goaway_parser_destroy(&goaway_parser);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(lists[i].head == nullptr);
    GPR_ASSERT(lists[i].tail == nullptr);
  }

  GRPC_ERROR_UNREF(goaway_error);

  GPR_ASSERT(grpc_chttp2_stream_map_size(&stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&stream_map);

  GRPC_COMBINER_UNREF(combiner, "chttp2_transport");

  cancel_pings(this,
               GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed"));

  while (write_cb_pool) {
    grpc_chttp2_write_cb* next = write_cb_pool->next;
    gpr_free(write_cb_pool);
    write_cb_pool = next;
  }

  GRPC_ERROR_UNREF(closed_with_error);
  gpr_free(ping_acks);
  if (grpc_core::test_only_destruct_callback != nullptr) {
    grpc_core::test_only_destruct_callback();
  }
}

static const grpc_transport_vtable* get_vtable(void);

static void read_channel_args(grpc_chttp2_transport* t,
                              const grpc_core::ChannelArgs& channel_args,
                              bool is_client) {
  const int initial_sequence_number =
      channel_args.GetInt(GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER).value_or(-1);
  if (initial_sequence_number > 0) {
    if ((t->next_stream_id & 1) != (initial_sequence_number & 1)) {
      gpr_log(GPR_ERROR, "%s: low bit must be %d on %s",
              GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER, t->next_stream_id & 1,
              is_client ? "client" : "server");
    } else {
      t->next_stream_id = static_cast<uint32_t>(initial_sequence_number);
    }
  }

  const int max_hpack_table_size =
      channel_args.GetInt(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER).value_or(-1);
  if (max_hpack_table_size >= 0) {
    t->hpack_compressor.SetMaxUsableSize(max_hpack_table_size);
  }

  t->ping_policy.max_pings_without_data =
      std::max(0, channel_args.GetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA)
                      .value_or(g_default_max_pings_without_data));
  t->ping_policy.max_ping_strikes =
      std::max(0, channel_args.GetInt(GRPC_ARG_HTTP2_MAX_PING_STRIKES)
                      .value_or(g_default_max_ping_strikes));
  t->ping_policy.min_recv_ping_interval_without_data =
      std::max(grpc_core::Duration::Zero(),
               channel_args
                   .GetDurationFromIntMillis(
                       GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)
                   .value_or(grpc_core::Duration::Milliseconds(
                       g_default_min_recv_ping_interval_without_data_ms)));
  t->write_buffer_size =
      std::max(0, channel_args.GetInt(GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE)
                      .value_or(grpc_core::chttp2::kDefaultWindow));
  t->keepalive_time =
      std::max(grpc_core::Duration::Milliseconds(1),
               channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIME_MS)
                   .value_or(grpc_core::Duration::Milliseconds(
                       t->is_client ? g_default_client_keepalive_time_ms
                                    : g_default_server_keepalive_time_ms)));
  t->keepalive_timeout = std::max(
      grpc_core::Duration::Zero(),
      channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIMEOUT_MS)
          .value_or(grpc_core::Duration::Milliseconds(
              t->is_client ? g_default_client_keepalive_timeout_ms
                           : g_default_server_keepalive_timeout_ms)));
  t->keepalive_permit_without_calls =
      channel_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
          .value_or(false);

  if (channel_args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
          .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    t->channelz_socket =
        grpc_core::MakeRefCounted<grpc_core::channelz::SocketNode>(
            std::string(grpc_endpoint_get_local_address(t->ep)), t->peer_string,
            absl::StrFormat("%s %s", get_vtable()->name, t->peer_string),
            channel_args
                .GetObjectRef<grpc_core::channelz::SocketNode::Security>());
  }

  static const struct {
    absl::string_view channel_arg_name;
    grpc_chttp2_setting_id setting_id;
    int default_value;
    int min;
    int max;
    bool availability[2] /* server, client */;
  } settings_map[] = {{GRPC_ARG_MAX_CONCURRENT_STREAMS,
                       GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                       -1,
                       0,
                       INT32_MAX,
                       {true, false}},
                      {GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER,
                       GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE,
                       -1,
                       0,
                       INT32_MAX,
                       {true, true}},
                      {GRPC_ARG_MAX_METADATA_SIZE,
                       GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
                       -1,
                       0,
                       INT32_MAX,
                       {true, true}},
                      {GRPC_ARG_HTTP2_MAX_FRAME_SIZE,
                       GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
                       -1,
                       16384,
                       16777215,
                       {true, true}},
                      {GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY,
                       GRPC_CHTTP2_SETTINGS_GRPC_ALLOW_TRUE_BINARY_METADATA,
                       1,
                       0,
                       1,
                       {true, true}},
                      {GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES,
                       GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
                       -1,
                       5,
                       INT32_MAX,
                       {true, true}}};

  for (size_t i = 0; i < GPR_ARRAY_SIZE(settings_map); i++) {
    const auto& setting = settings_map[i];
    if (setting.availability[is_client]) {
      const int value = channel_args.GetInt(setting.channel_arg_name)
                            .value_or(setting.default_value);
      if (value >= 0) {
        queue_setting_update(t, setting.setting_id,
                             grpc_core::Clamp(value, setting.min, setting.max));
      }
    } else if (channel_args.Contains(setting.channel_arg_name)) {
      gpr_log(GPR_DEBUG, "%s is not available on %s",
              std::string(setting.channel_arg_name).c_str(),
              is_client ? "clients" : "servers");
    }
  }
}

static void init_transport_keepalive_settings(grpc_chttp2_transport* t) {
  if (t->is_client) {
    t->keepalive_time = g_default_client_keepalive_time_ms == INT_MAX
                            ? grpc_core::Duration::Infinity()
                            : grpc_core::Duration::Milliseconds(
                                  g_default_client_keepalive_time_ms);
    t->keepalive_timeout = g_default_client_keepalive_timeout_ms == INT_MAX
                               ? grpc_core::Duration::Infinity()
                               : grpc_core::Duration::Milliseconds(
                                     g_default_client_keepalive_timeout_ms);
    t->keepalive_permit_without_calls =
        g_default_client_keepalive_permit_without_calls;
  } else {
    t->keepalive_time = g_default_server_keepalive_time_ms == INT_MAX
                            ? grpc_core::Duration::Infinity()
                            : grpc_core::Duration::Milliseconds(
                                  g_default_server_keepalive_time_ms);
    t->keepalive_timeout = g_default_server_keepalive_timeout_ms == INT_MAX
                               ? grpc_core::Duration::Infinity()
                               : grpc_core::Duration::Milliseconds(
                                     g_default_server_keepalive_timeout_ms);
    t->keepalive_permit_without_calls =
        g_default_server_keepalive_permit_without_calls;
  }
}

static void configure_transport_ping_policy(grpc_chttp2_transport* t) {
  t->ping_policy.max_pings_without_data = g_default_max_pings_without_data;
  t->ping_policy.max_ping_strikes = g_default_max_ping_strikes;
  t->ping_policy.min_recv_ping_interval_without_data =
      grpc_core::Duration::Milliseconds(
          g_default_min_recv_ping_interval_without_data_ms);
}

static void init_keepalive_pings_if_enabled(grpc_chttp2_transport* t) {
  if (t->keepalive_time != grpc_core::Duration::Infinity()) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
    GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
    GRPC_CLOSURE_INIT(&t->init_keepalive_ping_locked, init_keepalive_ping, t,
                      grpc_schedule_on_exec_ctx);
    grpc_timer_init(&t->keepalive_ping_timer,
                    grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                    &t->init_keepalive_ping_locked);
  } else {
    // Use GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED to indicate there are no
    //   inflight keeaplive timers
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED;
  }
}

grpc_chttp2_transport::grpc_chttp2_transport(
    const grpc_core::ChannelArgs& channel_args, grpc_endpoint* ep,
    bool is_client)
    : refs(1, GRPC_TRACE_FLAG_ENABLED(grpc_trace_chttp2_refcount)
                  ? "chttp2_refcount"
                  : nullptr),
      ep(ep),
      peer_string(grpc_endpoint_get_peer(ep)),
      memory_owner(channel_args.GetObject<grpc_core::ResourceQuota>()
                       ->memory_quota()
                       ->CreateMemoryOwner(absl::StrCat(
                           grpc_endpoint_get_peer(ep), ":client_transport"))),
      self_reservation(
          memory_owner.MakeReservation(sizeof(grpc_chttp2_transport))),
      combiner(grpc_combiner_create()),
      state_tracker(is_client ? "client_transport" : "server_transport",
                    GRPC_CHANNEL_READY),
      is_client(is_client),
      next_stream_id(is_client ? 1 : 2),
      flow_control(
          peer_string.c_str(),
          channel_args.GetBool(GRPC_ARG_HTTP2_BDP_PROBE).value_or(true),
          &memory_owner),
      deframe_state(is_client ? GRPC_DTS_FH_0 : GRPC_DTS_CLIENT_PREFIX_0) {
  GPR_ASSERT(strlen(GRPC_CHTTP2_CLIENT_CONNECT_STRING) ==
             GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);
  base.vtable = get_vtable();
  // 8 is a random stab in the dark as to a good initial size: it's small enough
  //   that it shouldn't waste memory for infrequently used connections, yet
  //   large enough that the exponential growth should happen nicely when it's
  //   needed.
  //   TODO(ctiller): tune this
  grpc_chttp2_stream_map_init(&stream_map, 8);

  grpc_slice_buffer_init(&read_buffer);
  grpc_slice_buffer_init(&outbuf);
  if (is_client) {
    grpc_slice_buffer_add(&outbuf, grpc_slice_from_copied_string(
                                       GRPC_CHTTP2_CLIENT_CONNECT_STRING));
  }
  grpc_slice_buffer_init(&qbuf);
  // copy in initial settings to all setting sets
  size_t i;
  int j;
  for (i = 0; i < GRPC_CHTTP2_NUM_SETTINGS; i++) {
    for (j = 0; j < GRPC_NUM_SETTING_SETS; j++) {
      settings[j][i] = grpc_chttp2_settings_parameters[i].default_value;
    }
  }
  grpc_chttp2_goaway_parser_init(&goaway_parser);

  // configure http2 the way we like it
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

  read_channel_args(this, channel_args, is_client);

  // No pings allowed before receiving a header or data frame.
  ping_state.pings_before_data_required = 0;
  ping_state.is_delayed_ping_timer_set = false;
  ping_state.last_ping_sent_time = grpc_core::Timestamp::InfPast();

  ping_recv_state.last_ping_recv_time = grpc_core::Timestamp::InfPast();
  ping_recv_state.ping_strikes = 0;

  init_keepalive_pings_if_enabled(this);

  if (flow_control.bdp_probe()) {
    bdp_ping_blocked = true;
    grpc_chttp2_act_on_flowctl_action(flow_control.PeriodicUpdate(), this,
                                      nullptr);
  }

  grpc_chttp2_initiate_write(this, GRPC_CHTTP2_INITIATE_WRITE_INITIAL_WRITE);
  post_benign_reclaimer(this);
  if (grpc_core::test_only_init_callback != nullptr) {
    grpc_core::test_only_init_callback();
  }
}

static void destroy_transport_locked(void* tp, grpc_error_handle /*error*/) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->destroying = 1;
  close_transport_locked(
      t, grpc_error_set_int(
             GRPC_ERROR_CREATE_FROM_STATIC_STRING("Transport destroyed"),
             GRPC_ERROR_INT_OCCURRED_DURING_WRITE, t->write_state));
  t->memory_owner.Reset();
  // Must be the last line.
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "destroy");
}

static void destroy_transport(grpc_transport* gt) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  t->combiner->Run(GRPC_CLOSURE_CREATE(destroy_transport_locked, t, nullptr),
                   GRPC_ERROR_NONE);
}

static void close_transport_locked(grpc_chttp2_transport* t,
                                   grpc_error_handle error) {
  end_all_the_calls(t, GRPC_ERROR_REF(error));
  cancel_pings(t, GRPC_ERROR_REF(error));
  if (GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    if (!grpc_error_has_clear_grpc_status(error)) {
      error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                                 GRPC_STATUS_UNAVAILABLE);
    }
    if (t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE) {
      if (GRPC_ERROR_IS_NONE(t->close_transport_on_writes_finished)) {
        t->close_transport_on_writes_finished =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Delayed close due to in-progress write");
      }
      t->close_transport_on_writes_finished =
          grpc_error_add_child(t->close_transport_on_writes_finished, error);
      return;
    }
    GPR_ASSERT(!GRPC_ERROR_IS_NONE(error));
    t->closed_with_error = GRPC_ERROR_REF(error);
    connectivity_state_set(t, GRPC_CHANNEL_SHUTDOWN, absl::Status(),
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
        // keepalive timers are not set in these two states
        break;
    }

    // flush writable stream list to avoid dangling references
    grpc_chttp2_stream* s;
    while (grpc_chttp2_list_pop_writable_stream(t, &s)) {
      GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:close");
    }
    GPR_ASSERT(t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE);
    grpc_endpoint_shutdown(t->ep, GRPC_ERROR_REF(error));
  }
  if (t->notify_on_receive_settings != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, t->notify_on_receive_settings,
                            GRPC_ERROR_REF(error));
    t->notify_on_receive_settings = nullptr;
  }
  if (t->notify_on_close != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, t->notify_on_close,
                            GRPC_ERROR_REF(error));
    t->notify_on_close = nullptr;
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

grpc_chttp2_stream::Reffer::Reffer(grpc_chttp2_stream* s) {
  // We reserve one 'active stream' that's dropped when the stream is
  //   read-closed. The others are for Chttp2IncomingByteStreams that are
  //   actively reading
  GRPC_CHTTP2_STREAM_REF(s, "chttp2");
  GRPC_CHTTP2_REF_TRANSPORT(s->t, "stream");
}

grpc_chttp2_stream::grpc_chttp2_stream(grpc_chttp2_transport* t,
                                       grpc_stream_refcount* refcount,
                                       const void* server_data,
                                       grpc_core::Arena* arena)
    : t(t),
      refcount(refcount),
      reffer(this),
      initial_metadata_buffer(arena),
      trailing_metadata_buffer(arena),
      flow_control(&t->flow_control) {
  if (server_data) {
    id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(server_data));
    *t->accepting_stream = this;
    grpc_chttp2_stream_map_add(&t->stream_map, id, this);
    post_destructive_reclaimer(t);
  }

  grpc_slice_buffer_init(&frame_storage);
  grpc_slice_buffer_init(&flow_controlled_buffer);
}

grpc_chttp2_stream::~grpc_chttp2_stream() {
  grpc_chttp2_list_remove_stalled_by_stream(t, this);
  grpc_chttp2_list_remove_stalled_by_transport(t, this);

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

  grpc_slice_buffer_destroy_internal(&frame_storage);

  for (int i = 0; i < STREAM_LIST_COUNT; i++) {
    if (GPR_UNLIKELY(included.is_set(i))) {
      gpr_log(GPR_ERROR, "%s stream %d still included in list %d",
              t->is_client ? "client" : "server", id, i);
      abort();
    }
  }

  GPR_ASSERT(send_initial_metadata_finished == nullptr);
  GPR_ASSERT(send_trailing_metadata_finished == nullptr);
  GPR_ASSERT(recv_initial_metadata_ready == nullptr);
  GPR_ASSERT(recv_message_ready == nullptr);
  GPR_ASSERT(recv_trailing_metadata_finished == nullptr);
  grpc_slice_buffer_destroy_internal(&flow_controlled_buffer);
  GRPC_ERROR_UNREF(read_closed_error);
  GRPC_ERROR_UNREF(write_closed_error);
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "stream");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, destroy_stream_arg, GRPC_ERROR_NONE);
}

static int init_stream(grpc_transport* gt, grpc_stream* gs,
                       grpc_stream_refcount* refcount, const void* server_data,
                       grpc_core::Arena* arena) {
  GPR_TIMER_SCOPE("init_stream", 0);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  new (gs) grpc_chttp2_stream(t, refcount, server_data, arena);
  return 0;
}

static void destroy_stream_locked(void* sp, grpc_error_handle /*error*/) {
  GPR_TIMER_SCOPE("destroy_stream", 0);
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(sp);
  s->~grpc_chttp2_stream();
}

static void destroy_stream(grpc_transport* gt, grpc_stream* gs,
                           grpc_closure* then_schedule_closure) {
  GPR_TIMER_SCOPE("destroy_stream", 0);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_chttp2_stream* s = reinterpret_cast<grpc_chttp2_stream*>(gs);

  s->destroy_stream_arg = then_schedule_closure;
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&s->destroy_stream, destroy_stream_locked, s, nullptr),
      GRPC_ERROR_NONE);
}

grpc_chttp2_stream* grpc_chttp2_parsing_accept_stream(grpc_chttp2_transport* t,
                                                      uint32_t id) {
  if (t->accept_stream_cb == nullptr) {
    return nullptr;
  }
  grpc_chttp2_stream* accepting = nullptr;
  GPR_ASSERT(t->accepting_stream == nullptr);
  t->accepting_stream = &accepting;
  t->accept_stream_cb(t->accept_stream_cb_user_data, &t->base,
                      reinterpret_cast<void*>(id));
  t->accepting_stream = nullptr;
  return accepting;
}

//
// OUTPUT PROCESSING
//

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
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_INFO, "W:%p %s [%s] state %s -> %s [%s]", t,
              t->is_client ? "CLIENT" : "SERVER", t->peer_string.c_str(),
              write_state_name(t->write_state), write_state_name(st), reason));
  t->write_state = st;
  // If the state is being reset back to idle, it means a write was just
  // finished. Make sure all the run_after_write closures are scheduled.
  //
  // This is also our chance to close the transport if the transport was marked
  // to be closed after all writes finish (for example, if we received a go-away
  // from peer while we had some pending writes)
  if (st == GRPC_CHTTP2_WRITE_STATE_IDLE) {
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &t->run_after_write);
    if (!GRPC_ERROR_IS_NONE(t->close_transport_on_writes_finished)) {
      grpc_error_handle err = t->close_transport_on_writes_finished;
      t->close_transport_on_writes_finished = GRPC_ERROR_NONE;
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
    case GRPC_CHTTP2_INITIATE_WRITE_SETTINGS_ACK:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_SETTINGS_ACK();
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
    case GRPC_CHTTP2_INITIATE_WRITE_BDP_PING:
      GRPC_STATS_INC_HTTP2_INITIATE_WRITE_DUE_TO_BDP_ESTIMATOR_PING();
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
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      // Note that the 'write_action_begin_locked' closure is being scheduled
      // on the 'finally_scheduler' of t->combiner. This means that
      // 'write_action_begin_locked' is called only *after* all the other
      // closures (some of which are potentially initiating more writes on the
      // transport) are executed on the t->combiner.
      //
      // The reason for scheduling on finally_scheduler is to make sure we batch
      // as many writes as possible. 'write_action_begin_locked' is the function
      // that gathers all the relevant bytes (which are at various places in the
      // grpc_chttp2_transport structure) and append them to 'outbuf' field in
      // grpc_chttp2_transport thereby batching what would have been potentially
      // multiple write operations.
      //
      // Also, 'write_action_begin_locked' only gathers the bytes into outbuf.
      // It does not call the endpoint to write the bytes. That is done by the
      // 'write_action' (which is scheduled by 'write_action_begin_locked')
      t->combiner->FinallyRun(
          GRPC_CLOSURE_INIT(&t->write_action_begin_locked,
                            write_action_begin_locked, t, nullptr),
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
  if (GRPC_ERROR_IS_NONE(t->closed_with_error) &&
      grpc_chttp2_list_add_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_REF(s, "chttp2_writing:become");
  }
}

static const char* begin_writing_desc(bool partial) {
  if (partial) {
    return "begin partial write in background";
  } else {
    return "begin write in current thread";
  }
}

static void write_action_begin_locked(void* gt,
                                      grpc_error_handle /*error_ignored*/) {
  GPR_TIMER_SCOPE("write_action_begin_locked", 0);
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(gt);
  GPR_ASSERT(t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE);
  grpc_chttp2_begin_write_result r;
  if (!GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    r.writing = false;
  } else {
    r = grpc_chttp2_begin_write(t);
  }
  if (r.writing) {
    if (r.partial) {
      GRPC_STATS_INC_HTTP2_PARTIAL_WRITES();
    }
    set_write_state(t,
                    r.partial ? GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE
                              : GRPC_CHTTP2_WRITE_STATE_WRITING,
                    begin_writing_desc(r.partial));
    write_action(t, GRPC_ERROR_NONE);
    if (t->reading_paused_on_pending_induced_frames) {
      GPR_ASSERT(t->num_pending_induced_frames == 0);
      // We had paused reading, because we had many induced frames (SETTINGS
      // ACK, PINGS ACK and RST_STREAMS) pending in t->qbuf. Now that we have
      // been able to flush qbuf, we can resume reading.
      GRPC_CHTTP2_IF_TRACING(gpr_log(
          GPR_INFO,
          "transport %p : Resuming reading after being paused due to too "
          "many unwritten SETTINGS ACK, PINGS ACK and RST_STREAM frames",
          t));
      t->reading_paused_on_pending_induced_frames = false;
      continue_read_action_locked(t);
    }
  } else {
    GRPC_STATS_INC_HTTP2_SPURIOUS_WRITES_BEGUN();
    set_write_state(t, GRPC_CHTTP2_WRITE_STATE_IDLE, "begin writing nothing");
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "writing");
  }
}

static void write_action(void* gt, grpc_error_handle /*error*/) {
  GPR_TIMER_SCOPE("write_action", 0);
  static bool kEnablePeerStateBasedFraming =
      GPR_GLOBAL_CONFIG_GET(grpc_experimental_enable_peer_state_based_framing);
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(gt);
  void* cl = t->cl;
  t->cl = nullptr;
  // If grpc_experimental_enable_peer_state_based_framing is set to true,
  // choose max_frame_size as 2 * max http2 frame size of peer. If peer is under
  // high memory pressure, then it would advertise a smaller max http2 frame
  // size. With this logic, the sender would automatically reduce the sending
  // frame size as well.
  int max_frame_size =
      kEnablePeerStateBasedFraming
          ? 2 * t->settings[GRPC_PEER_SETTINGS]
                           [GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE]
          : INT_MAX;
  grpc_endpoint_write(
      t->ep, &t->outbuf,
      GRPC_CLOSURE_INIT(&t->write_action_end_locked, write_action_end, t,
                        grpc_schedule_on_exec_ctx),
      cl, max_frame_size);
}

static void write_action_end(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->write_action_end_locked,
                                     write_action_end_locked, t, nullptr),
                   GRPC_ERROR_REF(error));
}

// Callback from the grpc_endpoint after bytes have been written by calling
// sendmsg
static void write_action_end_locked(void* tp, grpc_error_handle error) {
  GPR_TIMER_SCOPE("terminate_writing_with_lock", 0);
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);

  bool closed = false;
  if (!GRPC_ERROR_IS_NONE(error)) {
    close_transport_locked(t, GRPC_ERROR_REF(error));
    closed = true;
  }

  if (t->sent_goaway_state == GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED) {
    t->sent_goaway_state = GRPC_CHTTP2_FINAL_GOAWAY_SENT;
    closed = true;
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
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      // If the transport is closed, we will retry writing on the endpoint
      // and next write may contain part of the currently serialized frames.
      // So, we should only call the run_after_write callbacks when the next
      // write finishes, or the callbacks will be invoked when the stream is
      // closed.
      if (!closed) {
        grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &t->run_after_write);
      }
      t->combiner->FinallyRun(
          GRPC_CLOSURE_INIT(&t->write_action_begin_locked,
                            write_action_begin_locked, t, nullptr),
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
  uint32_t use_value = grpc_core::Clamp(value, sp->min_value, sp->max_value);
  if (use_value != value) {
    gpr_log(GPR_INFO, "Requested parameter %s clamped from %d to %d", sp->name,
            value, use_value);
  }
  if (use_value != t->settings[GRPC_LOCAL_SETTINGS][id]) {
    t->settings[GRPC_LOCAL_SETTINGS][id] = use_value;
    t->dirtied_local_settings = true;
  }
}

// Cancel out streams that haven't yet started if we have received a GOAWAY
static void cancel_unstarted_streams(grpc_chttp2_transport* t,
                                     grpc_error_handle error) {
  grpc_chttp2_stream* s;
  while (grpc_chttp2_list_pop_waiting_for_concurrency(t, &s)) {
    s->trailing_metadata_buffer.Set(
        grpc_core::GrpcStreamNetworkState(),
        grpc_core::GrpcStreamNetworkState::kNotSentOnWire);
    grpc_chttp2_cancel_stream(t, s, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

void grpc_chttp2_add_incoming_goaway(grpc_chttp2_transport* t,
                                     uint32_t goaway_error,
                                     uint32_t last_stream_id,
                                     absl::string_view goaway_text) {
  // Discard the error from a previous goaway frame (if any)
  if (!GRPC_ERROR_IS_NONE(t->goaway_error)) {
    GRPC_ERROR_UNREF(t->goaway_error);
  }
  t->goaway_error = grpc_error_set_str(
      grpc_error_set_int(
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("GOAWAY received"),
              GRPC_ERROR_INT_HTTP2_ERROR, static_cast<intptr_t>(goaway_error)),
          GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE),
      GRPC_ERROR_STR_RAW_BYTES, goaway_text);

  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_INFO, "transport %p got goaway with last stream id %d", t,
              last_stream_id));
  // We want to log this irrespective of whether http tracing is enabled if we
  // received a GOAWAY with a non NO_ERROR code.
  if (goaway_error != GRPC_HTTP2_NO_ERROR) {
    gpr_log(GPR_INFO, "%s: Got goaway [%d] err=%s", t->peer_string.c_str(),
            goaway_error, grpc_error_std_string(t->goaway_error).c_str());
  }
  if (t->is_client) {
    cancel_unstarted_streams(t, GRPC_ERROR_REF(t->goaway_error));
    // Cancel all unseen streams
    grpc_chttp2_stream_map_for_each(
        &t->stream_map,
        [](void* user_data, uint32_t /* key */, void* stream) {
          uint32_t last_stream_id = *(static_cast<uint32_t*>(user_data));
          grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(stream);
          if (s->id > last_stream_id) {
            s->trailing_metadata_buffer.Set(
                grpc_core::GrpcStreamNetworkState(),
                grpc_core::GrpcStreamNetworkState::kNotSeenByServer);
            grpc_chttp2_cancel_stream(s->t, s,
                                      GRPC_ERROR_REF(s->t->goaway_error));
          }
        },
        &last_stream_id);
  }
  absl::Status status = grpc_error_to_absl_status(t->goaway_error);
  // When a client receives a GOAWAY with error code ENHANCE_YOUR_CALM and debug
  // data equal to "too_many_pings", it should log the occurrence at a log level
  // that is enabled by default and double the configured KEEPALIVE_TIME used
  // for new connections on that channel.
  if (GPR_UNLIKELY(t->is_client &&
                   goaway_error == GRPC_HTTP2_ENHANCE_YOUR_CALM &&
                   goaway_text == "too_many_pings")) {
    gpr_log(GPR_ERROR,
            "Received a GOAWAY with error code ENHANCE_YOUR_CALM and debug "
            "data equal to \"too_many_pings\"");
    constexpr int max_keepalive_time_millis =
        INT_MAX / KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
    int throttled_keepalive_time =
        t->keepalive_time.millis() > max_keepalive_time_millis
            ? INT_MAX
            : t->keepalive_time.millis() * KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
    status.SetPayload(grpc_core::kKeepaliveThrottlingKey,
                      absl::Cord(std::to_string(throttled_keepalive_time)));
  }
  // lie: use transient failure from the transport to indicate goaway has been
  // received.
  if (!grpc_core::test_only_disable_transient_failure_state_notification) {
    connectivity_state_set(t, GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                           "got_goaway");
  }
}

static void maybe_start_some_streams(grpc_chttp2_transport* t) {
  grpc_chttp2_stream* s;
  // maybe cancel out streams that haven't yet started if we have received a
  // GOAWAY
  if (!GRPC_ERROR_IS_NONE(t->goaway_error)) {
    cancel_unstarted_streams(t, GRPC_ERROR_REF(t->goaway_error));
    return;
  }
  // start streams where we have free grpc_chttp2_stream ids and free
  // * concurrency
  while (t->next_stream_id <= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_stream_map_size(&t->stream_map) <
             t->settings[GRPC_PEER_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS] &&
         grpc_chttp2_list_pop_waiting_for_concurrency(t, &s)) {
    // safe since we can't (legally) be parsing this stream yet
    GRPC_CHTTP2_IF_TRACING(gpr_log(
        GPR_INFO,
        "HTTP:%s: Transport %p allocating new grpc_chttp2_stream %p to id %d",
        t->is_client ? "CLI" : "SVR", t, s, t->next_stream_id));

    GPR_ASSERT(s->id == 0);
    s->id = t->next_stream_id;
    t->next_stream_id += 2;

    if (t->next_stream_id >= MAX_CLIENT_STREAM_ID) {
      connectivity_state_set(t, GRPC_CHANNEL_TRANSIENT_FAILURE,
                             absl::Status(absl::StatusCode::kUnavailable,
                                          "Transport Stream IDs exhausted"),
                             "no_more_stream_ids");
    }

    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
    post_destructive_reclaimer(t);
    grpc_chttp2_mark_stream_writable(t, s);
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_START_NEW_STREAM);
  }
  // cancel out streams that will never be started
  if (t->next_stream_id >= MAX_CLIENT_STREAM_ID) {
    while (grpc_chttp2_list_pop_waiting_for_concurrency(t, &s)) {
      s->trailing_metadata_buffer.Set(
          grpc_core::GrpcStreamNetworkState(),
          grpc_core::GrpcStreamNetworkState::kNotSentOnWire);
      grpc_chttp2_cancel_stream(
          t, s,
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("Stream IDs exhausted"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
    }
  }
}

// Flag that this closure barrier may be covering a write in a pollset, and so
//   we should not complete this closure until we can prove that the write got
//   scheduled
#define CLOSURE_BARRIER_MAY_COVER_WRITE (1 << 0)
// First bit of the reference count, stored in the high order bits (with the low
//   bits being used for flags defined above)
#define CLOSURE_BARRIER_FIRST_REF_BIT (1 << 16)

static grpc_closure* add_closure_barrier(grpc_closure* closure) {
  closure->next_data.scratch += CLOSURE_BARRIER_FIRST_REF_BIT;
  return closure;
}

static void null_then_sched_closure(grpc_closure** closure) {
  grpc_closure* c = *closure;
  *closure = nullptr;
  // null_then_schedule_closure might be run during a start_batch which might
  // subsequently examine the batch for more operations contained within.
  // However, the closure run might make it back to the call object, push a
  // completion, have the application see it, and make a new operation on the
  // call which recycles the batch BEFORE the call to start_batch completes,
  // forcing a race.
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
}

void grpc_chttp2_complete_closure_step(grpc_chttp2_transport* t,
                                       grpc_chttp2_stream* /*s*/,
                                       grpc_closure** pclosure,
                                       grpc_error_handle error,
                                       const char* desc) {
  grpc_closure* closure = *pclosure;
  *pclosure = nullptr;
  if (closure == nullptr) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  closure->next_data.scratch -= CLOSURE_BARRIER_FIRST_REF_BIT;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(
        GPR_INFO,
        "complete_closure_step: t=%p %p refs=%d flags=0x%04x desc=%s err=%s "
        "write_state=%s",
        t, closure,
        static_cast<int>(closure->next_data.scratch /
                         CLOSURE_BARRIER_FIRST_REF_BIT),
        static_cast<int>(closure->next_data.scratch %
                         CLOSURE_BARRIER_FIRST_REF_BIT),
        desc, grpc_error_std_string(error).c_str(),
        write_state_name(t->write_state));
  }
  if (!GRPC_ERROR_IS_NONE(error)) {
    grpc_error_handle cl_err =
        grpc_core::internal::StatusMoveFromHeapPtr(closure->error_data.error);
    if (GRPC_ERROR_IS_NONE(cl_err)) {
      cl_err = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Error in HTTP transport completing operation");
      cl_err = grpc_error_set_str(cl_err, GRPC_ERROR_STR_TARGET_ADDRESS,
                                  t->peer_string);
    }
    cl_err = grpc_error_add_child(cl_err, error);
    closure->error_data.error = grpc_core::internal::StatusAllocHeapPtr(cl_err);
  }
  if (closure->next_data.scratch < CLOSURE_BARRIER_FIRST_REF_BIT) {
    if ((t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE) ||
        !(closure->next_data.scratch & CLOSURE_BARRIER_MAY_COVER_WRITE)) {
      // Using GRPC_CLOSURE_SCHED instead of GRPC_CLOSURE_RUN to avoid running
      // closures earlier than when it is safe to do so.
      grpc_error_handle run_error =
          grpc_core::internal::StatusMoveFromHeapPtr(closure->error_data.error);
      closure->error_data.error = 0;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, run_error);
    } else {
      grpc_closure_list_append(&t->run_after_write, closure);
    }
  }
}

static bool contains_non_ok_status(grpc_metadata_batch* batch) {
  return batch->get(grpc_core::GrpcStatusMetadata()).value_or(GRPC_STATUS_OK) !=
         GRPC_STATUS_OK;
}

static void log_metadata(const grpc_metadata_batch* md_batch, uint32_t id,
                         bool is_client, bool is_initial) {
  const std::string prefix = absl::StrCat(
      "HTTP:", id, is_initial ? ":HDR" : ":TRL", is_client ? ":CLI:" : ":SVR:");
  md_batch->Log([&prefix](absl::string_view key, absl::string_view value) {
    gpr_log(GPR_INFO, "%s", absl::StrCat(prefix, key, ": ", value).c_str());
  });
}

static void perform_stream_op_locked(void* stream_op,
                                     grpc_error_handle /*error_ignored*/) {
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO,
            "perform_stream_op_locked[s=%p; op=%p]: %s; on_complete = %p", s,
            op, grpc_transport_stream_op_batch_string(op).c_str(),
            op->on_complete);
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
  // on_complete will be null if and only if there are no send ops in the batch.
  if (on_complete != nullptr) {
    // This batch has send ops. Use final_data as a barrier until enqueue time;
    // the initial counter is dropped at the end of this function.
    on_complete->next_data.scratch = CLOSURE_BARRIER_FIRST_REF_BIT;
    on_complete->error_data.error = 0;
  }

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

    s->send_initial_metadata_finished = add_closure_barrier(on_complete);
    s->send_initial_metadata =
        op_payload->send_initial_metadata.send_initial_metadata;
    if (t->is_client) {
      s->deadline = std::min(
          s->deadline,
          s->send_initial_metadata->get(grpc_core::GrpcTimeoutMetadata())
              .value_or(grpc_core::Timestamp::InfFuture()));
    }
    if (contains_non_ok_status(s->send_initial_metadata)) {
      s->seen_error = true;
    }
    if (!s->write_closed) {
      if (t->is_client) {
        if (GRPC_ERROR_IS_NONE(t->closed_with_error)) {
          GPR_ASSERT(s->id == 0);
          grpc_chttp2_list_add_waiting_for_concurrency(t, s);
          maybe_start_some_streams(t);
        } else {
          s->trailing_metadata_buffer.Set(
              grpc_core::GrpcStreamNetworkState(),
              grpc_core::GrpcStreamNetworkState::kNotSentOnWire);
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
              (op->payload->send_message.flags & GRPC_WRITE_BUFFER_HINT))) {
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
    if (op_payload->send_initial_metadata.peer_string != nullptr) {
      gpr_atm_rel_store(op_payload->send_initial_metadata.peer_string,
                        (gpr_atm)t->peer_string.c_str());
    }
  }

  if (op->send_message) {
    GRPC_STATS_INC_HTTP2_OP_SEND_MESSAGE();
    t->num_messages_in_next_write++;
    GRPC_STATS_INC_HTTP2_SEND_MESSAGE_SIZE(
        op->payload->send_message.send_message->Length());
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->send_message_finished = add_closure_barrier(op->on_complete);
    const uint32_t flags = op_payload->send_message.flags;
    if (s->write_closed) {
      op->payload->send_message.stream_write_closed = true;
      // We should NOT return an error here, so as to avoid a cancel OP being
      // started. The surface layer will notice that the stream has been closed
      // for writes and fail the send message op.
      grpc_chttp2_complete_closure_step(t, s, &s->send_message_finished,
                                        GRPC_ERROR_NONE,
                                        "fetching_send_message_finished");
    } else {
      uint8_t* frame_hdr = grpc_slice_buffer_tiny_add(
          &s->flow_controlled_buffer, GRPC_HEADER_SIZE_IN_BYTES);
      frame_hdr[0] = (flags & GRPC_WRITE_INTERNAL_COMPRESS) != 0;
      size_t len = op_payload->send_message.send_message->Length();
      frame_hdr[1] = static_cast<uint8_t>(len >> 24);
      frame_hdr[2] = static_cast<uint8_t>(len >> 16);
      frame_hdr[3] = static_cast<uint8_t>(len >> 8);
      frame_hdr[4] = static_cast<uint8_t>(len);

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

      grpc_slice* const slices =
          op_payload->send_message.send_message->c_slice_buffer()->slices;
      grpc_slice* const end =
          slices + op_payload->send_message.send_message->Count();
      for (grpc_slice* slice = slices; slice != end; slice++) {
        grpc_slice_buffer_add(&s->flow_controlled_buffer,
                              grpc_slice_ref_internal(*slice));
      }

      int64_t notify_offset = s->next_message_end_offset;
      if (notify_offset <= s->flow_controlled_bytes_written) {
        grpc_chttp2_complete_closure_step(t, s, &s->send_message_finished,
                                          GRPC_ERROR_NONE,
                                          "fetching_send_message_finished");
      } else {
        grpc_chttp2_write_cb* cb = t->write_cb_pool;
        if (cb == nullptr) {
          cb = static_cast<grpc_chttp2_write_cb*>(gpr_malloc(sizeof(*cb)));
        } else {
          t->write_cb_pool = cb->next;
        }
        cb->call_at_byte = notify_offset;
        cb->closure = s->send_message_finished;
        s->send_message_finished = nullptr;
        grpc_chttp2_write_cb** list = flags & GRPC_WRITE_THROUGH
                                          ? &s->on_write_finished_cbs
                                          : &s->on_flow_controlled_cbs;
        cb->next = *list;
        *list = cb;
      }

      if (s->id != 0 &&
          (!s->write_buffering ||
           s->flow_controlled_buffer.length > t->write_buffer_size)) {
        grpc_chttp2_mark_stream_writable(t, s);
        grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_SEND_MESSAGE);
      }
    }
  }

  if (op->send_trailing_metadata) {
    GRPC_STATS_INC_HTTP2_OP_SEND_TRAILING_METADATA();
    GPR_ASSERT(s->send_trailing_metadata_finished == nullptr);
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->send_trailing_metadata_finished = add_closure_barrier(on_complete);
    s->send_trailing_metadata =
        op_payload->send_trailing_metadata.send_trailing_metadata;
    s->sent_trailing_metadata_op = op_payload->send_trailing_metadata.sent;
    s->write_buffering = false;
    if (contains_non_ok_status(s->send_trailing_metadata)) {
      s->seen_error = true;
    }
    if (s->write_closed) {
      s->send_trailing_metadata = nullptr;
      s->sent_trailing_metadata_op = nullptr;
      grpc_chttp2_complete_closure_step(
          t, s, &s->send_trailing_metadata_finished,
          op->payload->send_trailing_metadata.send_trailing_metadata->empty()
              ? GRPC_ERROR_NONE
              : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Attempt to send trailing metadata after "
                    "stream was closed"),
          "send_trailing_metadata_finished");
    } else if (s->id != 0) {
      // TODO(ctiller): check if there's flow control for any outstanding
      //   bytes before going writable
      grpc_chttp2_mark_stream_writable(t, s);
      grpc_chttp2_initiate_write(
          t, GRPC_CHTTP2_INITIATE_WRITE_SEND_TRAILING_METADATA);
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
                        (gpr_atm)t->peer_string.c_str());
    }
    grpc_chttp2_maybe_complete_recv_initial_metadata(t, s);
  }

  if (op->recv_message) {
    GRPC_STATS_INC_HTTP2_OP_RECV_MESSAGE();
    GPR_ASSERT(s->recv_message_ready == nullptr);
    s->recv_message_ready = op_payload->recv_message.recv_message_ready;
    s->recv_message = op_payload->recv_message.recv_message;
    s->recv_message->emplace();
    s->recv_message_flags = op_payload->recv_message.flags;
    s->call_failed_before_recv_message =
        op_payload->recv_message.call_failed_before_recv_message;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
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

  if (on_complete != nullptr) {
    grpc_chttp2_complete_closure_step(t, s, &on_complete, GRPC_ERROR_NONE,
                                      "op->on_complete");
  }

  GRPC_CHTTP2_STREAM_UNREF(s, "perform_stream_op");
}

static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
  GPR_TIMER_SCOPE("perform_stream_op", 0);
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_chttp2_stream* s = reinterpret_cast<grpc_chttp2_stream*>(gs);

  if (!t->is_client) {
    if (op->send_initial_metadata) {
      GPR_ASSERT(!op->payload->send_initial_metadata.send_initial_metadata
                      ->get(grpc_core::GrpcTimeoutMetadata())
                      .has_value());
    }
    if (op->send_trailing_metadata) {
      GPR_ASSERT(!op->payload->send_trailing_metadata.send_trailing_metadata
                      ->get(grpc_core::GrpcTimeoutMetadata())
                      .has_value());
    }
  }

  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "perform_stream_op[s=%p; op=%p]: %s", s, op,
            grpc_transport_stream_op_batch_string(op).c_str());
  }

  GRPC_CHTTP2_STREAM_REF(s, "perform_stream_op");
  op->handler_private.extra_arg = gs;
  t->combiner->Run(GRPC_CLOSURE_INIT(&op->handler_private.closure,
                                     perform_stream_op_locked, op, nullptr),
                   GRPC_ERROR_NONE);
}

static void cancel_pings(grpc_chttp2_transport* t, grpc_error_handle error) {
  // callback remaining pings: they're not allowed to call into the transport,
  //   and maybe they hold resources that need to be freed
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  GPR_ASSERT(!GRPC_ERROR_IS_NONE(error));
  for (size_t j = 0; j < GRPC_CHTTP2_PCL_COUNT; j++) {
    grpc_closure_list_fail_all(&pq->lists[j], GRPC_ERROR_REF(error));
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &pq->lists[j]);
  }
  GRPC_ERROR_UNREF(error);
}

static void send_ping_locked(grpc_chttp2_transport* t,
                             grpc_closure* on_initiate, grpc_closure* on_ack) {
  if (!GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_initiate,
                            GRPC_ERROR_REF(t->closed_with_error));
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_ack,
                            GRPC_ERROR_REF(t->closed_with_error));
    return;
  }
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_INITIATE], on_initiate,
                           GRPC_ERROR_NONE);
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_NEXT], on_ack,
                           GRPC_ERROR_NONE);
}

// Specialized form of send_ping_locked for keepalive ping. If there is already
// a ping in progress, the keepalive ping would piggyback onto that ping,
// instead of waiting for that ping to complete and then starting a new ping.
static void send_keepalive_ping_locked(grpc_chttp2_transport* t) {
  if (!GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    t->combiner->Run(GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked,
                                       start_keepalive_ping_locked, t, nullptr),
                     GRPC_ERROR_REF(t->closed_with_error));
    t->combiner->Run(
        GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                          finish_keepalive_ping_locked, t, nullptr),
        GRPC_ERROR_REF(t->closed_with_error));
    return;
  }
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_INFLIGHT])) {
    // There is a ping in flight. Add yourself to the inflight closure list.
    t->combiner->Run(GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked,
                                       start_keepalive_ping_locked, t, nullptr),
                     GRPC_ERROR_REF(t->closed_with_error));
    grpc_closure_list_append(
        &pq->lists[GRPC_CHTTP2_PCL_INFLIGHT],
        GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                          finish_keepalive_ping, t, grpc_schedule_on_exec_ctx),
        GRPC_ERROR_NONE);
    return;
  }
  grpc_closure_list_append(
      &pq->lists[GRPC_CHTTP2_PCL_INITIATE],
      GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked, start_keepalive_ping,
                        t, grpc_schedule_on_exec_ctx),
      GRPC_ERROR_NONE);
  grpc_closure_list_append(
      &pq->lists[GRPC_CHTTP2_PCL_NEXT],
      GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked, finish_keepalive_ping,
                        t, grpc_schedule_on_exec_ctx),
      GRPC_ERROR_NONE);
}

void grpc_chttp2_retry_initiate_ping(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->retry_initiate_ping_locked,
                                     retry_initiate_ping_locked, t, nullptr),
                   GRPC_ERROR_REF(error));
}

static void retry_initiate_ping_locked(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->ping_state.is_delayed_ping_timer_set = false;
  if (GRPC_ERROR_IS_NONE(error)) {
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING);
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "retry_initiate_ping_locked");
}

void grpc_chttp2_ack_ping(grpc_chttp2_transport* t, uint64_t id) {
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (pq->inflight_id != id) {
    gpr_log(GPR_DEBUG, "Unknown ping response from %s: %" PRIx64,
            t->peer_string.c_str(), id);
    return;
  }
  grpc_core::ExecCtx::RunList(DEBUG_LOCATION,
                              &pq->lists[GRPC_CHTTP2_PCL_INFLIGHT]);
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_NEXT])) {
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_CONTINUE_PINGS);
  }
}

namespace {

// Fire and forget (deletes itself on completion). Does a graceful shutdown by
// sending a GOAWAY frame with the last stream id set to 2^31-1, sending a ping
// and waiting for an ack (effective waiting for an RTT) and then sending a
// final GOAWAY freame with an updated last stream identifier. This helps ensure
// that a connection can be cleanly shut down without losing requests.
// In the event, that the client does not respond to the ping for some reason,
// we add a 20 second deadline, after which we send the second goaway.
class GracefulGoaway : public grpc_core::RefCounted<GracefulGoaway> {
 public:
  static void Start(grpc_chttp2_transport* t) { new GracefulGoaway(t); }

  ~GracefulGoaway() override {
    GRPC_CHTTP2_UNREF_TRANSPORT(t_, "graceful goaway");
  }

 private:
  explicit GracefulGoaway(grpc_chttp2_transport* t) : t_(t) {
    t->sent_goaway_state = GRPC_CHTTP2_GRACEFUL_GOAWAY;
    GRPC_CHTTP2_REF_TRANSPORT(t_, "graceful goaway");
    grpc_chttp2_goaway_append((1u << 31) - 1, 0, grpc_empty_slice(), &t->qbuf);
    send_ping_locked(
        t, nullptr, GRPC_CLOSURE_INIT(&on_ping_ack_, OnPingAck, this, nullptr));
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
    Ref().release();  // Ref for the timer
    grpc_timer_init(
        &timer_,
        grpc_core::ExecCtx::Get()->Now() + grpc_core::Duration::Seconds(20),
        GRPC_CLOSURE_INIT(&on_timer_, OnTimer, this, nullptr));
  }

  void MaybeSendFinalGoawayLocked() {
    if (t_->sent_goaway_state != GRPC_CHTTP2_GRACEFUL_GOAWAY) {
      // We already sent the final GOAWAY.
      return;
    }
    if (t_->destroying || !GRPC_ERROR_IS_NONE(t_->closed_with_error)) {
      GRPC_CHTTP2_IF_TRACING(gpr_log(
          GPR_INFO,
          "transport:%p %s peer:%s Transport already shutting down. "
          "Graceful GOAWAY abandoned.",
          t_, t_->is_client ? "CLIENT" : "SERVER", t_->peer_string.c_str()));
      return;
    }
    // Ping completed. Send final goaway.
    GRPC_CHTTP2_IF_TRACING(
        gpr_log(GPR_INFO,
                "transport:%p %s peer:%s Graceful shutdown: Ping received. "
                "Sending final GOAWAY with stream_id:%d",
                t_, t_->is_client ? "CLIENT" : "SERVER",
                t_->peer_string.c_str(), t_->last_new_stream_id));
    t_->sent_goaway_state = GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED;
    grpc_chttp2_goaway_append(t_->last_new_stream_id, 0, grpc_empty_slice(),
                              &t_->qbuf);
    grpc_chttp2_initiate_write(t_, GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
  }

  static void OnPingAck(void* arg, grpc_error_handle /* error */) {
    auto* self = static_cast<GracefulGoaway*>(arg);
    self->t_->combiner->Run(
        GRPC_CLOSURE_INIT(&self->on_ping_ack_, OnPingAckLocked, self, nullptr),
        GRPC_ERROR_NONE);
  }

  static void OnPingAckLocked(void* arg, grpc_error_handle /* error */) {
    auto* self = static_cast<GracefulGoaway*>(arg);
    grpc_timer_cancel(&self->timer_);
    self->MaybeSendFinalGoawayLocked();
    self->Unref();
  }

  static void OnTimer(void* arg, grpc_error_handle error) {
    auto* self = static_cast<GracefulGoaway*>(arg);
    if (!GRPC_ERROR_IS_NONE(error)) {
      self->Unref();
      return;
    }
    self->t_->combiner->Run(
        GRPC_CLOSURE_INIT(&self->on_timer_, OnTimerLocked, self, nullptr),
        GRPC_ERROR_NONE);
  }

  static void OnTimerLocked(void* arg, grpc_error_handle /* error */) {
    auto* self = static_cast<GracefulGoaway*>(arg);
    self->MaybeSendFinalGoawayLocked();
    self->Unref();
  }

  grpc_chttp2_transport* t_;
  grpc_closure on_ping_ack_;
  grpc_timer timer_;
  grpc_closure on_timer_;
};

}  // namespace

static void send_goaway(grpc_chttp2_transport* t, grpc_error_handle error,
                        bool immediate_disconnect_hint) {
  grpc_http2_error_code http_error;
  std::string message;
  grpc_error_get_status(error, grpc_core::Timestamp::InfFuture(), nullptr,
                        &message, &http_error, nullptr);
  if (!t->is_client && http_error == GRPC_HTTP2_NO_ERROR &&
      !immediate_disconnect_hint) {
    // Do a graceful shutdown.
    if (t->sent_goaway_state == GRPC_CHTTP2_NO_GOAWAY_SEND) {
      GracefulGoaway::Start(t);
    } else {
      // Graceful GOAWAY is already in progress.
    }
  } else if (t->sent_goaway_state == GRPC_CHTTP2_NO_GOAWAY_SEND ||
             t->sent_goaway_state == GRPC_CHTTP2_GRACEFUL_GOAWAY) {
    // We want to log this irrespective of whether http tracing is enabled
    gpr_log(GPR_DEBUG, "%s: Sending goaway err=%s", t->peer_string.c_str(),
            grpc_error_std_string(error).c_str());
    t->sent_goaway_state = GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED;
    grpc_chttp2_goaway_append(
        t->last_new_stream_id, static_cast<uint32_t>(http_error),
        grpc_slice_from_cpp_string(std::move(message)), &t->qbuf);
  } else {
    // Final GOAWAY has already been sent.
  }
  grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
  GRPC_ERROR_UNREF(error);
}

void grpc_chttp2_add_ping_strike(grpc_chttp2_transport* t) {
  if (++t->ping_recv_state.ping_strikes > t->ping_policy.max_ping_strikes &&
      t->ping_policy.max_ping_strikes != 0) {
    send_goaway(t,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("too_many_pings"),
                    GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_ENHANCE_YOUR_CALM),
                /*immediate_disconnect_hint=*/true);
    // The transport will be closed after the write is done
    close_transport_locked(
        t, grpc_error_set_int(
               GRPC_ERROR_CREATE_FROM_STATIC_STRING("Too many pings"),
               GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
  }
}

void grpc_chttp2_reset_ping_clock(grpc_chttp2_transport* t) {
  if (!t->is_client) {
    t->ping_recv_state.last_ping_recv_time = grpc_core::Timestamp::InfPast();
    t->ping_recv_state.ping_strikes = 0;
  }
  t->ping_state.pings_before_data_required =
      t->ping_policy.max_pings_without_data;
}

static void perform_transport_op_locked(void* stream_op,
                                        grpc_error_handle /*error_ignored*/) {
  grpc_transport_op* op = static_cast<grpc_transport_op*>(stream_op);
  grpc_chttp2_transport* t =
      static_cast<grpc_chttp2_transport*>(op->handler_private.extra_arg);

  if (!GRPC_ERROR_IS_NONE(op->goaway_error)) {
    send_goaway(t, op->goaway_error, /*immediate_disconnect_hint=*/false);
  }

  if (op->set_accept_stream) {
    t->accept_stream_cb = op->set_accept_stream_fn;
    t->accept_stream_cb_user_data = op->set_accept_stream_user_data;
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

  if (op->start_connectivity_watch != nullptr) {
    t->state_tracker.AddWatcher(op->start_connectivity_watch_state,
                                std::move(op->start_connectivity_watch));
  }
  if (op->stop_connectivity_watch != nullptr) {
    t->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
  }

  if (!GRPC_ERROR_IS_NONE(op->disconnect_with_error)) {
    send_goaway(t, GRPC_ERROR_REF(op->disconnect_with_error),
                /*immediate_disconnect_hint=*/true);
    close_transport_locked(t, op->disconnect_with_error);
  }

  grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);

  GRPC_CHTTP2_UNREF_TRANSPORT(t, "transport_op");
}

static void perform_transport_op(grpc_transport* gt, grpc_transport_op* op) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "perform_transport_op[t=%p]: %s", t,
            grpc_transport_op_string(op).c_str());
  }
  op->handler_private.extra_arg = gt;
  GRPC_CHTTP2_REF_TRANSPORT(t, "transport_op");
  t->combiner->Run(GRPC_CLOSURE_INIT(&op->handler_private.closure,
                                     perform_transport_op_locked, op, nullptr),
                   GRPC_ERROR_NONE);
}

//
// INPUT PROCESSING - GENERAL
//

void grpc_chttp2_maybe_complete_recv_initial_metadata(grpc_chttp2_transport* t,
                                                      grpc_chttp2_stream* s) {
  if (s->recv_initial_metadata_ready != nullptr &&
      s->published_metadata[0] != GRPC_METADATA_NOT_PUBLISHED) {
    if (s->seen_error) {
      grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
    }
    *s->recv_initial_metadata = std::move(s->initial_metadata_buffer);
    s->recv_initial_metadata->Set(grpc_core::PeerString(), t->peer_string);
    // If we didn't receive initial metadata from the wire and instead faked a
    // status (due to stream cancellations for example), let upper layers know
    // that trailing metadata is immediately available.
    if (s->trailing_metadata_available != nullptr &&
        s->published_metadata[0] != GRPC_METADATA_PUBLISHED_FROM_WIRE &&
        s->published_metadata[1] == GRPC_METADATA_SYNTHESIZED_FROM_FAKE) {
      *s->trailing_metadata_available = true;
      s->trailing_metadata_available = nullptr;
    }
    null_then_sched_closure(&s->recv_initial_metadata_ready);
  }
}

void grpc_chttp2_maybe_complete_recv_message(grpc_chttp2_transport* t,
                                             grpc_chttp2_stream* s) {
  if (s->recv_message_ready == nullptr) return;

  grpc_core::chttp2::StreamFlowControl::IncomingUpdateContext upd(
      &s->flow_control);
  grpc_error_handle error = GRPC_ERROR_NONE;

  // Lambda is immediately invoked as a big scoped section that can be
  // exited out of at any point by returning.
  [&]() {
    if (s->final_metadata_requested && s->seen_error) {
      grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
      s->recv_message->reset();
    } else {
      if (s->frame_storage.length != 0) {
        while (true) {
          GPR_ASSERT(s->frame_storage.length > 0);
          uint32_t min_progress_size;
          auto r = grpc_deframe_unprocessed_incoming_frames(
              s, &min_progress_size, &**s->recv_message, s->recv_message_flags);
          if (absl::holds_alternative<grpc_core::Pending>(r)) {
            if (s->read_closed) {
              grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
              s->recv_message->reset();
              break;
            } else {
              upd.SetMinProgressSize(min_progress_size);
              return;  // Out of lambda to enclosing function
            }
          } else {
            error = absl::get<grpc_error_handle>(r);
            if (!GRPC_ERROR_IS_NONE(error)) {
              s->seen_error = true;
              grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
              break;
            } else {
              if (t->channelz_socket != nullptr) {
                t->channelz_socket->RecordMessageReceived();
              }
              break;
            }
          }
        }
      } else if (s->read_closed) {
        s->recv_message->reset();
      } else {
        upd.SetMinProgressSize(GRPC_HEADER_SIZE_IN_BYTES);
        return;  // Out of lambda to enclosing function
      }
    }
    // save the length of the buffer before handing control back to application
    // threads. Needed to support correct flow control bookkeeping
    if (GRPC_ERROR_IS_NONE(error) && s->recv_message->has_value()) {
      null_then_sched_closure(&s->recv_message_ready);
    } else if (s->published_metadata[1] != GRPC_METADATA_NOT_PUBLISHED) {
      if (s->call_failed_before_recv_message != nullptr) {
        *s->call_failed_before_recv_message =
            (s->published_metadata[1] != GRPC_METADATA_PUBLISHED_AT_CLOSE);
      }
      null_then_sched_closure(&s->recv_message_ready);
    }
    GRPC_ERROR_UNREF(error);
  }();

  upd.SetPendingSize(s->frame_storage.length);
  grpc_chttp2_act_on_flowctl_action(upd.MakeAction(), t, s);
}

void grpc_chttp2_maybe_complete_recv_trailing_metadata(grpc_chttp2_transport* t,
                                                       grpc_chttp2_stream* s) {
  grpc_chttp2_maybe_complete_recv_message(t, s);
  if (s->recv_trailing_metadata_finished != nullptr && s->read_closed &&
      s->write_closed) {
    if (s->seen_error || !t->is_client) {
      grpc_slice_buffer_reset_and_unref_internal(&s->frame_storage);
    }
    if (s->read_closed && s->frame_storage.length == 0 &&
        s->recv_trailing_metadata_finished != nullptr) {
      grpc_transport_move_stats(&s->stats, s->collecting_stats);
      s->collecting_stats = nullptr;
      *s->recv_trailing_metadata = std::move(s->trailing_metadata_buffer);
      s->recv_trailing_metadata->Set(grpc_core::PeerString(), t->peer_string);
      null_then_sched_closure(&s->recv_trailing_metadata_finished);
    }
  }
}

static void remove_stream(grpc_chttp2_transport* t, uint32_t id,
                          grpc_error_handle error) {
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(
      grpc_chttp2_stream_map_delete(&t->stream_map, id));
  GPR_DEBUG_ASSERT(s);
  if (t->incoming_stream == s) {
    t->incoming_stream = nullptr;
    grpc_chttp2_parsing_become_skip_parser(t);
  }

  if (grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    post_benign_reclaimer(t);
    if (t->sent_goaway_state == GRPC_CHTTP2_FINAL_GOAWAY_SENT) {
      close_transport_locked(
          t, GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                 "Last stream closed after sending GOAWAY", &error, 1));
    }
  }
  if (grpc_chttp2_list_remove_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:remove_stream");
  }
  grpc_chttp2_list_remove_stalled_by_stream(t, s);
  grpc_chttp2_list_remove_stalled_by_transport(t, s);

  GRPC_ERROR_UNREF(error);

  maybe_start_some_streams(t);
}

void grpc_chttp2_cancel_stream(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                               grpc_error_handle due_to_error) {
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
      grpc_chttp2_add_rst_stream_to_next_write(
          t, s->id, static_cast<uint32_t>(http_error), &s->stats.outgoing);
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RST_STREAM);
    }
  }
  if (!GRPC_ERROR_IS_NONE(due_to_error) && !s->seen_error) {
    s->seen_error = true;
  }
  grpc_chttp2_mark_stream_closed(t, s, 1, 1, due_to_error);
}

void grpc_chttp2_fake_status(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                             grpc_error_handle error) {
  grpc_status_code status;
  std::string message;
  grpc_error_get_status(error, s->deadline, &status, &message, nullptr,
                        nullptr);
  if (status != GRPC_STATUS_OK) {
    s->seen_error = true;
  }
  // stream_global->recv_trailing_metadata_finished gives us a
  //   last chance replacement: we've received trailing metadata,
  //   but something more important has become available to signal
  //   to the upper layers - drop what we've got, and then publish
  //   what we want - which is safe because we haven't told anyone
  //   about the metadata yet
  if (s->published_metadata[1] == GRPC_METADATA_NOT_PUBLISHED ||
      s->recv_trailing_metadata_finished != nullptr) {
    s->trailing_metadata_buffer.Set(grpc_core::GrpcStatusMetadata(), status);
    if (!message.empty()) {
      s->trailing_metadata_buffer.Set(
          grpc_core::GrpcMessageMetadata(),
          grpc_core::Slice::FromCopiedBuffer(message));
    }
    s->published_metadata[1] = GRPC_METADATA_SYNTHESIZED_FROM_FAKE;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
  }

  GRPC_ERROR_UNREF(error);
}

static void add_error(grpc_error_handle error, grpc_error_handle* refs,
                      size_t* nrefs) {
  if (GRPC_ERROR_IS_NONE(error)) return;
  for (size_t i = 0; i < *nrefs; i++) {
    if (error == refs[i]) {
      return;
    }
  }
  refs[*nrefs] = error;
  ++*nrefs;
}

static grpc_error_handle removal_error(grpc_error_handle extra_error,
                                       grpc_chttp2_stream* s,
                                       const char* main_error_msg) {
  grpc_error_handle refs[3];
  size_t nrefs = 0;
  add_error(s->read_closed_error, refs, &nrefs);
  add_error(s->write_closed_error, refs, &nrefs);
  add_error(extra_error, refs, &nrefs);
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (nrefs > 0) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(main_error_msg,
                                                             refs, nrefs);
  }
  GRPC_ERROR_UNREF(extra_error);
  return error;
}

static void flush_write_list(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                             grpc_chttp2_write_cb** list,
                             grpc_error_handle error) {
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
                                     grpc_chttp2_stream* s,
                                     grpc_error_handle error) {
  error =
      removal_error(error, s, "Pending writes failed due to stream closure");
  s->send_initial_metadata = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_initial_metadata_finished,
                                    GRPC_ERROR_REF(error),
                                    "send_initial_metadata_finished");

  s->send_trailing_metadata = nullptr;
  s->sent_trailing_metadata_op = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_trailing_metadata_finished,
                                    GRPC_ERROR_REF(error),
                                    "send_trailing_metadata_finished");

  grpc_chttp2_complete_closure_step(t, s, &s->send_message_finished,
                                    GRPC_ERROR_REF(error),
                                    "fetching_send_message_finished");
  flush_write_list(t, s, &s->on_write_finished_cbs, GRPC_ERROR_REF(error));
  flush_write_list(t, s, &s->on_flow_controlled_cbs, error);
}

void grpc_chttp2_mark_stream_closed(grpc_chttp2_transport* t,
                                    grpc_chttp2_stream* s, int close_reads,
                                    int close_writes, grpc_error_handle error) {
  if (s->read_closed && s->write_closed) {
    // already closed, but we should still fake the status if needed.
    grpc_error_handle overall_error = removal_error(error, s, "Stream removed");
    if (!GRPC_ERROR_IS_NONE(overall_error)) {
      grpc_chttp2_fake_status(t, s, overall_error);
    }
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
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
    grpc_error_handle overall_error =
        removal_error(GRPC_ERROR_REF(error), s, "Stream removed");
    if (s->id != 0) {
      remove_stream(t, s->id, GRPC_ERROR_REF(overall_error));
    } else {
      // Purge streams waiting on concurrency still waiting for id assignment
      grpc_chttp2_list_remove_waiting_for_concurrency(t, s);
    }
    if (!GRPC_ERROR_IS_NONE(overall_error)) {
      grpc_chttp2_fake_status(t, s, overall_error);
    }
  }
  if (closed_read) {
    for (int i = 0; i < 2; i++) {
      if (s->published_metadata[i] == GRPC_METADATA_NOT_PUBLISHED) {
        s->published_metadata[i] = GRPC_METADATA_PUBLISHED_AT_CLOSE;
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
                           grpc_error_handle error) {
  grpc_slice hdr;
  grpc_slice status_hdr;
  grpc_slice http_status_hdr;
  grpc_slice content_type_hdr;
  grpc_slice message_pfx;
  uint8_t* p;
  uint32_t len = 0;
  grpc_status_code grpc_status;
  std::string message;
  grpc_error_get_status(error, s->deadline, &grpc_status, &message, nullptr,
                        nullptr);

  GPR_ASSERT(grpc_status >= 0 && (int)grpc_status < 100);

  // Hand roll a header block.
  //   This is unnecessarily ugly - at some point we should find a more
  //   elegant solution.
  //   It's complicated by the fact that our send machinery would be dead by
  //   the time we got around to sending this, so instead we ignore HPACK
  //   compression and just write the uncompressed bytes onto the wire.
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

  size_t msg_len = message.length();
  GPR_ASSERT(msg_len <= UINT32_MAX);
  grpc_core::VarintWriter<1> msg_len_writer(msg_len);
  message_pfx = GRPC_SLICE_MALLOC(14 + msg_len_writer.length());
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
  msg_len_writer.Write(0, p);
  p += msg_len_writer.length();
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
  grpc_slice_buffer_add(&t->qbuf,
                        grpc_slice_from_cpp_string(std::move(message)));
  grpc_chttp2_reset_ping_clock(t);
  grpc_chttp2_add_rst_stream_to_next_write(t, s->id, GRPC_HTTP2_NO_ERROR,
                                           &s->stats.outgoing);

  grpc_chttp2_mark_stream_closed(t, s, 1, 1, error);
  grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_CLOSE_FROM_API);
}

struct cancel_stream_cb_args {
  grpc_error_handle error;
  grpc_chttp2_transport* t;
};

static void cancel_stream_cb(void* user_data, uint32_t /*key*/, void* stream) {
  cancel_stream_cb_args* args = static_cast<cancel_stream_cb_args*>(user_data);
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(stream);
  grpc_chttp2_cancel_stream(args->t, s, GRPC_ERROR_REF(args->error));
}

static void end_all_the_calls(grpc_chttp2_transport* t,
                              grpc_error_handle error) {
  intptr_t http2_error;
  // If there is no explicit grpc or HTTP/2 error, set to UNAVAILABLE on server.
  if (!t->is_client && !grpc_error_has_clear_grpc_status(error) &&
      !grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &http2_error)) {
    error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                               GRPC_STATUS_UNAVAILABLE);
  }
  cancel_unstarted_streams(t, GRPC_ERROR_REF(error));
  cancel_stream_cb_args args = {error, t};
  grpc_chttp2_stream_map_for_each(&t->stream_map, cancel_stream_cb, &args);
  GRPC_ERROR_UNREF(error);
}

//
// INPUT PROCESSING - PARSING
//

template <class F>
static void WithUrgency(grpc_chttp2_transport* t,
                        grpc_core::chttp2::FlowControlAction::Urgency urgency,
                        grpc_chttp2_initiate_write_reason reason, F action) {
  switch (urgency) {
    case grpc_core::chttp2::FlowControlAction::Urgency::NO_ACTION_NEEDED:
      break;
    case grpc_core::chttp2::FlowControlAction::Urgency::UPDATE_IMMEDIATELY:
      grpc_chttp2_initiate_write(t, reason);
      ABSL_FALLTHROUGH_INTENDED;
    case grpc_core::chttp2::FlowControlAction::Urgency::QUEUE_UPDATE:
      action();
      break;
  }
}

void grpc_chttp2_act_on_flowctl_action(
    const grpc_core::chttp2::FlowControlAction& action,
    grpc_chttp2_transport* t, grpc_chttp2_stream* s) {
  WithUrgency(t, action.send_stream_update(),
              GRPC_CHTTP2_INITIATE_WRITE_STREAM_FLOW_CONTROL, [t, s]() {
                if (s->id != 0) {
                  grpc_chttp2_mark_stream_writable(t, s);
                }
              });
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

static grpc_error_handle try_http_parsing(grpc_chttp2_transport* t) {
  grpc_http_parser parser;
  size_t i = 0;
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_http_response response;

  grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, &response);

  grpc_error_handle parse_error = GRPC_ERROR_NONE;
  for (; i < t->read_buffer.count && GRPC_ERROR_IS_NONE(parse_error); i++) {
    parse_error =
        grpc_http_parser_parse(&parser, t->read_buffer.slices[i], nullptr);
  }
  if (GRPC_ERROR_IS_NONE(parse_error) &&
      (parse_error = grpc_http_parser_eof(&parser)) == GRPC_ERROR_NONE) {
    error = grpc_error_set_int(
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                               "Trying to connect an http1.x server"),
                           GRPC_ERROR_INT_HTTP_STATUS, response.status),
        GRPC_ERROR_INT_GRPC_STATUS,
        grpc_http2_status_to_grpc_status(response.status));
  }
  GRPC_ERROR_UNREF(parse_error);

  grpc_http_parser_destroy(&parser);
  grpc_http_response_destroy(&response);
  return error;
}

static void read_action(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_locked, t, nullptr),
      GRPC_ERROR_REF(error));
}

static void read_action_locked(void* tp, grpc_error_handle error) {
  GPR_TIMER_SCOPE("reading_action_locked", 0);

  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);

  (void)GRPC_ERROR_REF(error);

  grpc_error_handle err = error;
  if (!GRPC_ERROR_IS_NONE(err)) {
    err = grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
                                 "Endpoint read failed", &err, 1),
                             GRPC_ERROR_INT_OCCURRED_DURING_WRITE,
                             t->write_state);
  }
  std::swap(err, error);
  GRPC_ERROR_UNREF(err);
  if (GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    GPR_TIMER_SCOPE("reading_action.parse", 0);
    size_t i = 0;
    grpc_error_handle errors[3] = {GRPC_ERROR_REF(error), GRPC_ERROR_NONE,
                                   GRPC_ERROR_NONE};
    for (; i < t->read_buffer.count && errors[1] == GRPC_ERROR_NONE; i++) {
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
  if (GRPC_ERROR_IS_NONE(error) && !GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Transport closed", &t->closed_with_error, 1);
  }
  if (!GRPC_ERROR_IS_NONE(error)) {
    // If a goaway frame was received, this might be the reason why the read
    // failed. Add this info to the error
    if (!GRPC_ERROR_IS_NONE(t->goaway_error)) {
      error = grpc_error_add_child(error, GRPC_ERROR_REF(t->goaway_error));
    }

    close_transport_locked(t, GRPC_ERROR_REF(error));
    t->endpoint_reading = 0;
  } else if (GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    keep_reading = true;
    // Since we have read a byte, reset the keepalive timer
    if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING) {
      grpc_timer_cancel(&t->keepalive_ping_timer);
    }
  }
  grpc_slice_buffer_reset_and_unref_internal(&t->read_buffer);

  if (keep_reading) {
    if (t->num_pending_induced_frames >= DEFAULT_MAX_PENDING_INDUCED_FRAMES) {
      t->reading_paused_on_pending_induced_frames = true;
      GRPC_CHTTP2_IF_TRACING(
          gpr_log(GPR_INFO,
                  "transport %p : Pausing reading due to too "
                  "many unwritten SETTINGS ACK and RST_STREAM frames",
                  t));
    } else {
      continue_read_action_locked(t);
    }
  } else {
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "reading_action");
  }

  GRPC_ERROR_UNREF(error);
}

static void continue_read_action_locked(grpc_chttp2_transport* t) {
  const bool urgent = !GRPC_ERROR_IS_NONE(t->goaway_error);
  GRPC_CLOSURE_INIT(&t->read_action_locked, read_action, t,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(t->ep, &t->read_buffer, &t->read_action_locked, urgent,
                     /*min_progress_size=*/1);
}

// t is reffed prior to calling the first time, and once the callback chain
// that kicks off finishes, it's unreffed
void schedule_bdp_ping_locked(grpc_chttp2_transport* t) {
  t->flow_control.bdp_estimator()->SchedulePing();
  send_ping_locked(
      t,
      GRPC_CLOSURE_INIT(&t->start_bdp_ping_locked, start_bdp_ping, t,
                        grpc_schedule_on_exec_ctx),
      GRPC_CLOSURE_INIT(&t->finish_bdp_ping_locked, finish_bdp_ping, t,
                        grpc_schedule_on_exec_ctx));
  grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_BDP_PING);
}

static void start_bdp_ping(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->start_bdp_ping_locked,
                                     start_bdp_ping_locked, t, nullptr),
                   GRPC_ERROR_REF(error));
}

static void start_bdp_ping_locked(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "%s: Start BDP ping err=%s", t->peer_string.c_str(),
            grpc_error_std_string(error).c_str());
  }
  if (!GRPC_ERROR_IS_NONE(error) || !GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    return;
  }
  // Reset the keepalive ping timer
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING) {
    grpc_timer_cancel(&t->keepalive_ping_timer);
  }
  t->flow_control.bdp_estimator()->StartPing();
  t->bdp_ping_started = true;
}

static void finish_bdp_ping(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->finish_bdp_ping_locked,
                                     finish_bdp_ping_locked, t, nullptr),
                   GRPC_ERROR_REF(error));
}

static void finish_bdp_ping_locked(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "%s: Complete BDP ping err=%s", t->peer_string.c_str(),
            grpc_error_std_string(error).c_str());
  }
  if (!GRPC_ERROR_IS_NONE(error) || !GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "bdp_ping");
    return;
  }
  if (!t->bdp_ping_started) {
    // start_bdp_ping_locked has not been run yet. Schedule
    // finish_bdp_ping_locked to be run later.
    t->combiner->Run(GRPC_CLOSURE_INIT(&t->finish_bdp_ping_locked,
                                       finish_bdp_ping_locked, t, nullptr),
                     GRPC_ERROR_REF(error));
    return;
  }
  t->bdp_ping_started = false;
  grpc_core::Timestamp next_ping =
      t->flow_control.bdp_estimator()->CompletePing();
  grpc_chttp2_act_on_flowctl_action(t->flow_control.PeriodicUpdate(), t,
                                    nullptr);
  GPR_ASSERT(!t->have_next_bdp_ping_timer);
  t->have_next_bdp_ping_timer = true;
  GRPC_CLOSURE_INIT(&t->next_bdp_ping_timer_expired_locked,
                    next_bdp_ping_timer_expired, t, grpc_schedule_on_exec_ctx);
  grpc_timer_init(&t->next_bdp_ping_timer, next_ping,
                  &t->next_bdp_ping_timer_expired_locked);
}

static void next_bdp_ping_timer_expired(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&t->next_bdp_ping_timer_expired_locked,
                        next_bdp_ping_timer_expired_locked, t, nullptr),
      GRPC_ERROR_REF(error));
}

static void next_bdp_ping_timer_expired_locked(void* tp,
                                               grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  GPR_ASSERT(t->have_next_bdp_ping_timer);
  t->have_next_bdp_ping_timer = false;
  if (!GRPC_ERROR_IS_NONE(error)) {
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "bdp_ping");
    return;
  }
  if (t->flow_control.bdp_estimator()->accumulator() == 0) {
    // Block the bdp ping till we receive more data.
    t->bdp_ping_blocked = true;
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "bdp_ping");
  } else {
    schedule_bdp_ping_locked(t);
  }
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
                     GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)) {
        g_default_min_recv_ping_interval_without_data_ms =
            grpc_channel_arg_get_integer(
                &args->args[i],
                {g_default_min_recv_ping_interval_without_data_ms, 0, INT_MAX});
      }
    }
  }
}

static void init_keepalive_ping(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->init_keepalive_ping_locked,
                                     init_keepalive_ping_locked, t, nullptr),
                   GRPC_ERROR_REF(error));
}

static void init_keepalive_ping_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  GPR_ASSERT(t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING);
  if (t->destroying || !GRPC_ERROR_IS_NONE(t->closed_with_error)) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
  } else if (GRPC_ERROR_IS_NONE(error)) {
    if (t->keepalive_permit_without_calls ||
        grpc_chttp2_stream_map_size(&t->stream_map) > 0) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_PINGING;
      GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive ping end");
      grpc_timer_init_unset(&t->keepalive_watchdog_timer);
      send_keepalive_ping_locked(t);
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING);
    } else {
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      GRPC_CLOSURE_INIT(&t->init_keepalive_ping_locked, init_keepalive_ping, t,
                        grpc_schedule_on_exec_ctx);
      grpc_timer_init(&t->keepalive_ping_timer,
                      grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                      &t->init_keepalive_ping_locked);
    }
  } else if (error == GRPC_ERROR_CANCELLED) {
    // The keepalive ping timer may be cancelled by bdp
    GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
    GRPC_CLOSURE_INIT(&t->init_keepalive_ping_locked, init_keepalive_ping, t,
                      grpc_schedule_on_exec_ctx);
    grpc_timer_init(&t->keepalive_ping_timer,
                    grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                    &t->init_keepalive_ping_locked);
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "init keepalive ping");
}

static void start_keepalive_ping(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked,
                                     start_keepalive_ping_locked, t, nullptr),
                   GRPC_ERROR_REF(error));
}

static void start_keepalive_ping_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (!GRPC_ERROR_IS_NONE(error)) {
    return;
  }
  if (t->channelz_socket != nullptr) {
    t->channelz_socket->RecordKeepaliveSent();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace) ||
      GRPC_TRACE_FLAG_ENABLED(grpc_keepalive_trace)) {
    gpr_log(GPR_INFO, "%s: Start keepalive ping", t->peer_string.c_str());
  }
  GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive watchdog");
  GRPC_CLOSURE_INIT(&t->keepalive_watchdog_fired_locked,
                    keepalive_watchdog_fired, t, grpc_schedule_on_exec_ctx);
  grpc_timer_init(&t->keepalive_watchdog_timer,
                  grpc_core::ExecCtx::Get()->Now() + t->keepalive_timeout,
                  &t->keepalive_watchdog_fired_locked);
  t->keepalive_ping_started = true;
}

static void finish_keepalive_ping(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                                     finish_keepalive_ping_locked, t, nullptr),
                   GRPC_ERROR_REF(error));
}

static void finish_keepalive_ping_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    if (GRPC_ERROR_IS_NONE(error)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace) ||
          GRPC_TRACE_FLAG_ENABLED(grpc_keepalive_trace)) {
        gpr_log(GPR_INFO, "%s: Finish keepalive ping", t->peer_string.c_str());
      }
      if (!t->keepalive_ping_started) {
        // start_keepalive_ping_locked has not run yet. Reschedule
        // finish_keepalive_ping_locked for it to be run later.
        t->combiner->Run(
            GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                              finish_keepalive_ping_locked, t, nullptr),
            GRPC_ERROR_REF(error));
        return;
      }
      t->keepalive_ping_started = false;
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
      grpc_timer_cancel(&t->keepalive_watchdog_timer);
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      GRPC_CLOSURE_INIT(&t->init_keepalive_ping_locked, init_keepalive_ping, t,
                        grpc_schedule_on_exec_ctx);
      grpc_timer_init(&t->keepalive_ping_timer,
                      grpc_core::ExecCtx::Get()->Now() + t->keepalive_time,
                      &t->init_keepalive_ping_locked);
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive ping end");
}

static void keepalive_watchdog_fired(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&t->keepalive_watchdog_fired_locked,
                        keepalive_watchdog_fired_locked, t, nullptr),
      GRPC_ERROR_REF(error));
}

static void keepalive_watchdog_fired_locked(void* arg,
                                            grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    if (GRPC_ERROR_IS_NONE(error)) {
      gpr_log(GPR_INFO, "%s: Keepalive watchdog fired. Closing transport.",
              t->peer_string.c_str());
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
      close_transport_locked(
          t, grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                    "keepalive watchdog timeout"),
                                GRPC_ERROR_INT_GRPC_STATUS,
                                GRPC_STATUS_UNAVAILABLE));
    }
  } else {
    // The watchdog timer should have been cancelled by
    // finish_keepalive_ping_locked.
    if (GPR_UNLIKELY(error != GRPC_ERROR_CANCELLED)) {
      gpr_log(GPR_ERROR, "keepalive_ping_end state error: %d (expect: %d)",
              t->keepalive_state, GRPC_CHTTP2_KEEPALIVE_STATE_PINGING);
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive watchdog");
}

//
// CALLBACK LOOP
//

static void connectivity_state_set(grpc_chttp2_transport* t,
                                   grpc_connectivity_state state,
                                   const absl::Status& status,
                                   const char* reason) {
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_INFO, "transport %p set connectivity_state=%d", t, state));
  t->state_tracker.SetState(state, status, reason);
}

//
// POLLSET STUFF
//

static void set_pollset(grpc_transport* gt, grpc_stream* /*gs*/,
                        grpc_pollset* pollset) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_endpoint_add_to_pollset(t->ep, pollset);
}

static void set_pollset_set(grpc_transport* gt, grpc_stream* /*gs*/,
                            grpc_pollset_set* pollset_set) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_endpoint_add_to_pollset_set(t->ep, pollset_set);
}

//
// RESOURCE QUOTAS
//

static void post_benign_reclaimer(grpc_chttp2_transport* t) {
  if (!t->benign_reclaimer_registered) {
    t->benign_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "benign_reclaimer");
    t->memory_owner.PostReclaimer(
        grpc_core::ReclamationPass::kBenign,
        [t](absl::optional<grpc_core::ReclamationSweep> sweep) {
          if (sweep.has_value()) {
            GRPC_CLOSURE_INIT(&t->benign_reclaimer_locked,
                              benign_reclaimer_locked, t,
                              grpc_schedule_on_exec_ctx);
            t->active_reclamation = std::move(*sweep);
            t->combiner->Run(&t->benign_reclaimer_locked, GRPC_ERROR_NONE);
          } else {
            GRPC_CHTTP2_UNREF_TRANSPORT(t, "benign_reclaimer");
          }
        });
  }
}

static void post_destructive_reclaimer(grpc_chttp2_transport* t) {
  if (!t->destructive_reclaimer_registered) {
    t->destructive_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "destructive_reclaimer");
    t->memory_owner.PostReclaimer(
        grpc_core::ReclamationPass::kDestructive,
        [t](absl::optional<grpc_core::ReclamationSweep> sweep) {
          if (sweep.has_value()) {
            GRPC_CLOSURE_INIT(&t->destructive_reclaimer_locked,
                              destructive_reclaimer_locked, t,
                              grpc_schedule_on_exec_ctx);
            t->active_reclamation = std::move(*sweep);
            t->combiner->Run(&t->destructive_reclaimer_locked, GRPC_ERROR_NONE);
          } else {
            GRPC_CHTTP2_UNREF_TRANSPORT(t, "benign_reclaimer");
          }
        });
  }
}

static void benign_reclaimer_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (GRPC_ERROR_IS_NONE(error) &&
      grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    // Channel with no active streams: send a goaway to try and make it
    // disconnect cleanly
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      gpr_log(GPR_INFO, "HTTP2: %s - send goaway to free memory",
              t->peer_string.c_str());
    }
    send_goaway(t,
                grpc_error_set_int(
                    GRPC_ERROR_CREATE_FROM_STATIC_STRING("Buffers full"),
                    GRPC_ERROR_INT_HTTP2_ERROR, GRPC_HTTP2_ENHANCE_YOUR_CALM),
                /*immediate_disconnect_hint=*/true);
  } else if (GRPC_ERROR_IS_NONE(error) &&
             GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO,
            "HTTP2: %s - skip benign reclamation, there are still %" PRIdPTR
            " streams",
            t->peer_string.c_str(),
            grpc_chttp2_stream_map_size(&t->stream_map));
  }
  t->benign_reclaimer_registered = false;
  if (error != GRPC_ERROR_CANCELLED) {
    t->active_reclamation.Finish();
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "benign_reclaimer");
}

static void destructive_reclaimer_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  size_t n = grpc_chttp2_stream_map_size(&t->stream_map);
  t->destructive_reclaimer_registered = false;
  if (GRPC_ERROR_IS_NONE(error) && n > 0) {
    grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(
        grpc_chttp2_stream_map_rand(&t->stream_map));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      gpr_log(GPR_INFO, "HTTP2: %s - abandon stream id %d",
              t->peer_string.c_str(), s->id);
    }
    grpc_chttp2_cancel_stream(
        t, s,
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Buffers full"),
                           GRPC_ERROR_INT_HTTP2_ERROR,
                           GRPC_HTTP2_ENHANCE_YOUR_CALM));
    if (n > 1) {
      // Since we cancel one stream per destructive reclamation, if
      //   there are more streams left, we can immediately post a new
      //   reclaimer in case the resource quota needs to free more
      //   memory
      post_destructive_reclaimer(t);
    }
  }
  if (error != GRPC_ERROR_CANCELLED) {
    t->active_reclamation.Finish();
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "destructive_reclaimer");
}

//
// MONITORING
//

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
    case GRPC_CHTTP2_INITIATE_WRITE_SETTINGS_ACK:
      return "SETTINGS_ACK";
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_SETTING:
      return "FLOW_CONTROL_UNSTALLED_BY_SETTING";
    case GRPC_CHTTP2_INITIATE_WRITE_FLOW_CONTROL_UNSTALLED_BY_UPDATE:
      return "FLOW_CONTROL_UNSTALLED_BY_UPDATE";
    case GRPC_CHTTP2_INITIATE_WRITE_APPLICATION_PING:
      return "APPLICATION_PING";
    case GRPC_CHTTP2_INITIATE_WRITE_BDP_PING:
      return "BDP_PING";
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
                                             nullptr,
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
    const grpc_core::ChannelArgs& channel_args, grpc_endpoint* ep,
    bool is_client) {
  auto t = new grpc_chttp2_transport(channel_args, ep, is_client);
  return &t->base;
}

void grpc_chttp2_transport_start_reading(
    grpc_transport* transport, grpc_slice_buffer* read_buffer,
    grpc_closure* notify_on_receive_settings, grpc_closure* notify_on_close) {
  grpc_chttp2_transport* t =
      reinterpret_cast<grpc_chttp2_transport*>(transport);
  GRPC_CHTTP2_REF_TRANSPORT(
      t, "reading_action"); /* matches unref inside reading_action */
  if (read_buffer != nullptr) {
    grpc_slice_buffer_move_into(read_buffer, &t->read_buffer);
    gpr_free(read_buffer);
  }
  t->notify_on_receive_settings = notify_on_receive_settings;
  t->notify_on_close = notify_on_close;
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_locked, t, nullptr),
      GRPC_ERROR_NONE);
}
