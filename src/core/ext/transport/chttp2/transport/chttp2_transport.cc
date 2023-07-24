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
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/context_list_entry.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http_trace.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/resource_quota/trace.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/bdp_estimator.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/status_conversion.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_impl.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include "src/core/lib/iomgr/ev_posix.h"
#endif

#define DEFAULT_CONNECTION_WINDOW_TARGET (1024 * 1024)
#define MAX_WINDOW 0x7fffffffu
#define MAX_WRITE_BUFFER_SIZE (64 * 1024 * 1024)
#define DEFAULT_MAX_HEADER_LIST_SIZE (16 * 1024)
#define DEFAULT_MAX_HEADER_LIST_SIZE_SOFT_LIMIT (8 * 1024)

#define KEEPALIVE_TIME_BACKOFF_MULTIPLIER 2

#define DEFAULT_MAX_PENDING_INDUCED_FRAMES 10000

static grpc_core::Duration g_default_client_keepalive_time =
    grpc_core::Duration::Infinity();
static grpc_core::Duration g_default_client_keepalive_timeout =
    grpc_core::Duration::Seconds(20);
static grpc_core::Duration g_default_server_keepalive_time =
    grpc_core::Duration::Hours(2);
static grpc_core::Duration g_default_server_keepalive_timeout =
    grpc_core::Duration::Seconds(20);
static bool g_default_client_keepalive_permit_without_calls = false;
static bool g_default_server_keepalive_permit_without_calls = false;

static grpc_core::Duration g_default_min_recv_ping_interval_without_data =
    grpc_core::Duration::Minutes(5);
static int g_default_max_pings_without_data = 2;
static int g_default_max_ping_strikes = 2;

#define MAX_CLIENT_STREAM_ID 0x7fffffffu
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
static void next_bdp_ping_timer_expired(grpc_chttp2_transport* t);
static void next_bdp_ping_timer_expired_locked(
    void* tp, GRPC_UNUSED grpc_error_handle error);

static void cancel_pings(grpc_chttp2_transport* t, grpc_error_handle error);
static void send_ping_locked(grpc_chttp2_transport* t,
                             grpc_closure* on_initiate, grpc_closure* on_ack);
static void retry_initiate_ping_locked(void* tp,
                                       GRPC_UNUSED grpc_error_handle error);

// keepalive-relevant functions
static void init_keepalive_ping(grpc_chttp2_transport* t);
static void init_keepalive_ping_locked(void* arg,
                                       GRPC_UNUSED grpc_error_handle error);
static void start_keepalive_ping(void* arg, grpc_error_handle error);
static void finish_keepalive_ping(void* arg, grpc_error_handle error);
static void start_keepalive_ping_locked(void* arg, grpc_error_handle error);
static void finish_keepalive_ping_locked(void* arg, grpc_error_handle error);
static void keepalive_watchdog_fired(grpc_chttp2_transport* t);
static void keepalive_watchdog_fired_locked(
    void* arg, GRPC_UNUSED grpc_error_handle error);
static void maybe_reset_keepalive_ping_timer_locked(grpc_chttp2_transport* t);

namespace {
grpc_core::CallTracerInterface* CallTracerIfEnabled(grpc_chttp2_stream* s) {
  if (s->context == nullptr || !grpc_core::IsTraceRecordCallopsEnabled()) {
    return nullptr;
  }
  return static_cast<grpc_core::CallTracerInterface*>(
      static_cast<grpc_call_context_element*>(
          s->context)[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
          .value);
}

grpc_core::WriteTimestampsCallback g_write_timestamps_callback = nullptr;
grpc_core::CopyContextFn g_get_copied_context_fn = nullptr;
}  // namespace

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

void GrpcHttp2SetWriteTimestampsCallback(WriteTimestampsCallback fn) {
  g_write_timestamps_callback = fn;
}

void GrpcHttp2SetCopyContextFn(CopyContextFn fn) {
  g_get_copied_context_fn = fn;
}

WriteTimestampsCallback GrpcHttp2GetWriteTimestampsCallback() {
  return g_write_timestamps_callback;
}

CopyContextFn GrpcHttp2GetCopyContextFn() { return g_get_copied_context_fn; }

// For each entry in the passed ContextList, it executes the function set using
// GrpcHttp2SetWriteTimestampsCallback method with each context in the list
// and \a ts. It also deletes/frees up the passed ContextList after this
// operation.
void ForEachContextListEntryExecute(void* arg, Timestamps* ts,
                                    grpc_error_handle error) {
  ContextList* context_list = reinterpret_cast<ContextList*>(arg);
  if (!context_list) {
    return;
  }
  for (auto it = context_list->begin(); it != context_list->end(); it++) {
    ContextListEntry& entry = (*it);
    if (ts) {
      ts->byte_offset = static_cast<uint32_t>(entry.ByteOffsetInStream());
    }
    g_write_timestamps_callback(entry.TraceContext(), ts, error);
  }
  delete context_list;
}

}  // namespace grpc_core

//
// CONSTRUCTION/DESTRUCTION/REFCOUNTING
//

grpc_chttp2_transport::~grpc_chttp2_transport() {
  size_t i;

  event_engine.reset();

  if (channelz_socket != nullptr) {
    channelz_socket.reset();
  }

  grpc_endpoint_destroy(ep);

  grpc_slice_buffer_destroy(&qbuf);

  grpc_slice_buffer_destroy(&outbuf);

  grpc_error_handle error = GRPC_ERROR_CREATE("Transport destroyed");
  // ContextList::Execute follows semantics of a callback function and does not
  // take a ref on error
  if (cl != nullptr) {
    grpc_core::ForEachContextListEntryExecute(cl, nullptr, error);
  }
  cl = nullptr;

  grpc_slice_buffer_destroy(&read_buffer);
  grpc_chttp2_goaway_parser_destroy(&goaway_parser);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(lists[i].head == nullptr);
    GPR_ASSERT(lists[i].tail == nullptr);
  }

  GPR_ASSERT(stream_map.empty());
  GRPC_COMBINER_UNREF(combiner, "chttp2_transport");

  cancel_pings(this, GRPC_ERROR_CREATE("Transport destroyed"));

  while (write_cb_pool) {
    grpc_chttp2_write_cb* next = write_cb_pool->next;
    gpr_free(write_cb_pool);
    write_cb_pool = next;
  }

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
                   .value_or(g_default_min_recv_ping_interval_without_data));
  t->write_buffer_size =
      std::max(0, channel_args.GetInt(GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE)
                      .value_or(grpc_core::chttp2::kDefaultWindow));
  t->keepalive_time =
      std::max(grpc_core::Duration::Milliseconds(1),
               channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIME_MS)
                   .value_or(t->is_client ? g_default_client_keepalive_time
                                          : g_default_server_keepalive_time));
  t->keepalive_timeout = std::max(
      grpc_core::Duration::Zero(),
      channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIMEOUT_MS)
          .value_or(t->is_client ? g_default_client_keepalive_timeout
                                 : g_default_server_keepalive_timeout));
  if (grpc_core::IsKeepaliveFixEnabled()) {
    t->keepalive_permit_without_calls =
        channel_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
            .value_or(t->is_client
                          ? g_default_client_keepalive_permit_without_calls
                          : g_default_server_keepalive_permit_without_calls);
  } else {
    t->keepalive_permit_without_calls =
        channel_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
            .value_or(false);
  }

  // Only send the prefered rx frame size http2 setting if we are instructed
  // to auto size the buffers allocated at tcp level and we also can adjust
  // sending frame size.
  t->enable_preferred_rx_crypto_frame_advertisement =
      channel_args
          .GetBool(GRPC_ARG_EXPERIMENTAL_HTTP2_PREFERRED_CRYPTO_FRAME_SIZE)
          .value_or(false);

  if (channel_args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
          .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    t->channelz_socket =
        grpc_core::MakeRefCounted<grpc_core::channelz::SocketNode>(
            std::string(grpc_endpoint_get_local_address(t->ep)),
            std::string(t->peer_string.as_string_view()),
            absl::StrCat(get_vtable()->name, " ",
                         t->peer_string.as_string_view()),
            channel_args
                .GetObjectRef<grpc_core::channelz::SocketNode::Security>());
  }

  t->ack_pings = channel_args.GetBool("grpc.http2.ack_pings").value_or(true);

  const int soft_limit =
      channel_args.GetInt(GRPC_ARG_MAX_METADATA_SIZE).value_or(-1);
  if (soft_limit < 0) {
    // Set soft limit to 0.8 * hard limit if this is larger than
    // `DEFAULT_MAX_HEADER_LIST_SIZE_SOFT_LIMIT` and
    // `GRPC_ARG_MAX_METADATA_SIZE` is not set.
    t->max_header_list_size_soft_limit = std::max(
        DEFAULT_MAX_HEADER_LIST_SIZE_SOFT_LIMIT,
        static_cast<int>(
            0.8 * channel_args.GetInt(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE)
                      .value_or(-1)));
  } else {
    t->max_header_list_size_soft_limit = soft_limit;
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
                      {GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE,
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
      } else if (setting.setting_id ==
                 GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE) {
        // Set value to 1.25 * soft limit if this is larger than
        // `DEFAULT_MAX_HEADER_LIST_SIZE` and
        // `GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE` is not set.
        const int soft_limit = channel_args.GetInt(GRPC_ARG_MAX_METADATA_SIZE)
                                   .value_or(setting.default_value);
        const int value = (soft_limit >= 0 && soft_limit < (INT_MAX / 1.25))
                              ? static_cast<int>(soft_limit * 1.25)
                              : soft_limit;
        if (value > DEFAULT_MAX_HEADER_LIST_SIZE) {
          queue_setting_update(
              t, setting.setting_id,
              grpc_core::Clamp(value, setting.min, setting.max));
        }
      }
    } else if (channel_args.Contains(setting.channel_arg_name)) {
      gpr_log(GPR_DEBUG, "%s is not available on %s",
              std::string(setting.channel_arg_name).c_str(),
              is_client ? "clients" : "servers");
    }
  }

  if (t->enable_preferred_rx_crypto_frame_advertisement) {
    const grpc_chttp2_setting_parameters* sp =
        &grpc_chttp2_settings_parameters
            [GRPC_CHTTP2_SETTINGS_GRPC_PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE];
    queue_setting_update(
        t, GRPC_CHTTP2_SETTINGS_GRPC_PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE,
        grpc_core::Clamp(INT_MAX, static_cast<int>(sp->min_value),
                         static_cast<int>(sp->max_value)));
  }
}

static void init_keepalive_pings_if_enabled_locked(
    void* arg, GRPC_UNUSED grpc_error_handle error) {
  GPR_DEBUG_ASSERT(error.ok());
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (t->keepalive_time != grpc_core::Duration::Infinity()) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
    GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
    t->keepalive_ping_timer_handle =
        t->event_engine->RunAfter(t->keepalive_time, [t] {
          grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
          grpc_core::ExecCtx exec_ctx;
          init_keepalive_ping(t);
        });
  } else {
    // Use GRPC_CHTTP2_KEEPALIVE_STATE_DISABLED to indicate there are no
    // inflight keepalive timers
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
      peer_string(
          grpc_core::Slice::FromCopiedString(grpc_endpoint_get_peer(ep))),
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
          peer_string.as_string_view(),
          channel_args.GetBool(GRPC_ARG_HTTP2_BDP_PROBE).value_or(true),
          &memory_owner),
      deframe_state(is_client ? GRPC_DTS_FH_0 : GRPC_DTS_CLIENT_PREFIX_0),
      event_engine(
          channel_args
              .GetObjectRef<grpc_event_engine::experimental::EventEngine>()) {
  cl = new grpc_core::ContextList();
  GPR_ASSERT(strlen(GRPC_CHTTP2_CLIENT_CONNECT_STRING) ==
             GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);
  base.vtable = get_vtable();

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

  read_channel_args(this, channel_args, is_client);

  // No pings allowed before receiving a header or data frame.
  ping_state.pings_before_data_required = 0;
  ping_state.last_ping_sent_time = grpc_core::Timestamp::InfPast();

  ping_recv_state.last_ping_recv_time = grpc_core::Timestamp::InfPast();
  ping_recv_state.ping_strikes = 0;

  grpc_core::ExecCtx exec_ctx;
  combiner->Run(
      GRPC_CLOSURE_INIT(&init_keepalive_ping_locked,
                        init_keepalive_pings_if_enabled_locked, this, nullptr),
      absl::OkStatus());

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

#ifdef GRPC_POSIX_SOCKET_TCP
  closure_barrier_may_cover_write =
      grpc_event_engine_run_in_background() &&
              grpc_core::IsScheduleCancellationOverWriteEnabled()
          ? 0
          : CLOSURE_BARRIER_MAY_COVER_WRITE;
#endif
}

static void destroy_transport_locked(void* tp, grpc_error_handle /*error*/) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->destroying = 1;
  close_transport_locked(
      t, grpc_error_set_int(GRPC_ERROR_CREATE("Transport destroyed"),
                            grpc_core::StatusIntProperty::kOccurredDuringWrite,
                            t->write_state));
  t->memory_owner.Reset();
  // Must be the last line.
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "destroy");
}

static void destroy_transport(grpc_transport* gt) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  t->combiner->Run(GRPC_CLOSURE_CREATE(destroy_transport_locked, t, nullptr),
                   absl::OkStatus());
}

static void close_transport_locked(grpc_chttp2_transport* t,
                                   grpc_error_handle error) {
  end_all_the_calls(t, error);
  cancel_pings(t, error);
  if (t->closed_with_error.ok()) {
    if (!grpc_error_has_clear_grpc_status(error)) {
      error =
          grpc_error_set_int(error, grpc_core::StatusIntProperty::kRpcStatus,
                             GRPC_STATUS_UNAVAILABLE);
    }
    if (t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE) {
      if (t->close_transport_on_writes_finished.ok()) {
        t->close_transport_on_writes_finished =
            GRPC_ERROR_CREATE("Delayed close due to in-progress write");
      }
      t->close_transport_on_writes_finished =
          grpc_error_add_child(t->close_transport_on_writes_finished, error);
      return;
    }
    GPR_ASSERT(!error.ok());
    t->closed_with_error = error;
    connectivity_state_set(t, GRPC_CHANNEL_SHUTDOWN, absl::Status(),
                           "close_transport");
    if (t->ping_state.delayed_ping_timer_handle.has_value()) {
      if (t->event_engine->Cancel(*t->ping_state.delayed_ping_timer_handle)) {
        GRPC_CHTTP2_UNREF_TRANSPORT(t, "retry_initiate_ping_locked");
        t->ping_state.delayed_ping_timer_handle.reset();
      }
    }
    if (t->next_bdp_ping_timer_handle.has_value()) {
      if (t->event_engine->Cancel(*t->next_bdp_ping_timer_handle)) {
        GRPC_CHTTP2_UNREF_TRANSPORT(t, "bdp_ping");
        t->next_bdp_ping_timer_handle.reset();
      }
    }
    switch (t->keepalive_state) {
      case GRPC_CHTTP2_KEEPALIVE_STATE_WAITING:
        if (t->keepalive_ping_timer_handle.has_value()) {
          if (t->event_engine->Cancel(*t->keepalive_ping_timer_handle)) {
            GRPC_CHTTP2_UNREF_TRANSPORT(t, "init keepalive ping");
            t->keepalive_ping_timer_handle.reset();
          }
        }
        break;
      case GRPC_CHTTP2_KEEPALIVE_STATE_PINGING:
        if (t->keepalive_ping_timer_handle.has_value()) {
          if (t->event_engine->Cancel(*t->keepalive_ping_timer_handle)) {
            GRPC_CHTTP2_UNREF_TRANSPORT(t, "init keepalive ping");
            t->keepalive_ping_timer_handle.reset();
          }
        }
        if (t->keepalive_watchdog_timer_handle.has_value()) {
          if (t->event_engine->Cancel(*t->keepalive_watchdog_timer_handle)) {
            GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive watchdog");
            t->keepalive_watchdog_timer_handle.reset();
          }
        }
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
    grpc_endpoint_shutdown(t->ep, error);
  }
  if (t->notify_on_receive_settings != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, t->notify_on_receive_settings,
                            error);
    t->notify_on_receive_settings = nullptr;
  }
  if (t->notify_on_close != nullptr) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, t->notify_on_close, error);
    t->notify_on_close = nullptr;
  }
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
    if (grpc_http_trace.enabled()) {
      gpr_log(GPR_DEBUG, "HTTP:%p/%p creating accept stream %d [from %p]", t,
              this, id, server_data);
    }
    *t->accepting_stream = this;
    t->stream_map.emplace(id, this);
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
    GPR_ASSERT(t->stream_map.count(id) == 0);
  }

  grpc_slice_buffer_destroy(&frame_storage);

  for (int i = 0; i < STREAM_LIST_COUNT; i++) {
    if (GPR_UNLIKELY(included.is_set(i))) {
      grpc_core::Crash(absl::StrFormat("%s stream %d still included in list %d",
                                       t->is_client ? "client" : "server", id,
                                       i));
    }
  }

  GPR_ASSERT(send_initial_metadata_finished == nullptr);
  GPR_ASSERT(send_trailing_metadata_finished == nullptr);
  GPR_ASSERT(recv_initial_metadata_ready == nullptr);
  GPR_ASSERT(recv_message_ready == nullptr);
  GPR_ASSERT(recv_trailing_metadata_finished == nullptr);
  grpc_slice_buffer_destroy(&flow_controlled_buffer);
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "stream");
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, destroy_stream_arg, absl::OkStatus());
}

static int init_stream(grpc_transport* gt, grpc_stream* gs,
                       grpc_stream_refcount* refcount, const void* server_data,
                       grpc_core::Arena* arena) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  new (gs) grpc_chttp2_stream(t, refcount, server_data, arena);
  return 0;
}

static void destroy_stream_locked(void* sp, grpc_error_handle /*error*/) {
  grpc_chttp2_stream* s = static_cast<grpc_chttp2_stream*>(sp);
  s->~grpc_chttp2_stream();
}

static void destroy_stream(grpc_transport* gt, grpc_stream* gs,
                           grpc_closure* then_schedule_closure) {
  grpc_chttp2_transport* t = reinterpret_cast<grpc_chttp2_transport*>(gt);
  grpc_chttp2_stream* s = reinterpret_cast<grpc_chttp2_stream*>(gs);

  s->destroy_stream_arg = then_schedule_closure;
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&s->destroy_stream, destroy_stream_locked, s, nullptr),
      absl::OkStatus());
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
              t->is_client ? "CLIENT" : "SERVER",
              std::string(t->peer_string.as_string_view()).c_str(),
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
    if (!t->close_transport_on_writes_finished.ok()) {
      grpc_error_handle err = t->close_transport_on_writes_finished;
      t->close_transport_on_writes_finished = absl::OkStatus();
      close_transport_locked(t, err);
    }
  }
}

void grpc_chttp2_initiate_write(grpc_chttp2_transport* t,
                                grpc_chttp2_initiate_write_reason reason) {
  switch (t->write_state) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
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
          absl::OkStatus());
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
  if (t->closed_with_error.ok() && grpc_chttp2_list_add_writable_stream(t, s)) {
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
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(gt);
  GPR_ASSERT(t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE);
  grpc_chttp2_begin_write_result r;
  if (!t->closed_with_error.ok()) {
    r.writing = false;
  } else {
    r = grpc_chttp2_begin_write(t);
  }
  if (r.writing) {
    set_write_state(t,
                    r.partial ? GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE
                              : GRPC_CHTTP2_WRITE_STATE_WRITING,
                    begin_writing_desc(r.partial));
    write_action(t, absl::OkStatus());
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
    set_write_state(t, GRPC_CHTTP2_WRITE_STATE_IDLE, "begin writing nothing");
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "writing");
  }
}

static void write_action(void* gt, grpc_error_handle /*error*/) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(gt);
  void* cl = t->cl;
  if (!t->cl->empty()) {
    // Transfer the ownership of the context list to the endpoint and create and
    // associate a new context list with the transport.
    // The old context list is stored in the cl local variable which is passed
    // to the endpoint. Its upto the endpoint to manage its lifetime.
    t->cl = new grpc_core::ContextList();
  } else {
    // t->cl is Empty. There is nothing to trace in this endpoint_write. set cl
    // to nullptr.
    cl = nullptr;
  }
  // Choose max_frame_size as the prefered rx crypto frame size indicated by the
  // peer.
  int max_frame_size =
      t->settings
          [GRPC_PEER_SETTINGS]
          [GRPC_CHTTP2_SETTINGS_GRPC_PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE];
  // Note: max frame size is 0 if the remote peer does not support adjusting the
  // sending frame size.
  if (max_frame_size == 0) {
    max_frame_size = INT_MAX;
  }
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
                   error);
}

// Callback from the grpc_endpoint after bytes have been written by calling
// sendmsg
static void write_action_end_locked(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);

  bool closed = false;
  if (!error.ok()) {
    close_transport_locked(t, error);
    closed = true;
  }

  if (t->sent_goaway_state == GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED) {
    t->sent_goaway_state = GRPC_CHTTP2_FINAL_GOAWAY_SENT;
    closed = true;
    if (t->stream_map.empty()) {
      close_transport_locked(t, GRPC_ERROR_CREATE("goaway sent"));
    }
  }

  switch (t->write_state) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      GPR_UNREACHABLE_CODE(break);
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      set_write_state(t, GRPC_CHTTP2_WRITE_STATE_IDLE, "finish writing");
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
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
          absl::OkStatus());
      break;
  }

  grpc_chttp2_end_write(t, error);
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
    grpc_chttp2_cancel_stream(t, s, error);
  }
}

void grpc_chttp2_add_incoming_goaway(grpc_chttp2_transport* t,
                                     uint32_t goaway_error,
                                     uint32_t last_stream_id,
                                     absl::string_view goaway_text) {
  t->goaway_error = grpc_error_set_str(
      grpc_error_set_int(
          grpc_error_set_int(
              grpc_core::StatusCreate(
                  absl::StatusCode::kUnavailable,
                  absl::StrFormat(
                      "GOAWAY received; Error code: %u; Debug Text: %s",
                      goaway_error, goaway_text),
                  DEBUG_LOCATION, {}),
              grpc_core::StatusIntProperty::kHttp2Error,
              static_cast<intptr_t>(goaway_error)),
          grpc_core::StatusIntProperty::kRpcStatus, GRPC_STATUS_UNAVAILABLE),
      grpc_core::StatusStrProperty::kRawBytes, goaway_text);

  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_INFO, "transport %p got goaway with last stream id %d", t,
              last_stream_id));
  // We want to log this irrespective of whether http tracing is enabled if we
  // received a GOAWAY with a non NO_ERROR code.
  if (goaway_error != GRPC_HTTP2_NO_ERROR) {
    gpr_log(GPR_INFO, "%s: Got goaway [%d] err=%s",
            std::string(t->peer_string.as_string_view()).c_str(), goaway_error,
            grpc_core::StatusToString(t->goaway_error).c_str());
  }
  if (t->is_client) {
    cancel_unstarted_streams(t, t->goaway_error);
    // Cancel all unseen streams
    std::vector<grpc_chttp2_stream*> to_cancel;
    for (auto id_stream : t->stream_map) {
      if (id_stream.first > last_stream_id) {
        to_cancel.push_back(id_stream.second);
      }
    }
    for (auto s : to_cancel) {
      s->trailing_metadata_buffer.Set(
          grpc_core::GrpcStreamNetworkState(),
          grpc_core::GrpcStreamNetworkState::kNotSeenByServer);
      grpc_chttp2_cancel_stream(s->t, s, s->t->goaway_error);
    }
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
            "%s: Received a GOAWAY with error code ENHANCE_YOUR_CALM and debug "
            "data equal to \"too_many_pings\". Current keepalive time (before "
            "throttling): %s",
            std::string(t->peer_string.as_string_view()).c_str(),
            t->keepalive_time.ToString().c_str());
    constexpr int max_keepalive_time_millis =
        INT_MAX / KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
    int64_t throttled_keepalive_time =
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
  if (!t->goaway_error.ok()) {
    cancel_unstarted_streams(t, t->goaway_error);
    return;
  }
  // start streams where we have free grpc_chttp2_stream ids and free
  // * concurrency
  while (t->next_stream_id <= MAX_CLIENT_STREAM_ID &&
         t->stream_map.size() <
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

    t->stream_map.emplace(s->id, s);
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
          grpc_error_set_int(GRPC_ERROR_CREATE("Stream IDs exhausted"),
                             grpc_core::StatusIntProperty::kRpcStatus,
                             GRPC_STATUS_UNAVAILABLE));
    }
  }
}

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
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, absl::OkStatus());
}

void grpc_chttp2_complete_closure_step(grpc_chttp2_transport* t,
                                       grpc_chttp2_stream* s,
                                       grpc_closure** pclosure,
                                       grpc_error_handle error,
                                       const char* desc,
                                       grpc_core::DebugLocation whence) {
  grpc_closure* closure = *pclosure;
  *pclosure = nullptr;
  if (closure == nullptr) {
    return;
  }
  closure->next_data.scratch -= CLOSURE_BARRIER_FIRST_REF_BIT;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(
        GPR_INFO,
        "complete_closure_step: t=%p %p refs=%d flags=0x%04x desc=%s err=%s "
        "write_state=%s whence=%s:%d",
        t, closure,
        static_cast<int>(closure->next_data.scratch /
                         CLOSURE_BARRIER_FIRST_REF_BIT),
        static_cast<int>(closure->next_data.scratch %
                         CLOSURE_BARRIER_FIRST_REF_BIT),
        desc, grpc_core::StatusToString(error).c_str(),
        write_state_name(t->write_state), whence.file(), whence.line());
  }

  auto* tracer = CallTracerIfEnabled(s);
  if (tracer != nullptr) {
    tracer->RecordAnnotation(
        absl::StrFormat("on_complete: s=%p %p desc=%s err=%s", s, closure, desc,
                        grpc_core::StatusToString(error).c_str()));
  }

  if (!error.ok()) {
    grpc_error_handle cl_err =
        grpc_core::internal::StatusMoveFromHeapPtr(closure->error_data.error);
    if (cl_err.ok()) {
      cl_err = GRPC_ERROR_CREATE(absl::StrCat(
          "Error in HTTP transport completing operation: ", desc,
          " write_state=", write_state_name(t->write_state), " refs=",
          closure->next_data.scratch / CLOSURE_BARRIER_FIRST_REF_BIT, " flags=",
          closure->next_data.scratch % CLOSURE_BARRIER_FIRST_REF_BIT));
      cl_err = grpc_error_set_str(cl_err,
                                  grpc_core::StatusStrProperty::kTargetAddress,
                                  std::string(t->peer_string.as_string_view()));
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
  gpr_log(GPR_INFO, "--metadata--");
  const std::string prefix = absl::StrCat(
      "HTTP:", id, is_initial ? ":HDR" : ":TRL", is_client ? ":CLI:" : ":SVR:");
  md_batch->Log([&prefix](absl::string_view key, absl::string_view value) {
    gpr_log(GPR_INFO, "%s", absl::StrCat(prefix, key, ": ", value).c_str());
  });
}

static void perform_stream_op_locked(void* stream_op,
                                     grpc_error_handle /*error_ignored*/) {
  grpc_transport_stream_op_batch* op =
      static_cast<grpc_transport_stream_op_batch*>(stream_op);
  grpc_chttp2_stream* s =
      static_cast<grpc_chttp2_stream*>(op->handler_private.extra_arg);
  grpc_transport_stream_op_batch_payload* op_payload = op->payload;
  grpc_chttp2_transport* t = s->t;

  s->context = op->payload->context;
  s->traced = op->is_traced;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO,
            "perform_stream_op_locked[s=%p; op=%p]: %s; on_complete = %p", s,
            op, grpc_transport_stream_op_batch_string(op, false).c_str(),
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

  auto* tracer = CallTracerIfEnabled(s);
  if (tracer != nullptr) {
    tracer->RecordAnnotation(absl::StrFormat(
        "perform_stream_op_locked[s=%p; op=%p]: %s; on_complete = %p", s, op,
        grpc_transport_stream_op_batch_string(op, true).c_str(),
        op->on_complete));
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
    grpc_chttp2_cancel_stream(t, s, op_payload->cancel_stream.cancel_error);
  }

  if (op->send_initial_metadata) {
    if (t->is_client && t->channelz_socket != nullptr) {
      t->channelz_socket->RecordStreamStartedFromLocal();
    }
    GPR_ASSERT(s->send_initial_metadata_finished == nullptr);
    on_complete->next_data.scratch |= t->closure_barrier_may_cover_write;

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
        if (t->closed_with_error.ok()) {
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
                  GRPC_ERROR_CREATE_REFERENCING("Transport closed",
                                                &t->closed_with_error, 1),
                  grpc_core::StatusIntProperty::kRpcStatus,
                  GRPC_STATUS_UNAVAILABLE));
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
          GRPC_ERROR_CREATE_REFERENCING(
              "Attempt to send initial metadata after stream was closed",
              &s->write_closed_error, 1),
          "send_initial_metadata_finished");
    }
  }

  if (op->send_message) {
    t->num_messages_in_next_write++;
    grpc_core::global_stats().IncrementHttp2SendMessageSize(
        op->payload->send_message.send_message->Length());
    on_complete->next_data.scratch |= t->closure_barrier_may_cover_write;
    s->send_message_finished = add_closure_barrier(op->on_complete);
    const uint32_t flags = op_payload->send_message.flags;
    if (s->write_closed) {
      op->payload->send_message.stream_write_closed = true;
      // We should NOT return an error here, so as to avoid a cancel OP being
      // started. The surface layer will notice that the stream has been closed
      // for writes and fail the send message op.
      grpc_chttp2_complete_closure_step(t, s, &s->send_message_finished,
                                        absl::OkStatus(),
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
                              grpc_core::CSliceRef(*slice));
      }

      int64_t notify_offset = s->next_message_end_offset;
      if (notify_offset <= s->flow_controlled_bytes_written) {
        grpc_chttp2_complete_closure_step(t, s, &s->send_message_finished,
                                          absl::OkStatus(),
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
    GPR_ASSERT(s->send_trailing_metadata_finished == nullptr);
    on_complete->next_data.scratch |= t->closure_barrier_may_cover_write;
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
              ? absl::OkStatus()
              : GRPC_ERROR_CREATE("Attempt to send trailing metadata after "
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
    GPR_ASSERT(s->recv_initial_metadata_ready == nullptr);
    s->recv_initial_metadata_ready =
        op_payload->recv_initial_metadata.recv_initial_metadata_ready;
    s->recv_initial_metadata =
        op_payload->recv_initial_metadata.recv_initial_metadata;
    s->trailing_metadata_available =
        op_payload->recv_initial_metadata.trailing_metadata_available;
    if (s->parsed_trailers_only && s->trailing_metadata_available != nullptr) {
      *s->trailing_metadata_available = true;
    }
    grpc_chttp2_maybe_complete_recv_initial_metadata(t, s);
  }

  if (op->recv_message) {
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
    grpc_chttp2_complete_closure_step(t, s, &on_complete, absl::OkStatus(),
                                      "op->on_complete");
  }

  GRPC_CHTTP2_STREAM_UNREF(s, "perform_stream_op");
}

static void perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                              grpc_transport_stream_op_batch* op) {
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
            grpc_transport_stream_op_batch_string(op, false).c_str());
  }

  GRPC_CHTTP2_STREAM_REF(s, "perform_stream_op");
  op->handler_private.extra_arg = gs;
  t->combiner->Run(GRPC_CLOSURE_INIT(&op->handler_private.closure,
                                     perform_stream_op_locked, op, nullptr),
                   absl::OkStatus());
}

static void cancel_pings(grpc_chttp2_transport* t, grpc_error_handle error) {
  GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_INFO, "%p CANCEL PINGS: %s", t,
                                 grpc_core::StatusToString(error).c_str()));
  // callback remaining pings: they're not allowed to call into the transport,
  //   and maybe they hold resources that need to be freed
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  GPR_ASSERT(!error.ok());
  for (size_t j = 0; j < GRPC_CHTTP2_PCL_COUNT; j++) {
    grpc_closure_list_fail_all(&pq->lists[j], error);
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &pq->lists[j]);
  }
}

static void send_ping_locked(grpc_chttp2_transport* t,
                             grpc_closure* on_initiate, grpc_closure* on_ack) {
  if (!t->closed_with_error.ok()) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_initiate, t->closed_with_error);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_ack, t->closed_with_error);
    return;
  }
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_INITIATE], on_initiate,
                           absl::OkStatus());
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_NEXT], on_ack,
                           absl::OkStatus());
}

// Specialized form of send_ping_locked for keepalive ping. If there is already
// a ping in progress, the keepalive ping would piggyback onto that ping,
// instead of waiting for that ping to complete and then starting a new ping.
static void send_keepalive_ping_locked(grpc_chttp2_transport* t) {
  if (!t->closed_with_error.ok()) {
    t->combiner->Run(GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked,
                                       start_keepalive_ping_locked, t, nullptr),
                     t->closed_with_error);
    t->combiner->Run(
        GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                          finish_keepalive_ping_locked, t, nullptr),
        t->closed_with_error);
    return;
  }
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_INFLIGHT])) {
    // There is a ping in flight. Add yourself to the inflight closure list.
    t->combiner->Run(GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked,
                                       start_keepalive_ping_locked, t, nullptr),
                     t->closed_with_error);
    grpc_closure_list_append(
        &pq->lists[GRPC_CHTTP2_PCL_INFLIGHT],
        GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                          finish_keepalive_ping, t, grpc_schedule_on_exec_ctx),
        absl::OkStatus());
    return;
  }
  grpc_closure_list_append(
      &pq->lists[GRPC_CHTTP2_PCL_INITIATE],
      GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked, start_keepalive_ping,
                        t, grpc_schedule_on_exec_ctx),
      absl::OkStatus());
  grpc_closure_list_append(
      &pq->lists[GRPC_CHTTP2_PCL_NEXT],
      GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked, finish_keepalive_ping,
                        t, grpc_schedule_on_exec_ctx),
      absl::OkStatus());
}

void grpc_chttp2_retry_initiate_ping(grpc_chttp2_transport* t) {
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->retry_initiate_ping_locked,
                                     retry_initiate_ping_locked, t, nullptr),
                   absl::OkStatus());
}

static void retry_initiate_ping_locked(void* tp,
                                       GRPC_UNUSED grpc_error_handle error) {
  GPR_DEBUG_ASSERT(error.ok());
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  GPR_ASSERT(t->ping_state.delayed_ping_timer_handle.has_value());
  t->ping_state.delayed_ping_timer_handle.reset();
  grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_RETRY_SEND_PING);
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "retry_initiate_ping_locked");
}

void grpc_chttp2_ack_ping(grpc_chttp2_transport* t, uint64_t id) {
  grpc_chttp2_ping_queue* pq = &t->ping_queue;
  if (pq->inflight_id != id) {
    gpr_log(GPR_DEBUG, "Unknown ping response from %s: %" PRIx64,
            std::string(t->peer_string.as_string_view()).c_str(), id);
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
// final GOAWAY frame with an updated last stream identifier. This helps ensure
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
  using TaskHandle = ::grpc_event_engine::experimental::EventEngine::TaskHandle;

  explicit GracefulGoaway(grpc_chttp2_transport* t) : t_(t) {
    t->sent_goaway_state = GRPC_CHTTP2_GRACEFUL_GOAWAY;
    GRPC_CHTTP2_REF_TRANSPORT(t_, "graceful goaway");
    grpc_chttp2_goaway_append((1u << 31) - 1, 0, grpc_empty_slice(), &t->qbuf);
    send_ping_locked(
        t, nullptr, GRPC_CLOSURE_INIT(&on_ping_ack_, OnPingAck, this, nullptr));
    grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
    timer_handle_ = t_->event_engine->RunAfter(
        grpc_core::Duration::Seconds(20),
        [self = Ref(DEBUG_LOCATION, "GoawayTimer")]() mutable {
          grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
          grpc_core::ExecCtx exec_ctx;
          // The ref will be unreffed in the combiner.
          auto* ptr = self.release();
          ptr->t_->combiner->Run(
              GRPC_CLOSURE_INIT(&ptr->on_timer_, OnTimerLocked, ptr, nullptr),
              absl::OkStatus());
        });
  }

  void MaybeSendFinalGoawayLocked() {
    if (t_->sent_goaway_state != GRPC_CHTTP2_GRACEFUL_GOAWAY) {
      // We already sent the final GOAWAY.
      return;
    }
    if (t_->destroying || !t_->closed_with_error.ok()) {
      GRPC_CHTTP2_IF_TRACING(
          gpr_log(GPR_INFO,
                  "transport:%p %s peer:%s Transport already shutting down. "
                  "Graceful GOAWAY abandoned.",
                  t_, t_->is_client ? "CLIENT" : "SERVER",
                  std::string(t_->peer_string.as_string_view()).c_str()));
      return;
    }
    // Ping completed. Send final goaway.
    GRPC_CHTTP2_IF_TRACING(
        gpr_log(GPR_INFO,
                "transport:%p %s peer:%s Graceful shutdown: Ping received. "
                "Sending final GOAWAY with stream_id:%d",
                t_, t_->is_client ? "CLIENT" : "SERVER",
                std::string(t_->peer_string.as_string_view()).c_str(),
                t_->last_new_stream_id));
    t_->sent_goaway_state = GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED;
    grpc_chttp2_goaway_append(t_->last_new_stream_id, 0, grpc_empty_slice(),
                              &t_->qbuf);
    grpc_chttp2_initiate_write(t_, GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
  }

  static void OnPingAck(void* arg, grpc_error_handle /* error */) {
    auto* self = static_cast<GracefulGoaway*>(arg);
    self->t_->combiner->Run(
        GRPC_CLOSURE_INIT(&self->on_ping_ack_, OnPingAckLocked, self, nullptr),
        absl::OkStatus());
  }

  static void OnPingAckLocked(void* arg, grpc_error_handle /* error */) {
    auto* self = static_cast<GracefulGoaway*>(arg);
    if (self->timer_handle_ != TaskHandle::kInvalid) {
      self->t_->event_engine->Cancel(
          std::exchange(self->timer_handle_, TaskHandle::kInvalid));
    }
    self->MaybeSendFinalGoawayLocked();
    self->Unref();
  }

  static void OnTimerLocked(void* arg, grpc_error_handle /* error */) {
    auto* self = static_cast<GracefulGoaway*>(arg);
    // Clearing the handle since the timer has fired and the handle is invalid.
    self->timer_handle_ = TaskHandle::kInvalid;
    self->MaybeSendFinalGoawayLocked();
    self->Unref();
  }

  grpc_chttp2_transport* t_;
  grpc_closure on_ping_ack_;
  TaskHandle timer_handle_ = TaskHandle::kInvalid;
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
    gpr_log(GPR_DEBUG, "%s %s: Sending goaway last_new_stream_id=%d err=%s",
            std::string(t->peer_string.as_string_view()).c_str(),
            t->is_client ? "CLIENT" : "SERVER", t->last_new_stream_id,
            grpc_core::StatusToString(error).c_str());
    t->sent_goaway_state = GRPC_CHTTP2_FINAL_GOAWAY_SEND_SCHEDULED;
    grpc_chttp2_goaway_append(
        t->last_new_stream_id, static_cast<uint32_t>(http_error),
        grpc_slice_from_cpp_string(std::move(message)), &t->qbuf);
  } else {
    // Final GOAWAY has already been sent.
  }
  grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_GOAWAY_SENT);
}

void grpc_chttp2_add_ping_strike(grpc_chttp2_transport* t) {
  if (++t->ping_recv_state.ping_strikes > t->ping_policy.max_ping_strikes &&
      t->ping_policy.max_ping_strikes != 0) {
    send_goaway(t,
                grpc_error_set_int(GRPC_ERROR_CREATE("too_many_pings"),
                                   grpc_core::StatusIntProperty::kHttp2Error,
                                   GRPC_HTTP2_ENHANCE_YOUR_CALM),
                /*immediate_disconnect_hint=*/true);
    // The transport will be closed after the write is done
    close_transport_locked(
        t, grpc_error_set_int(GRPC_ERROR_CREATE("Too many pings"),
                              grpc_core::StatusIntProperty::kRpcStatus,
                              GRPC_STATUS_UNAVAILABLE));
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

  if (!op->goaway_error.ok()) {
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

  if (!op->disconnect_with_error.ok()) {
    send_goaway(t, op->disconnect_with_error,
                /*immediate_disconnect_hint=*/true);
    close_transport_locked(t, op->disconnect_with_error);
  }

  grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());

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
                   absl::OkStatus());
}

//
// INPUT PROCESSING - GENERAL
//

void grpc_chttp2_maybe_complete_recv_initial_metadata(grpc_chttp2_transport* t,
                                                      grpc_chttp2_stream* s) {
  if (s->recv_initial_metadata_ready != nullptr &&
      s->published_metadata[0] != GRPC_METADATA_NOT_PUBLISHED) {
    if (s->seen_error) {
      grpc_slice_buffer_reset_and_unref(&s->frame_storage);
    }
    *s->recv_initial_metadata = std::move(s->initial_metadata_buffer);
    s->recv_initial_metadata->Set(grpc_core::PeerString(),
                                  t->peer_string.Ref());
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
  grpc_error_handle error;

  // Lambda is immediately invoked as a big scoped section that can be
  // exited out of at any point by returning.
  [&]() {
    if (grpc_http_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "maybe_complete_recv_message %p final_metadata_requested=%d "
              "seen_error=%d",
              s, s->final_metadata_requested, s->seen_error);
    }
    if (s->final_metadata_requested && s->seen_error) {
      grpc_slice_buffer_reset_and_unref(&s->frame_storage);
      s->recv_message->reset();
    } else {
      if (s->frame_storage.length != 0) {
        while (true) {
          GPR_ASSERT(s->frame_storage.length > 0);
          int64_t min_progress_size;
          auto r = grpc_deframe_unprocessed_incoming_frames(
              s, &min_progress_size, &**s->recv_message, s->recv_message_flags);
          if (grpc_http_trace.enabled()) {
            gpr_log(GPR_DEBUG, "Deframe data frame: %s",
                    grpc_core::PollToString(r, [](absl::Status r) {
                      return r.ToString();
                    }).c_str());
          }
          if (r.pending()) {
            if (s->read_closed) {
              grpc_slice_buffer_reset_and_unref(&s->frame_storage);
              s->recv_message->reset();
              break;
            } else {
              upd.SetMinProgressSize(min_progress_size);
              return;  // Out of lambda to enclosing function
            }
          } else {
            error = std::move(r.value());
            if (!error.ok()) {
              s->seen_error = true;
              grpc_slice_buffer_reset_and_unref(&s->frame_storage);
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
    if (error.ok() && s->recv_message->has_value()) {
      null_then_sched_closure(&s->recv_message_ready);
    } else if (s->published_metadata[1] != GRPC_METADATA_NOT_PUBLISHED) {
      if (s->call_failed_before_recv_message != nullptr) {
        *s->call_failed_before_recv_message =
            (s->published_metadata[1] != GRPC_METADATA_PUBLISHED_AT_CLOSE);
      }
      null_then_sched_closure(&s->recv_message_ready);
    }
  }();

  upd.SetPendingSize(s->frame_storage.length);
  grpc_chttp2_act_on_flowctl_action(upd.MakeAction(), t, s);
}

void grpc_chttp2_maybe_complete_recv_trailing_metadata(grpc_chttp2_transport* t,
                                                       grpc_chttp2_stream* s) {
  grpc_chttp2_maybe_complete_recv_message(t, s);
  if (grpc_http_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "maybe_complete_recv_trailing_metadata cli=%d s=%p closure=%p "
            "read_closed=%d "
            "write_closed=%d %" PRIdPTR,
            t->is_client, s, s->recv_trailing_metadata_finished, s->read_closed,
            s->write_closed, s->frame_storage.length);
  }
  if (s->recv_trailing_metadata_finished != nullptr && s->read_closed &&
      s->write_closed) {
    if (s->seen_error || !t->is_client) {
      grpc_slice_buffer_reset_and_unref(&s->frame_storage);
    }
    if (s->read_closed && s->frame_storage.length == 0 &&
        s->recv_trailing_metadata_finished != nullptr) {
      grpc_transport_move_stats(&s->stats, s->collecting_stats);
      s->collecting_stats = nullptr;
      *s->recv_trailing_metadata = std::move(s->trailing_metadata_buffer);
      null_then_sched_closure(&s->recv_trailing_metadata_finished);
    }
  }
}

static void remove_stream(grpc_chttp2_transport* t, uint32_t id,
                          grpc_error_handle error) {
  grpc_chttp2_stream* s = t->stream_map.extract(id).mapped();
  GPR_DEBUG_ASSERT(s);
  if (t->incoming_stream == s) {
    t->incoming_stream = nullptr;
    grpc_chttp2_parsing_become_skip_parser(t);
  }

  if (t->stream_map.empty()) {
    post_benign_reclaimer(t);
    if (t->sent_goaway_state == GRPC_CHTTP2_FINAL_GOAWAY_SENT) {
      close_transport_locked(
          t, GRPC_ERROR_CREATE_REFERENCING(
                 "Last stream closed after sending GOAWAY", &error, 1));
    }
  }
  if (grpc_chttp2_list_remove_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_UNREF(s, "chttp2_writing:remove_stream");
  }
  grpc_chttp2_list_remove_stalled_by_stream(t, s);
  grpc_chttp2_list_remove_stalled_by_transport(t, s);

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
  if (!due_to_error.ok() && !s->seen_error) {
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
      s->recv_trailing_metadata_finished != nullptr ||
      !s->final_metadata_requested) {
    s->trailing_metadata_buffer.Set(grpc_core::GrpcStatusMetadata(), status);
    if (!message.empty()) {
      s->trailing_metadata_buffer.Set(
          grpc_core::GrpcMessageMetadata(),
          grpc_core::Slice::FromCopiedBuffer(message));
    }
    s->published_metadata[1] = GRPC_METADATA_SYNTHESIZED_FROM_FAKE;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
  }
}

static void add_error(grpc_error_handle error, grpc_error_handle* refs,
                      size_t* nrefs) {
  if (error.ok()) return;
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
  grpc_error_handle error;
  if (nrefs > 0) {
    error = GRPC_ERROR_CREATE_REFERENCING(main_error_msg, refs, nrefs);
  }
  return error;
}

static void flush_write_list(grpc_chttp2_transport* t, grpc_chttp2_stream* s,
                             grpc_chttp2_write_cb** list,
                             grpc_error_handle error) {
  while (*list) {
    grpc_chttp2_write_cb* cb = *list;
    *list = cb->next;
    grpc_chttp2_complete_closure_step(t, s, &cb->closure, error,
                                      "on_write_finished_cb");
    cb->next = t->write_cb_pool;
    t->write_cb_pool = cb;
  }
}

void grpc_chttp2_fail_pending_writes(grpc_chttp2_transport* t,
                                     grpc_chttp2_stream* s,
                                     grpc_error_handle error) {
  error =
      removal_error(error, s, "Pending writes failed due to stream closure");
  s->send_initial_metadata = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_initial_metadata_finished,
                                    error, "send_initial_metadata_finished");

  s->send_trailing_metadata = nullptr;
  s->sent_trailing_metadata_op = nullptr;
  grpc_chttp2_complete_closure_step(t, s, &s->send_trailing_metadata_finished,
                                    error, "send_trailing_metadata_finished");

  grpc_chttp2_complete_closure_step(t, s, &s->send_message_finished, error,
                                    "fetching_send_message_finished");
  flush_write_list(t, s, &s->on_write_finished_cbs, error);
  flush_write_list(t, s, &s->on_flow_controlled_cbs, error);
}

void grpc_chttp2_mark_stream_closed(grpc_chttp2_transport* t,
                                    grpc_chttp2_stream* s, int close_reads,
                                    int close_writes, grpc_error_handle error) {
  if (grpc_http_trace.enabled()) {
    gpr_log(
        GPR_DEBUG, "MARK_STREAM_CLOSED: t=%p s=%p(id=%d) %s [%s]", t, s, s->id,
        (close_reads && close_writes)
            ? "read+write"
            : (close_reads ? "read" : (close_writes ? "write" : "nothing??")),
        grpc_core::StatusToString(error).c_str());
  }
  if (s->read_closed && s->write_closed) {
    // already closed, but we should still fake the status if needed.
    grpc_error_handle overall_error = removal_error(error, s, "Stream removed");
    if (!overall_error.ok()) {
      grpc_chttp2_fake_status(t, s, overall_error);
    }
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
    return;
  }
  bool closed_read = false;
  bool became_closed = false;
  if (close_reads && !s->read_closed) {
    s->read_closed_error = error;
    s->read_closed = true;
    closed_read = true;
  }
  if (close_writes && !s->write_closed) {
    s->write_closed_error = error;
    s->write_closed = true;
    grpc_chttp2_fail_pending_writes(t, s, error);
  }
  if (s->read_closed && s->write_closed) {
    became_closed = true;
    grpc_error_handle overall_error = removal_error(error, s, "Stream removed");
    if (s->id != 0) {
      remove_stream(t, s->id, overall_error);
    } else {
      // Purge streams waiting on concurrency still waiting for id assignment
      grpc_chttp2_list_remove_waiting_for_concurrency(t, s);
    }
    if (!overall_error.ok()) {
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
    s->stats.latency =
        gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), s->creation_time);
    grpc_chttp2_maybe_complete_recv_trailing_metadata(t, s);
    GRPC_CHTTP2_STREAM_UNREF(s, "chttp2");
  }
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
  *p++ = 0x00;  // literal header, not indexed
  *p++ = 11;    // len(grpc-status)
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
  grpc_core::VarintWriter<1> msg_len_writer(static_cast<uint32_t>(msg_len));
  message_pfx = GRPC_SLICE_MALLOC(14 + msg_len_writer.length());
  p = GRPC_SLICE_START_PTR(message_pfx);
  *p++ = 0x00;  // literal header, not indexed
  *p++ = 12;    // len(grpc-message)
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

static void end_all_the_calls(grpc_chttp2_transport* t,
                              grpc_error_handle error) {
  intptr_t http2_error;
  // If there is no explicit grpc or HTTP/2 error, set to UNAVAILABLE on server.
  if (!t->is_client && !grpc_error_has_clear_grpc_status(error) &&
      !grpc_error_get_int(error, grpc_core::StatusIntProperty::kHttp2Error,
                          &http2_error)) {
    error = grpc_error_set_int(error, grpc_core::StatusIntProperty::kRpcStatus,
                               GRPC_STATUS_UNAVAILABLE);
  }
  cancel_unstarted_streams(t, error);
  std::vector<grpc_chttp2_stream*> to_cancel;
  for (auto id_stream : t->stream_map) {
    to_cancel.push_back(id_stream.second);
  }
  for (auto s : to_cancel) {
    grpc_chttp2_cancel_stream(t, s, error);
  }
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
                if (s->id != 0 && !s->read_closed) {
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
  if (t->enable_preferred_rx_crypto_frame_advertisement) {
    WithUrgency(
        t, action.preferred_rx_crypto_frame_size_update(),
        GRPC_CHTTP2_INITIATE_WRITE_SEND_SETTINGS, [t, &action]() {
          queue_setting_update(
              t, GRPC_CHTTP2_SETTINGS_GRPC_PREFERRED_RECEIVE_CRYPTO_FRAME_SIZE,
              action.preferred_rx_crypto_frame_size());
        });
  }
}

static grpc_error_handle try_http_parsing(grpc_chttp2_transport* t) {
  grpc_http_parser parser;
  size_t i = 0;
  grpc_error_handle error;
  grpc_http_response response;

  grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, &response);

  grpc_error_handle parse_error;
  for (; i < t->read_buffer.count && parse_error.ok(); i++) {
    parse_error =
        grpc_http_parser_parse(&parser, t->read_buffer.slices[i], nullptr);
  }
  if (parse_error.ok() &&
      (parse_error = grpc_http_parser_eof(&parser)) == absl::OkStatus()) {
    error = grpc_error_set_int(
        grpc_error_set_int(
            GRPC_ERROR_CREATE("Trying to connect an http1.x server"),
            grpc_core::StatusIntProperty::kHttpStatus, response.status),
        grpc_core::StatusIntProperty::kRpcStatus,
        grpc_http2_status_to_grpc_status(response.status));
  }

  grpc_http_parser_destroy(&parser);
  grpc_http_response_destroy(&response);
  return error;
}

static void read_action(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&t->read_action_locked, read_action_locked, t, nullptr),
      error);
}

static void read_action_locked(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);

  grpc_error_handle err = error;
  if (!err.ok()) {
    err = grpc_error_set_int(
        GRPC_ERROR_CREATE_REFERENCING("Endpoint read failed", &err, 1),
        grpc_core::StatusIntProperty::kOccurredDuringWrite, t->write_state);
  }
  std::swap(err, error);
  if (t->closed_with_error.ok()) {
    size_t i = 0;
    grpc_error_handle errors[3] = {error, absl::OkStatus(), absl::OkStatus()};
    for (; i < t->read_buffer.count && errors[1] == absl::OkStatus(); i++) {
      errors[1] = grpc_chttp2_perform_read(t, t->read_buffer.slices[i]);
    }
    if (errors[1] != absl::OkStatus()) {
      errors[2] = try_http_parsing(t);
      error = GRPC_ERROR_CREATE_REFERENCING("Failed parsing HTTP/2", errors,
                                            GPR_ARRAY_SIZE(errors));
    }

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

  bool keep_reading = false;
  if (error.ok() && !t->closed_with_error.ok()) {
    error = GRPC_ERROR_CREATE_REFERENCING("Transport closed",
                                          &t->closed_with_error, 1);
  }
  if (!error.ok()) {
    // If a goaway frame was received, this might be the reason why the read
    // failed. Add this info to the error
    if (!t->goaway_error.ok()) {
      error = grpc_error_add_child(error, t->goaway_error);
    }

    close_transport_locked(t, error);
    t->endpoint_reading = 0;
  } else if (t->closed_with_error.ok()) {
    keep_reading = true;
    // Since we have read a byte, reset the keepalive timer
    if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING) {
      maybe_reset_keepalive_ping_timer_locked(t);
    }
  }
  grpc_slice_buffer_reset_and_unref(&t->read_buffer);

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
}

static void continue_read_action_locked(grpc_chttp2_transport* t) {
  const bool urgent = !t->goaway_error.ok();
  GRPC_CLOSURE_INIT(&t->read_action_locked, read_action, t,
                    grpc_schedule_on_exec_ctx);
  grpc_endpoint_read(t->ep, &t->read_buffer, &t->read_action_locked, urgent,
                     grpc_chttp2_min_read_progress_size(t));
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
                   error);
}

static void start_bdp_ping_locked(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "%s: Start BDP ping err=%s",
            std::string(t->peer_string.as_string_view()).c_str(),
            grpc_core::StatusToString(error).c_str());
  }
  if (!error.ok() || !t->closed_with_error.ok()) {
    return;
  }
  // Reset the keepalive ping timer
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING) {
    maybe_reset_keepalive_ping_timer_locked(t);
  }
  t->flow_control.bdp_estimator()->StartPing();
  t->bdp_ping_started = true;
}

static void finish_bdp_ping(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->finish_bdp_ping_locked,
                                     finish_bdp_ping_locked, t, nullptr),
                   error);
}

static void finish_bdp_ping_locked(void* tp, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "%s: Complete BDP ping err=%s",
            std::string(t->peer_string.as_string_view()).c_str(),
            grpc_core::StatusToString(error).c_str());
  }
  if (!error.ok() || !t->closed_with_error.ok()) {
    GRPC_CHTTP2_UNREF_TRANSPORT(t, "bdp_ping");
    return;
  }
  if (!t->bdp_ping_started) {
    // start_bdp_ping_locked has not been run yet. Schedule
    // finish_bdp_ping_locked to be run later.
    t->combiner->Run(GRPC_CLOSURE_INIT(&t->finish_bdp_ping_locked,
                                       finish_bdp_ping_locked, t, nullptr),
                     error);
    return;
  }
  t->bdp_ping_started = false;
  grpc_core::Timestamp next_ping =
      t->flow_control.bdp_estimator()->CompletePing();
  grpc_chttp2_act_on_flowctl_action(t->flow_control.PeriodicUpdate(), t,
                                    nullptr);
  GPR_ASSERT(!t->next_bdp_ping_timer_handle.has_value());
  t->next_bdp_ping_timer_handle =
      t->event_engine->RunAfter(next_ping - grpc_core::Timestamp::Now(), [t] {
        grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
        grpc_core::ExecCtx exec_ctx;
        next_bdp_ping_timer_expired(t);
      });
}

static void next_bdp_ping_timer_expired(grpc_chttp2_transport* t) {
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&t->next_bdp_ping_timer_expired_locked,
                        next_bdp_ping_timer_expired_locked, t, nullptr),
      absl::OkStatus());
}

static void next_bdp_ping_timer_expired_locked(
    void* tp, GRPC_UNUSED grpc_error_handle error) {
  GPR_DEBUG_ASSERT(error.ok());
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(tp);
  GPR_ASSERT(t->next_bdp_ping_timer_handle.has_value());
  t->next_bdp_ping_timer_handle.reset();
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
  grpc_chttp2_config_default_keepalive_args(grpc_core::ChannelArgs::FromC(args),
                                            is_client);
}

void grpc_chttp2_config_default_keepalive_args(
    const grpc_core::ChannelArgs& channel_args, bool is_client) {
  const auto keepalive_time =
      std::max(grpc_core::Duration::Milliseconds(1),
               channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIME_MS)
                   .value_or(is_client ? g_default_client_keepalive_time
                                       : g_default_server_keepalive_time));
  if (is_client) {
    g_default_client_keepalive_time = keepalive_time;
  } else {
    g_default_server_keepalive_time = keepalive_time;
  }

  const auto keepalive_timeout = std::max(
      grpc_core::Duration::Zero(),
      channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIMEOUT_MS)
          .value_or(is_client ? g_default_client_keepalive_timeout
                              : g_default_server_keepalive_timeout));
  if (is_client) {
    g_default_client_keepalive_timeout = keepalive_timeout;
  } else {
    g_default_server_keepalive_timeout = keepalive_timeout;
  }

  const bool keepalive_permit_without_calls =
      channel_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
          .value_or(is_client
                        ? g_default_client_keepalive_permit_without_calls
                        : g_default_server_keepalive_permit_without_calls);
  if (is_client) {
    g_default_client_keepalive_permit_without_calls =
        keepalive_permit_without_calls;
  } else {
    g_default_server_keepalive_permit_without_calls =
        keepalive_permit_without_calls;
  }

  g_default_max_ping_strikes =
      std::max(0, channel_args.GetInt(GRPC_ARG_HTTP2_MAX_PING_STRIKES)
                      .value_or(g_default_max_ping_strikes));

  g_default_max_pings_without_data =
      std::max(0, channel_args.GetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA)
                      .value_or(g_default_max_pings_without_data));

  g_default_min_recv_ping_interval_without_data =
      std::max(grpc_core::Duration::Zero(),
               channel_args
                   .GetDurationFromIntMillis(
                       GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)
                   .value_or(g_default_min_recv_ping_interval_without_data));
}

static void init_keepalive_ping(grpc_chttp2_transport* t) {
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->init_keepalive_ping_locked,
                                     init_keepalive_ping_locked, t, nullptr),
                   absl::OkStatus());
}

static void init_keepalive_ping_locked(void* arg,
                                       GRPC_UNUSED grpc_error_handle error) {
  GPR_DEBUG_ASSERT(error.ok());
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  GPR_ASSERT(t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_WAITING);
  GPR_ASSERT(t->keepalive_ping_timer_handle.has_value());
  t->keepalive_ping_timer_handle.reset();
  if (t->destroying || !t->closed_with_error.ok()) {
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
  } else {
    if (t->keepalive_permit_without_calls || !t->stream_map.empty()) {
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_PINGING;
      GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive ping end");
      send_keepalive_ping_locked(t);
      grpc_chttp2_initiate_write(t, GRPC_CHTTP2_INITIATE_WRITE_KEEPALIVE_PING);
    } else {
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      t->keepalive_ping_timer_handle =
          t->event_engine->RunAfter(t->keepalive_time, [t] {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            init_keepalive_ping(t);
          });
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "init keepalive ping");
}

static void start_keepalive_ping(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->start_keepalive_ping_locked,
                                     start_keepalive_ping_locked, t, nullptr),
                   error);
}

static void start_keepalive_ping_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (!error.ok()) {
    return;
  }
  if (t->channelz_socket != nullptr) {
    t->channelz_socket->RecordKeepaliveSent();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace) ||
      GRPC_TRACE_FLAG_ENABLED(grpc_keepalive_trace)) {
    gpr_log(GPR_INFO, "%s: Start keepalive ping",
            std::string(t->peer_string.as_string_view()).c_str());
  }
  GRPC_CHTTP2_REF_TRANSPORT(t, "keepalive watchdog");
  t->keepalive_watchdog_timer_handle =
      t->event_engine->RunAfter(t->keepalive_timeout, [t] {
        grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
        grpc_core::ExecCtx exec_ctx;
        keepalive_watchdog_fired(t);
      });
  t->keepalive_ping_started = true;
}

static void finish_keepalive_ping(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  t->combiner->Run(GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                                     finish_keepalive_ping_locked, t, nullptr),
                   error);
}

static void finish_keepalive_ping_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    if (error.ok()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace) ||
          GRPC_TRACE_FLAG_ENABLED(grpc_keepalive_trace)) {
        gpr_log(GPR_INFO, "%s: Finish keepalive ping",
                std::string(t->peer_string.as_string_view()).c_str());
      }
      if (!t->keepalive_ping_started) {
        // start_keepalive_ping_locked has not run yet. Reschedule
        // finish_keepalive_ping_locked for it to be run later.
        t->combiner->Run(
            GRPC_CLOSURE_INIT(&t->finish_keepalive_ping_locked,
                              finish_keepalive_ping_locked, t, nullptr),
            error);
        return;
      }
      t->keepalive_ping_started = false;
      t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_WAITING;
      if (t->keepalive_watchdog_timer_handle.has_value()) {
        if (t->event_engine->Cancel(*t->keepalive_watchdog_timer_handle)) {
          GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive watchdog");
          t->keepalive_watchdog_timer_handle.reset();
        }
      }
      GPR_ASSERT(!t->keepalive_ping_timer_handle.has_value());
      GRPC_CHTTP2_REF_TRANSPORT(t, "init keepalive ping");
      t->keepalive_ping_timer_handle =
          t->event_engine->RunAfter(t->keepalive_time, [t] {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            init_keepalive_ping(t);
          });
    }
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive ping end");
}

static void keepalive_watchdog_fired(grpc_chttp2_transport* t) {
  t->combiner->Run(
      GRPC_CLOSURE_INIT(&t->keepalive_watchdog_fired_locked,
                        keepalive_watchdog_fired_locked, t, nullptr),
      absl::OkStatus());
}

static void keepalive_watchdog_fired_locked(
    void* arg, GRPC_UNUSED grpc_error_handle error) {
  GPR_DEBUG_ASSERT(error.ok());
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  GPR_ASSERT(t->keepalive_watchdog_timer_handle.has_value());
  t->keepalive_watchdog_timer_handle.reset();
  if (t->keepalive_state == GRPC_CHTTP2_KEEPALIVE_STATE_PINGING) {
    gpr_log(GPR_INFO, "%s: Keepalive watchdog fired. Closing transport.",
            std::string(t->peer_string.as_string_view()).c_str());
    t->keepalive_state = GRPC_CHTTP2_KEEPALIVE_STATE_DYING;
    close_transport_locked(
        t, grpc_error_set_int(GRPC_ERROR_CREATE("keepalive watchdog timeout"),
                              grpc_core::StatusIntProperty::kRpcStatus,
                              GRPC_STATUS_UNAVAILABLE));
  } else {
    // If keepalive_state is not PINGING, we consider it as an error. Maybe the
    // cancellation failed in finish_keepalive_ping_locked. Users have seen
    // other states: https://github.com/grpc/grpc/issues/32085.
    gpr_log(GPR_ERROR, "keepalive_ping_end state error: %d (expect: %d)",
            t->keepalive_state, GRPC_CHTTP2_KEEPALIVE_STATE_PINGING);
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "keepalive watchdog");
}

static void maybe_reset_keepalive_ping_timer_locked(grpc_chttp2_transport* t) {
  if (t->keepalive_ping_timer_handle.has_value()) {
    if (t->event_engine->Cancel(*t->keepalive_ping_timer_handle)) {
      // Cancel succeeds, resets the keepalive ping timer. Note that we don't
      // need to Ref or Unref here since we still hold the Ref.
      if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace) ||
          GRPC_TRACE_FLAG_ENABLED(grpc_keepalive_trace)) {
        gpr_log(GPR_INFO, "%s: Keepalive ping cancelled. Resetting timer.",
                std::string(t->peer_string.as_string_view()).c_str());
      }
      t->keepalive_ping_timer_handle =
          t->event_engine->RunAfter(t->keepalive_time, [t] {
            grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
            grpc_core::ExecCtx exec_ctx;
            init_keepalive_ping(t);
          });
    }
  }
}

//
// CALLBACK LOOP
//

static void connectivity_state_set(grpc_chttp2_transport* t,
                                   grpc_connectivity_state state,
                                   const absl::Status& status,
                                   const char* reason) {
  GRPC_CHTTP2_IF_TRACING(gpr_log(
      GPR_INFO, "transport %p set connectivity_state=%d; status=%s; reason=%s",
      t, state, status.ToString().c_str(), reason));
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
            t->combiner->Run(&t->benign_reclaimer_locked, absl::OkStatus());
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
            t->combiner->Run(&t->destructive_reclaimer_locked,
                             absl::OkStatus());
          } else {
            GRPC_CHTTP2_UNREF_TRANSPORT(t, "destructive_reclaimer");
          }
        });
  }
}

static void benign_reclaimer_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  if (error.ok() && t->stream_map.empty()) {
    // Channel with no active streams: send a goaway to try and make it
    // disconnect cleanly
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      gpr_log(GPR_INFO, "HTTP2: %s - send goaway to free memory",
              std::string(t->peer_string.as_string_view()).c_str());
    }
    send_goaway(t,
                grpc_error_set_int(GRPC_ERROR_CREATE("Buffers full"),
                                   grpc_core::StatusIntProperty::kHttp2Error,
                                   GRPC_HTTP2_ENHANCE_YOUR_CALM),
                /*immediate_disconnect_hint=*/true);
  } else if (error.ok() && GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO,
            "HTTP2: %s - skip benign reclamation, there are still %" PRIdPTR
            " streams",
            std::string(t->peer_string.as_string_view()).c_str(),
            t->stream_map.size());
  }
  t->benign_reclaimer_registered = false;
  if (error != absl::CancelledError()) {
    t->active_reclamation.Finish();
  }
  GRPC_CHTTP2_UNREF_TRANSPORT(t, "benign_reclaimer");
}

static void destructive_reclaimer_locked(void* arg, grpc_error_handle error) {
  grpc_chttp2_transport* t = static_cast<grpc_chttp2_transport*>(arg);
  t->destructive_reclaimer_registered = false;
  if (error.ok() && !t->stream_map.empty()) {
    // As stream_map is a hash map, this selects effectively a random stream.
    grpc_chttp2_stream* s = t->stream_map.begin()->second;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      gpr_log(GPR_INFO, "HTTP2: %s - abandon stream id %d",
              std::string(t->peer_string.as_string_view()).c_str(), s->id);
    }
    grpc_chttp2_cancel_stream(
        t, s,
        grpc_error_set_int(GRPC_ERROR_CREATE("Buffers full"),
                           grpc_core::StatusIntProperty::kHttp2Error,
                           GRPC_HTTP2_ENHANCE_YOUR_CALM));
    if (!t->stream_map.empty()) {
      // Since we cancel one stream per destructive reclamation, if
      //   there are more streams left, we can immediately post a new
      //   reclaimer in case the resource quota needs to free more
      //   memory
      post_destructive_reclaimer(t);
    }
  }
  if (error != absl::CancelledError()) {
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
                                             false,
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
      t, "reading_action");  // matches unref inside reading_action
  if (read_buffer != nullptr) {
    grpc_slice_buffer_move_into(read_buffer, &t->read_buffer);
    gpr_free(read_buffer);
  }
  t->combiner->Run(
      grpc_core::NewClosure([t, notify_on_receive_settings,
                             notify_on_close](grpc_error_handle) {
        if (!t->closed_with_error.ok()) {
          if (notify_on_receive_settings != nullptr) {
            grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify_on_receive_settings,
                                    t->closed_with_error);
          }
          if (notify_on_close != nullptr) {
            grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify_on_close,
                                    t->closed_with_error);
          }
          GRPC_CHTTP2_UNREF_TRANSPORT(t, "reading_action");
          return;
        }
        t->notify_on_receive_settings = notify_on_receive_settings;
        t->notify_on_close = notify_on_close;
        read_action_locked(t, absl::OkStatus());
      }),
      absl::OkStatus());
}
