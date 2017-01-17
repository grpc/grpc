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

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/http2_errors.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/status_conversion.h"
#include "src/core/ext/transport/chttp2/transport/varint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"
#include "src/core/lib/transport/transport_impl.h"

#define DEFAULT_WINDOW 65535
#define DEFAULT_CONNECTION_WINDOW_TARGET (1024 * 1024)
#define MAX_WINDOW 0x7fffffffu
#define MAX_WRITE_BUFFER_SIZE (64 * 1024 * 1024)
#define DEFAULT_MAX_HEADER_LIST_SIZE (16 * 1024)

#define MAX_CLIENT_STREAM_ID 0x7fffffffu
int grpc_http_trace = 0;
int grpc_flowctl_trace = 0;

static const grpc_transport_vtable vtable;

/* forward declarations of various callbacks that we'll build closures around */
static void write_action_begin_locked(grpc_exec_ctx *exec_ctx, void *t,
                                      grpc_error *error);
static void write_action(grpc_exec_ctx *exec_ctx, void *t, grpc_error *error);
static void write_action_end_locked(grpc_exec_ctx *exec_ctx, void *t,
                                    grpc_error *error);

static void read_action_locked(grpc_exec_ctx *exec_ctx, void *t,
                               grpc_error *error);

static void complete_fetch_locked(grpc_exec_ctx *exec_ctx, void *gs,
                                  grpc_error *error);
/** Set a transport level setting, and push it to our peer */
static void push_setting(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                         grpc_chttp2_setting_id id, uint32_t value);

static void close_from_api(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                           grpc_chttp2_stream *s, grpc_error *error);

/** Start new streams that have been created if we can */
static void maybe_start_some_streams(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t);

static void connectivity_state_set(grpc_exec_ctx *exec_ctx,
                                   grpc_chttp2_transport *t,
                                   grpc_connectivity_state state,
                                   grpc_error *error, const char *reason);

static void incoming_byte_stream_update_flow_control(grpc_exec_ctx *exec_ctx,
                                                     grpc_chttp2_transport *t,
                                                     grpc_chttp2_stream *s,
                                                     size_t max_size_hint,
                                                     size_t have_already);
static void incoming_byte_stream_destroy_locked(grpc_exec_ctx *exec_ctx,
                                                void *byte_stream,
                                                grpc_error *error_ignored);

static void benign_reclaimer_locked(grpc_exec_ctx *exec_ctx, void *t,
                                    grpc_error *error);
static void destructive_reclaimer_locked(grpc_exec_ctx *exec_ctx, void *t,
                                         grpc_error *error);

static void post_benign_reclaimer(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t);
static void post_destructive_reclaimer(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_transport *t);

static void close_transport_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_chttp2_transport *t, grpc_error *error);
static void end_all_the_calls(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                              grpc_error *error);

static void start_bdp_ping_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                  grpc_error *error);
static void finish_bdp_ping_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                   grpc_error *error);

static void cancel_pings(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                         grpc_error *error);
static void send_ping_locked(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                             grpc_chttp2_ping_type ping_type,
                             grpc_closure *on_initiate,
                             grpc_closure *on_complete);

#define DEFAULT_MIN_TIME_BETWEEN_PINGS_MS 100
#define DEFAULT_MAX_PINGS_BETWEEN_DATA 1

/*******************************************************************************
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

static void destruct_transport(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport *t) {
  size_t i;

  grpc_endpoint_destroy(exec_ctx, t->ep);

  grpc_slice_buffer_destroy_internal(exec_ctx, &t->qbuf);

  grpc_slice_buffer_destroy_internal(exec_ctx, &t->outbuf);
  grpc_chttp2_hpack_compressor_destroy(exec_ctx, &t->hpack_compressor);

  grpc_slice_buffer_destroy_internal(exec_ctx, &t->read_buffer);
  grpc_chttp2_hpack_parser_destroy(exec_ctx, &t->hpack_parser);
  grpc_chttp2_goaway_parser_destroy(&t->goaway_parser);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(t->lists[i].head == NULL);
    GPR_ASSERT(t->lists[i].tail == NULL);
  }

  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&t->stream_map);
  grpc_connectivity_state_destroy(exec_ctx, &t->channel_callback.state_tracker);

  grpc_combiner_destroy(exec_ctx, t->combiner);

  cancel_pings(exec_ctx, t, GRPC_ERROR_CREATE("Transport destroyed"));

  while (t->write_cb_pool) {
    grpc_chttp2_write_cb *next = t->write_cb_pool->next;
    gpr_free(t->write_cb_pool);
    t->write_cb_pool = next;
  }

  gpr_free(t->peer_string);
  gpr_free(t);
}

#ifdef GRPC_CHTTP2_REFCOUNTING_DEBUG
void grpc_chttp2_unref_transport(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport *t, const char *reason,
                                 const char *file, int line) {
  gpr_log(GPR_DEBUG, "chttp2:unref:%p %" PRIdPTR "->%" PRIdPTR " %s [%s:%d]", t,
          t->refs.count, t->refs.count - 1, reason, file, line);
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(exec_ctx, t);
}

void grpc_chttp2_ref_transport(grpc_chttp2_transport *t, const char *reason,
                               const char *file, int line) {
  gpr_log(GPR_DEBUG, "chttp2:  ref:%p %" PRIdPTR "->%" PRIdPTR " %s [%s:%d]", t,
          t->refs.count, t->refs.count + 1, reason, file, line);
  gpr_ref(&t->refs);
}
#else
void grpc_chttp2_unref_transport(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport *t) {
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(exec_ctx, t);
}

void grpc_chttp2_ref_transport(grpc_chttp2_transport *t) { gpr_ref(&t->refs); }
#endif

static void init_transport(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                           const grpc_channel_args *channel_args,
                           grpc_endpoint *ep, bool is_client) {
  size_t i;
  int j;

  GPR_ASSERT(strlen(GRPC_CHTTP2_CLIENT_CONNECT_STRING) ==
             GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);

  memset(t, 0, sizeof(*t));

  t->base.vtable = &vtable;
  t->ep = ep;
  /* one ref is for destroy */
  gpr_ref_init(&t->refs, 1);
  t->combiner = grpc_combiner_create(grpc_endpoint_get_workqueue(ep));
  t->peer_string = grpc_endpoint_get_peer(ep);
  t->endpoint_reading = 1;
  t->next_stream_id = is_client ? 1 : 2;
  t->is_client = is_client;
  t->outgoing_window = DEFAULT_WINDOW;
  t->incoming_window = DEFAULT_WINDOW;
  t->deframe_state = is_client ? GRPC_DTS_FH_0 : GRPC_DTS_CLIENT_PREFIX_0;
  t->is_first_frame = true;
  grpc_connectivity_state_init(
      &t->channel_callback.state_tracker, GRPC_CHANNEL_READY,
      is_client ? "client_transport" : "server_transport");

  grpc_slice_buffer_init(&t->qbuf);

  grpc_slice_buffer_init(&t->outbuf);
  grpc_chttp2_hpack_compressor_init(&t->hpack_compressor);

  grpc_closure_init(&t->write_action, write_action, t,
                    grpc_schedule_on_exec_ctx);
  grpc_closure_init(&t->read_action_locked, read_action_locked, t,
                    grpc_combiner_scheduler(t->combiner, false));
  grpc_closure_init(&t->benign_reclaimer_locked, benign_reclaimer_locked, t,
                    grpc_combiner_scheduler(t->combiner, false));
  grpc_closure_init(&t->destructive_reclaimer_locked,
                    destructive_reclaimer_locked, t,
                    grpc_combiner_scheduler(t->combiner, false));
  grpc_closure_init(&t->start_bdp_ping_locked, start_bdp_ping_locked, t,
                    grpc_combiner_scheduler(t->combiner, false));
  grpc_closure_init(&t->finish_bdp_ping_locked, finish_bdp_ping_locked, t,
                    grpc_combiner_scheduler(t->combiner, false));

  grpc_bdp_estimator_init(&t->bdp_estimator);
  t->last_bdp_ping_finished = gpr_now(GPR_CLOCK_MONOTONIC);
  t->last_pid_update = t->last_bdp_ping_finished;
  grpc_pid_controller_init(
      &t->pid_controller,
      (grpc_pid_controller_args){.gain_p = 4,
                                 .gain_i = 8,
                                 .gain_d = 0,
                                 .initial_control_value = log2(DEFAULT_WINDOW),
                                 .min_control_value = -1,
                                 .max_control_value = 22,
                                 .integral_range = 10});

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
  t->write_buffer_size = DEFAULT_WINDOW;

  if (is_client) {
    grpc_slice_buffer_add(&t->outbuf, grpc_slice_from_copied_string(
                                          GRPC_CHTTP2_CLIENT_CONNECT_STRING));
    grpc_chttp2_initiate_write(exec_ctx, t, false, "initial_write");
  }

  /* configure http2 the way we like it */
  if (is_client) {
    push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_ENABLE_PUSH, 0);
    push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 0);
  }
  push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
               DEFAULT_WINDOW);
  push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
               DEFAULT_MAX_HEADER_LIST_SIZE);

  t->ping_policy = (grpc_chttp2_repeated_ping_policy){
      .max_pings_without_data = DEFAULT_MAX_PINGS_BETWEEN_DATA,
      .min_time_between_pings =
          gpr_time_from_millis(DEFAULT_MIN_TIME_BETWEEN_PINGS_MS, GPR_TIMESPAN),
  };

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
            (grpc_integer_options){DEFAULT_MAX_PINGS_BETWEEN_DATA, 0, INT_MAX});
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_MIN_TIME_BETWEEN_PINGS_MS)) {
        t->ping_policy.min_time_between_pings = gpr_time_from_millis(
            grpc_channel_arg_get_integer(
                &channel_args->args[i],
                (grpc_integer_options){DEFAULT_MIN_TIME_BETWEEN_PINGS_MS, 0,
                                       INT_MAX}),
            GPR_TIMESPAN);
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE)) {
        t->write_buffer_size = (uint32_t)grpc_channel_arg_get_integer(
            &channel_args->args[i],
            (grpc_integer_options){0, 0, MAX_WRITE_BUFFER_SIZE});
      } else {
        static const struct {
          const char *channel_arg_name;
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
                push_setting(exec_ctx, t, settings_map[j].setting_id,
                             (uint32_t)value);
              }
            }
            break;
          }
        }
      }
    }
  }

  grpc_chttp2_initiate_write(exec_ctx, t, false, "init");
  post_benign_reclaimer(exec_ctx, t);
}

static void destroy_transport_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                     grpc_error *error) {
  grpc_chttp2_transport *t = tp;
  t->destroying = 1;
  close_transport_locked(
      exec_ctx, t,
      grpc_error_set_int(GRPC_ERROR_CREATE("Transport destroyed"),
                         GRPC_ERROR_INT_OCCURRED_DURING_WRITE, t->write_state));
  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "destroy");
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_closure_sched(exec_ctx, grpc_closure_create(
                                   destroy_transport_locked, t,
                                   grpc_combiner_scheduler(t->combiner, false)),
                     GRPC_ERROR_NONE);
}

static void close_transport_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_chttp2_transport *t,
                                   grpc_error *error) {
  if (!t->closed) {
    if (t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE) {
      if (t->close_transport_on_writes_finished == NULL) {
        t->close_transport_on_writes_finished =
            GRPC_ERROR_CREATE("Delayed close due to in-progress write");
      }
      t->close_transport_on_writes_finished =
          grpc_error_add_child(t->close_transport_on_writes_finished, error);
      return;
    }
    if (!grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, NULL)) {
      error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                                 GRPC_STATUS_UNAVAILABLE);
    }
    t->closed = 1;
    connectivity_state_set(exec_ctx, t, GRPC_CHANNEL_SHUTDOWN,
                           GRPC_ERROR_REF(error), "close_transport");
    grpc_endpoint_shutdown(exec_ctx, t->ep);

    /* flush writable stream list to avoid dangling references */
    grpc_chttp2_stream *s;
    while (grpc_chttp2_list_pop_writable_stream(t, &s)) {
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2_writing:close");
    }
    end_all_the_calls(exec_ctx, t, GRPC_ERROR_REF(error));
    cancel_pings(exec_ctx, t, GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_chttp2_stream_ref(grpc_chttp2_stream *s, const char *reason) {
  grpc_stream_ref(s->refcount, reason);
}
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx, grpc_chttp2_stream *s,
                              const char *reason) {
  grpc_stream_unref(exec_ctx, s->refcount, reason);
}
#else
void grpc_chttp2_stream_ref(grpc_chttp2_stream *s) {
  grpc_stream_ref(s->refcount);
}
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx, grpc_chttp2_stream *s) {
  grpc_stream_unref(exec_ctx, s->refcount);
}
#endif

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, grpc_stream_refcount *refcount,
                       const void *server_data) {
  GPR_TIMER_BEGIN("init_stream", 0);
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  memset(s, 0, sizeof(*s));

  s->t = t;
  s->refcount = refcount;
  /* We reserve one 'active stream' that's dropped when the stream is
     read-closed. The others are for incoming_byte_streams that are actively
     reading */
  gpr_ref_init(&s->active_streams, 1);
  GRPC_CHTTP2_STREAM_REF(s, "chttp2");

  grpc_chttp2_incoming_metadata_buffer_init(&s->metadata_buffer[0]);
  grpc_chttp2_incoming_metadata_buffer_init(&s->metadata_buffer[1]);
  grpc_chttp2_data_parser_init(&s->data_parser);
  grpc_slice_buffer_init(&s->flow_controlled_buffer);
  s->deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  grpc_closure_init(&s->complete_fetch_locked, complete_fetch_locked, s,
                    grpc_schedule_on_exec_ctx);

  GRPC_CHTTP2_REF_TRANSPORT(t, "stream");

  if (server_data) {
    s->id = (uint32_t)(uintptr_t)server_data;
    *t->accepting_stream = s;
    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
    post_destructive_reclaimer(exec_ctx, t);
  }

  GPR_TIMER_END("init_stream", 0);

  return 0;
}

static void destroy_stream_locked(grpc_exec_ctx *exec_ctx, void *sp,
                                  grpc_error *error) {
  grpc_byte_stream *bs;
  grpc_chttp2_stream *s = sp;
  grpc_chttp2_transport *t = s->t;

  GPR_TIMER_BEGIN("destroy_stream", 0);

  GPR_ASSERT((s->write_closed && s->read_closed) || s->id == 0);
  if (s->id != 0) {
    GPR_ASSERT(grpc_chttp2_stream_map_find(&t->stream_map, s->id) == NULL);
  }

  while ((bs = grpc_chttp2_incoming_frame_queue_pop(&s->incoming_frames))) {
    incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
  }

  grpc_chttp2_list_remove_stalled_by_transport(t, s);
  grpc_chttp2_list_remove_stalled_by_stream(t, s);

  for (int i = 0; i < STREAM_LIST_COUNT; i++) {
    if (s->included[i]) {
      gpr_log(GPR_ERROR, "%s stream %d still included in list %d",
              t->is_client ? "client" : "server", s->id, i);
      abort();
    }
  }

  GPR_ASSERT(s->send_initial_metadata_finished == NULL);
  GPR_ASSERT(s->fetching_send_message == NULL);
  GPR_ASSERT(s->send_trailing_metadata_finished == NULL);
  GPR_ASSERT(s->recv_initial_metadata_ready == NULL);
  GPR_ASSERT(s->recv_message_ready == NULL);
  GPR_ASSERT(s->recv_trailing_metadata_finished == NULL);
  grpc_chttp2_data_parser_destroy(exec_ctx, &s->data_parser);
  grpc_chttp2_incoming_metadata_buffer_destroy(exec_ctx,
                                               &s->metadata_buffer[0]);
  grpc_chttp2_incoming_metadata_buffer_destroy(exec_ctx,
                                               &s->metadata_buffer[1]);
  grpc_slice_buffer_destroy_internal(exec_ctx, &s->flow_controlled_buffer);
  GRPC_ERROR_UNREF(s->read_closed_error);
  GRPC_ERROR_UNREF(s->write_closed_error);

  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "stream");

  GPR_TIMER_END("destroy_stream", 0);

  gpr_free(s->destroy_stream_arg);
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs, void *and_free_memory) {
  GPR_TIMER_BEGIN("destroy_stream", 0);
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  s->destroy_stream_arg = and_free_memory;
  grpc_closure_sched(
      exec_ctx, grpc_closure_init(&s->destroy_stream, destroy_stream_locked, s,
                                  grpc_combiner_scheduler(t->combiner, false)),
      GRPC_ERROR_NONE);
  GPR_TIMER_END("destroy_stream", 0);
}

grpc_chttp2_stream *grpc_chttp2_parsing_lookup_stream(grpc_chttp2_transport *t,
                                                      uint32_t id) {
  return grpc_chttp2_stream_map_find(&t->stream_map, id);
}

grpc_chttp2_stream *grpc_chttp2_parsing_accept_stream(grpc_exec_ctx *exec_ctx,
                                                      grpc_chttp2_transport *t,
                                                      uint32_t id) {
  if (t->channel_callback.accept_stream == NULL) {
    return NULL;
  }
  grpc_chttp2_stream *accepting;
  GPR_ASSERT(t->accepting_stream == NULL);
  t->accepting_stream = &accepting;
  t->channel_callback.accept_stream(exec_ctx,
                                    t->channel_callback.accept_stream_user_data,
                                    &t->base, (void *)(uintptr_t)id);
  t->accepting_stream = NULL;
  return accepting;
}

/*******************************************************************************
 * OUTPUT PROCESSING
 */

static const char *write_state_name(grpc_chttp2_write_state st) {
  switch (st) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      return "IDLE";
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      return "WRITING";
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
      return "WRITING+MORE";
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE_AND_COVERED_BY_POLLER:
      return "WRITING+MORE+COVERED";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

static void set_write_state(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                            grpc_chttp2_write_state st, const char *reason) {
  GRPC_CHTTP2_IF_TRACING(gpr_log(GPR_DEBUG, "W:%p %s state %s -> %s [%s]", t,
                                 t->is_client ? "CLIENT" : "SERVER",
                                 write_state_name(t->write_state),
                                 write_state_name(st), reason));
  t->write_state = st;
  if (st == GRPC_CHTTP2_WRITE_STATE_IDLE) {
    grpc_closure_list_sched(exec_ctx, &t->run_after_write);
    if (t->close_transport_on_writes_finished != NULL) {
      grpc_error *err = t->close_transport_on_writes_finished;
      t->close_transport_on_writes_finished = NULL;
      close_transport_locked(exec_ctx, t, err);
    }
  }
}

void grpc_chttp2_initiate_write(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_transport *t,
                                bool covered_by_poller, const char *reason) {
  GPR_TIMER_BEGIN("grpc_chttp2_initiate_write", 0);

  switch (t->write_state) {
    case GRPC_CHTTP2_WRITE_STATE_IDLE:
      set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_WRITING, reason);
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      grpc_closure_sched(
          exec_ctx,
          grpc_closure_init(
              &t->write_action_begin_locked, write_action_begin_locked, t,
              grpc_combiner_finally_scheduler(t->combiner, covered_by_poller)),
          GRPC_ERROR_NONE);
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING:
      set_write_state(
          exec_ctx, t,
          covered_by_poller
              ? GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE_AND_COVERED_BY_POLLER
              : GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE,
          reason);
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE:
      if (covered_by_poller) {
        set_write_state(
            exec_ctx, t,
            GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE_AND_COVERED_BY_POLLER,
            reason);
      }
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE_AND_COVERED_BY_POLLER:
      break;
  }
  GPR_TIMER_END("grpc_chttp2_initiate_write", 0);
}

void grpc_chttp2_become_writable(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport *t,
                                 grpc_chttp2_stream *s, bool covered_by_poller,
                                 const char *reason) {
  if (!t->closed && grpc_chttp2_list_add_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_REF(s, "chttp2_writing:become");
    grpc_chttp2_initiate_write(exec_ctx, t, covered_by_poller, reason);
  }
}

static void write_action_begin_locked(grpc_exec_ctx *exec_ctx, void *gt,
                                      grpc_error *error_ignored) {
  GPR_TIMER_BEGIN("write_action_begin_locked", 0);
  grpc_chttp2_transport *t = gt;
  GPR_ASSERT(t->write_state != GRPC_CHTTP2_WRITE_STATE_IDLE);
  if (!t->closed && grpc_chttp2_begin_write(exec_ctx, t)) {
    set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_WRITING,
                    "begin writing");
    grpc_closure_sched(exec_ctx, &t->write_action, GRPC_ERROR_NONE);
  } else {
    set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_IDLE,
                    "begin writing nothing");
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "writing");
  }
  GPR_TIMER_END("write_action_begin_locked", 0);
}

static void write_action(grpc_exec_ctx *exec_ctx, void *gt, grpc_error *error) {
  grpc_chttp2_transport *t = gt;
  GPR_TIMER_BEGIN("write_action", 0);
  grpc_endpoint_write(
      exec_ctx, t->ep, &t->outbuf,
      grpc_closure_init(&t->write_action_end_locked, write_action_end_locked, t,
                        grpc_combiner_scheduler(t->combiner, false)));
  GPR_TIMER_END("write_action", 0);
}

static void write_action_end_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                    grpc_error *error) {
  GPR_TIMER_BEGIN("terminate_writing_with_lock", 0);
  grpc_chttp2_transport *t = tp;

  if (error != GRPC_ERROR_NONE) {
    close_transport_locked(exec_ctx, t, GRPC_ERROR_REF(error));
  }

  if (t->sent_goaway_state == GRPC_CHTTP2_GOAWAY_SEND_SCHEDULED) {
    t->sent_goaway_state = GRPC_CHTTP2_GOAWAY_SENT;
    if (grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
      close_transport_locked(exec_ctx, t, GRPC_ERROR_CREATE("goaway sent"));
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
                      "continue writing [!covered]");
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      grpc_closure_run(
          exec_ctx,
          grpc_closure_init(
              &t->write_action_begin_locked, write_action_begin_locked, t,
              grpc_combiner_finally_scheduler(t->combiner, false)),
          GRPC_ERROR_NONE);
      break;
    case GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE_AND_COVERED_BY_POLLER:
      GPR_TIMER_MARK("state=writing_stale_with_poller", 0);
      set_write_state(exec_ctx, t, GRPC_CHTTP2_WRITE_STATE_WRITING,
                      "continue writing [covered]");
      GRPC_CHTTP2_REF_TRANSPORT(t, "writing");
      grpc_closure_run(
          exec_ctx,
          grpc_closure_init(&t->write_action_begin_locked,
                            write_action_begin_locked, t,
                            grpc_combiner_finally_scheduler(t->combiner, true)),
          GRPC_ERROR_NONE);
      break;
  }

  grpc_chttp2_end_write(exec_ctx, t, GRPC_ERROR_REF(error));

  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "writing");
  GPR_TIMER_END("terminate_writing_with_lock", 0);
}

static void push_setting(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                         grpc_chttp2_setting_id id, uint32_t value) {
  const grpc_chttp2_setting_parameters *sp =
      &grpc_chttp2_settings_parameters[id];
  uint32_t use_value = GPR_CLAMP(value, sp->min_value, sp->max_value);
  if (use_value != value) {
    gpr_log(GPR_INFO, "Requested parameter %s clamped from %d to %d", sp->name,
            value, use_value);
  }
  if (use_value != t->settings[GRPC_LOCAL_SETTINGS][id]) {
    t->settings[GRPC_LOCAL_SETTINGS][id] = use_value;
    t->dirtied_local_settings = 1;
    grpc_chttp2_initiate_write(exec_ctx, t, false, "push_setting");
  }
}

void grpc_chttp2_add_incoming_goaway(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     uint32_t goaway_error,
                                     grpc_slice goaway_text) {
  char *msg = grpc_dump_slice(goaway_text, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_DEBUG, "got goaway [%d]: %s", goaway_error, msg));
  grpc_slice_unref_internal(exec_ctx, goaway_text);
  t->seen_goaway = 1;
  /* lie: use transient failure from the transport to indicate goaway has been
   * received */
  connectivity_state_set(
      exec_ctx, t, GRPC_CHANNEL_TRANSIENT_FAILURE,
      grpc_error_set_str(
          grpc_error_set_int(GRPC_ERROR_CREATE("GOAWAY received"),
                             GRPC_ERROR_INT_HTTP2_ERROR,
                             (intptr_t)goaway_error),
          GRPC_ERROR_STR_RAW_BYTES, msg),
      "got_goaway");
  gpr_free(msg);
}

static void maybe_start_some_streams(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t) {
  grpc_chttp2_stream *s;
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
      connectivity_state_set(exec_ctx, t, GRPC_CHANNEL_TRANSIENT_FAILURE,
                             GRPC_ERROR_CREATE("Stream IDs exhausted"),
                             "no_more_stream_ids");
    }

    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
    post_destructive_reclaimer(exec_ctx, t);
    grpc_chttp2_become_writable(exec_ctx, t, s, true, "new_stream");
  }
  /* cancel out streams that will never be started */
  while (t->next_stream_id >= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_list_pop_waiting_for_concurrency(t, &s)) {
    grpc_chttp2_cancel_stream(
        exec_ctx, t, s,
        grpc_error_set_int(GRPC_ERROR_CREATE("Stream IDs exhausted"),
                           GRPC_ERROR_INT_GRPC_STATUS,
                           GRPC_STATUS_UNAVAILABLE));
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

static grpc_closure *add_closure_barrier(grpc_closure *closure) {
  closure->next_data.scratch += CLOSURE_BARRIER_FIRST_REF_BIT;
  return closure;
}

static void null_then_run_closure(grpc_exec_ctx *exec_ctx,
                                  grpc_closure **closure, grpc_error *error) {
  grpc_closure *c = *closure;
  *closure = NULL;
  grpc_closure_run(exec_ctx, c, error);
}

void grpc_chttp2_complete_closure_step(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_transport *t,
                                       grpc_chttp2_stream *s,
                                       grpc_closure **pclosure,
                                       grpc_error *error, const char *desc) {
  grpc_closure *closure = *pclosure;
  *pclosure = NULL;
  if (closure == NULL) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  closure->next_data.scratch -= CLOSURE_BARRIER_FIRST_REF_BIT;
  if (grpc_http_trace) {
    const char *errstr = grpc_error_string(error);
    gpr_log(GPR_DEBUG,
            "complete_closure_step: %p refs=%d flags=0x%04x desc=%s err=%s",
            closure,
            (int)(closure->next_data.scratch / CLOSURE_BARRIER_FIRST_REF_BIT),
            (int)(closure->next_data.scratch % CLOSURE_BARRIER_FIRST_REF_BIT),
            desc, errstr);
    grpc_error_free_string(errstr);
  }
  if (error != GRPC_ERROR_NONE) {
    if (closure->error_data.error == GRPC_ERROR_NONE) {
      closure->error_data.error =
          GRPC_ERROR_CREATE("Error in HTTP transport completing operation");
      closure->error_data.error =
          grpc_error_set_str(closure->error_data.error,
                             GRPC_ERROR_STR_TARGET_ADDRESS, t->peer_string);
    }
    closure->error_data.error =
        grpc_error_add_child(closure->error_data.error, error);
  }
  if (closure->next_data.scratch < CLOSURE_BARRIER_FIRST_REF_BIT) {
    if (closure->next_data.scratch & CLOSURE_BARRIER_STATS_BIT) {
      grpc_transport_move_stats(&s->stats, s->collecting_stats);
      s->collecting_stats = NULL;
    }
    if ((t->write_state == GRPC_CHTTP2_WRITE_STATE_IDLE) ||
        !(closure->next_data.scratch & CLOSURE_BARRIER_MAY_COVER_WRITE)) {
      grpc_closure_run(exec_ctx, closure, closure->error_data.error);
    } else {
      grpc_closure_list_append(&t->run_after_write, closure,
                               closure->error_data.error);
    }
  }
}

static bool contains_non_ok_status(grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  for (l = batch->list.head; l; l = l->next) {
    if (l->md->key == GRPC_MDSTR_GRPC_STATUS &&
        l->md != GRPC_MDELEM_GRPC_STATUS_0) {
      return true;
    }
  }
  return false;
}

static void maybe_become_writable_due_to_send_msg(grpc_exec_ctx *exec_ctx,
                                                  grpc_chttp2_transport *t,
                                                  grpc_chttp2_stream *s) {
  if (s->id != 0 && (!s->write_buffering ||
                     s->flow_controlled_buffer.length > t->write_buffer_size)) {
    grpc_chttp2_become_writable(exec_ctx, t, s, true, "op.send_message");
  }
}

static void add_fetched_slice_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_chttp2_stream *s) {
  s->fetched_send_message_length +=
      (uint32_t)GRPC_SLICE_LENGTH(s->fetching_slice);
  grpc_slice_buffer_add(&s->flow_controlled_buffer, s->fetching_slice);
  maybe_become_writable_due_to_send_msg(exec_ctx, t, s);
}

static void continue_fetching_send_locked(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s) {
  for (;;) {
    if (s->fetching_send_message == NULL) {
      /* Stream was cancelled before message fetch completed */
      abort(); /* TODO(ctiller): what cleanup here? */
      return;  /* early out */
    }
    if (s->fetched_send_message_length == s->fetching_send_message->length) {
      int64_t notify_offset = s->next_message_end_offset;
      if (notify_offset <= s->flow_controlled_bytes_written) {
        grpc_chttp2_complete_closure_step(
            exec_ctx, t, s, &s->fetching_send_message_finished, GRPC_ERROR_NONE,
            "fetching_send_message_finished");
      } else {
        grpc_chttp2_write_cb *cb = t->write_cb_pool;
        if (cb == NULL) {
          cb = gpr_malloc(sizeof(*cb));
        } else {
          t->write_cb_pool = cb->next;
        }
        cb->call_at_byte = notify_offset;
        cb->closure = s->fetching_send_message_finished;
        s->fetching_send_message_finished = NULL;
        cb->next = s->on_write_finished_cbs;
        s->on_write_finished_cbs = cb;
      }
      s->fetching_send_message = NULL;
      return; /* early out */
    } else if (grpc_byte_stream_next(exec_ctx, s->fetching_send_message,
                                     &s->fetching_slice, UINT32_MAX,
                                     &s->complete_fetch)) {
      add_fetched_slice_locked(exec_ctx, t, s);
    }
  }
}

static void complete_fetch_locked(grpc_exec_ctx *exec_ctx, void *gs,
                                  grpc_error *error) {
  grpc_chttp2_stream *s = gs;
  grpc_chttp2_transport *t = s->t;
  if (error == GRPC_ERROR_NONE) {
    add_fetched_slice_locked(exec_ctx, t, s);
    continue_fetching_send_locked(exec_ctx, t, s);
  } else {
    /* TODO(ctiller): what to do here */
    abort();
  }
}

static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

static void log_metadata(const grpc_metadata_batch *md_batch, uint32_t id,
                         bool is_client, bool is_initial) {
  for (grpc_linked_mdelem *md = md_batch->list.head; md != md_batch->list.tail;
       md = md->next) {
    gpr_log(GPR_INFO, "HTTP:%d:%s:%s: %s: %s", id, is_initial ? "HDR" : "TRL",
            is_client ? "CLI" : "SVR", grpc_mdstr_as_c_string(md->md->key),
            grpc_mdstr_as_c_string(md->md->value));
  }
}

static void perform_stream_op_locked(grpc_exec_ctx *exec_ctx, void *stream_op,
                                     grpc_error *error_ignored) {
  GPR_TIMER_BEGIN("perform_stream_op_locked", 0);

  grpc_transport_stream_op *op = stream_op;
  grpc_chttp2_transport *t = op->transport_private.args[0];
  grpc_chttp2_stream *s = op->transport_private.args[1];

  if (grpc_http_trace) {
    char *str = grpc_transport_stream_op_string(op);
    gpr_log(GPR_DEBUG, "perform_stream_op_locked: %s; on_complete = %p", str,
            op->on_complete);
    gpr_free(str);
    if (op->send_initial_metadata) {
      log_metadata(op->send_initial_metadata, s->id, t->is_client, true);
    }
    if (op->send_trailing_metadata) {
      log_metadata(op->send_trailing_metadata, s->id, t->is_client, false);
    }
  }

  grpc_closure *on_complete = op->on_complete;
  if (on_complete == NULL) {
    on_complete =
        grpc_closure_create(do_nothing, NULL, grpc_schedule_on_exec_ctx);
  }

  /* use final_data as a barrier until enqueue time; the inital counter is
     dropped at the end of this function */
  on_complete->next_data.scratch = CLOSURE_BARRIER_FIRST_REF_BIT;
  on_complete->error_data.error = GRPC_ERROR_NONE;

  if (op->collect_stats != NULL) {
    GPR_ASSERT(s->collecting_stats == NULL);
    s->collecting_stats = op->collect_stats;
    on_complete->next_data.scratch |= CLOSURE_BARRIER_STATS_BIT;
  }

  if (op->cancel_error != GRPC_ERROR_NONE) {
    grpc_chttp2_cancel_stream(exec_ctx, t, s, GRPC_ERROR_REF(op->cancel_error));
  }

  if (op->close_error != GRPC_ERROR_NONE) {
    close_from_api(exec_ctx, t, s, GRPC_ERROR_REF(op->close_error));
  }

  if (op->send_initial_metadata != NULL) {
    GPR_ASSERT(s->send_initial_metadata_finished == NULL);
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->send_initial_metadata_finished = add_closure_barrier(on_complete);
    s->send_initial_metadata = op->send_initial_metadata;
    const size_t metadata_size =
        grpc_metadata_batch_size(op->send_initial_metadata);
    const size_t metadata_peer_limit =
        t->settings[GRPC_PEER_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (t->is_client) {
      s->deadline =
          gpr_time_min(s->deadline, s->send_initial_metadata->deadline);
    }
    if (metadata_size > metadata_peer_limit) {
      grpc_chttp2_cancel_stream(
          exec_ctx, t, s,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(
                      GRPC_ERROR_CREATE("to-be-sent initial metadata size "
                                        "exceeds peer limit"),
                      GRPC_ERROR_INT_SIZE, (intptr_t)metadata_size),
                  GRPC_ERROR_INT_LIMIT, (intptr_t)metadata_peer_limit),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
    } else {
      if (contains_non_ok_status(op->send_initial_metadata)) {
        s->seen_error = true;
      }
      if (!s->write_closed) {
        if (t->is_client) {
          if (!t->closed) {
            GPR_ASSERT(s->id == 0);
            grpc_chttp2_list_add_waiting_for_concurrency(t, s);
            maybe_start_some_streams(exec_ctx, t);
          } else {
            grpc_chttp2_cancel_stream(exec_ctx, t, s,
                                      GRPC_ERROR_CREATE("Transport closed"));
          }
        } else {
          GPR_ASSERT(s->id != 0);
          grpc_chttp2_become_writable(exec_ctx, t, s, true,
                                      "op.send_initial_metadata");
        }
      } else {
        s->send_initial_metadata = NULL;
        grpc_chttp2_complete_closure_step(
            exec_ctx, t, s, &s->send_initial_metadata_finished,
            GRPC_ERROR_CREATE(
                "Attempt to send initial metadata after stream was closed"),
            "send_initial_metadata_finished");
      }
    }
  }

  if (op->send_message != NULL) {
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->fetching_send_message_finished = add_closure_barrier(op->on_complete);
    if (s->write_closed) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, t, s, &s->fetching_send_message_finished,
          GRPC_ERROR_CREATE("Attempt to send message after stream was closed"),
          "fetching_send_message_finished");
    } else {
      GPR_ASSERT(s->fetching_send_message == NULL);
      uint8_t *frame_hdr =
          grpc_slice_buffer_tiny_add(&s->flow_controlled_buffer, 5);
      uint32_t flags = op->send_message->flags;
      frame_hdr[0] = (flags & GRPC_WRITE_INTERNAL_COMPRESS) != 0;
      size_t len = op->send_message->length;
      frame_hdr[1] = (uint8_t)(len >> 24);
      frame_hdr[2] = (uint8_t)(len >> 16);
      frame_hdr[3] = (uint8_t)(len >> 8);
      frame_hdr[4] = (uint8_t)(len);
      s->fetching_send_message = op->send_message;
      s->fetched_send_message_length = 0;
      s->next_message_end_offset = s->flow_controlled_bytes_written +
                                   (int64_t)s->flow_controlled_buffer.length +
                                   (int64_t)len;
      s->complete_fetch_covered_by_poller = op->covered_by_poller;
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

  if (op->send_trailing_metadata != NULL) {
    GPR_ASSERT(s->send_trailing_metadata_finished == NULL);
    on_complete->next_data.scratch |= CLOSURE_BARRIER_MAY_COVER_WRITE;
    s->send_trailing_metadata_finished = add_closure_barrier(on_complete);
    s->send_trailing_metadata = op->send_trailing_metadata;
    s->write_buffering = false;
    const size_t metadata_size =
        grpc_metadata_batch_size(op->send_trailing_metadata);
    const size_t metadata_peer_limit =
        t->settings[GRPC_PEER_SETTINGS]
                   [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (metadata_size > metadata_peer_limit) {
      grpc_chttp2_cancel_stream(
          exec_ctx, t, s,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(
                      GRPC_ERROR_CREATE("to-be-sent trailing metadata size "
                                        "exceeds peer limit"),
                      GRPC_ERROR_INT_SIZE, (intptr_t)metadata_size),
                  GRPC_ERROR_INT_LIMIT, (intptr_t)metadata_peer_limit),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
    } else {
      if (contains_non_ok_status(op->send_trailing_metadata)) {
        s->seen_error = true;
      }
      if (s->write_closed) {
        s->send_trailing_metadata = NULL;
        grpc_chttp2_complete_closure_step(
            exec_ctx, t, s, &s->send_trailing_metadata_finished,
            grpc_metadata_batch_is_empty(op->send_trailing_metadata)
                ? GRPC_ERROR_NONE
                : GRPC_ERROR_CREATE("Attempt to send trailing metadata after "
                                    "stream was closed"),
            "send_trailing_metadata_finished");
      } else if (s->id != 0) {
        /* TODO(ctiller): check if there's flow control for any outstanding
           bytes before going writable */
        grpc_chttp2_become_writable(exec_ctx, t, s, true,
                                    "op.send_trailing_metadata");
      }
    }
  }

  if (op->recv_initial_metadata != NULL) {
    GPR_ASSERT(s->recv_initial_metadata_ready == NULL);
    s->recv_initial_metadata_ready = op->recv_initial_metadata_ready;
    s->recv_initial_metadata = op->recv_initial_metadata;
    grpc_chttp2_maybe_complete_recv_initial_metadata(exec_ctx, t, s);
  }

  if (op->recv_message != NULL) {
    GPR_ASSERT(s->recv_message_ready == NULL);
    s->recv_message_ready = op->recv_message_ready;
    s->recv_message = op->recv_message;
    if (s->id != 0 &&
        (s->incoming_frames.head == NULL || s->incoming_frames.head->is_tail)) {
      incoming_byte_stream_update_flow_control(exec_ctx, t, s, 5, 0);
    }
    grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  }

  if (op->recv_trailing_metadata != NULL) {
    GPR_ASSERT(s->recv_trailing_metadata_finished == NULL);
    s->recv_trailing_metadata_finished = add_closure_barrier(on_complete);
    s->recv_trailing_metadata = op->recv_trailing_metadata;
    s->final_metadata_requested = true;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }

  grpc_chttp2_complete_closure_step(exec_ctx, t, s, &on_complete,
                                    GRPC_ERROR_NONE, "op->on_complete");

  GPR_TIMER_END("perform_stream_op_locked", 0);
  GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "perform_stream_op");
}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs, grpc_transport_stream_op *op) {
  GPR_TIMER_BEGIN("perform_stream_op", 0);
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  if (grpc_http_trace) {
    char *str = grpc_transport_stream_op_string(op);
    gpr_log(GPR_DEBUG, "perform_stream_op[s=%p/%d]: %s", s, s->id, str);
    gpr_free(str);
  }

  op->transport_private.args[0] = gt;
  op->transport_private.args[1] = gs;
  GRPC_CHTTP2_STREAM_REF(s, "perform_stream_op");
  grpc_closure_sched(
      exec_ctx,
      grpc_closure_init(
          &op->transport_private.closure, perform_stream_op_locked, op,
          grpc_combiner_scheduler(t->combiner, op->covered_by_poller)),
      GRPC_ERROR_NONE);
  GPR_TIMER_END("perform_stream_op", 0);
}

static void cancel_pings(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                         grpc_error *error) {
  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  for (size_t i = 0; i < GRPC_CHTTP2_PING_TYPE_COUNT; i++) {
    grpc_chttp2_ping_queue *pq = &t->ping_queues[i];
    for (size_t j = 0; j < GRPC_CHTTP2_PCL_COUNT; j++) {
      grpc_closure_list_fail_all(&pq->lists[j], GRPC_ERROR_REF(error));
      grpc_closure_list_sched(exec_ctx, &pq->lists[j]);
    }
  }
  GRPC_ERROR_UNREF(error);
}

static void send_ping_locked(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                             grpc_chttp2_ping_type ping_type,
                             grpc_closure *on_initiate, grpc_closure *on_ack) {
  grpc_chttp2_ping_queue *pq = &t->ping_queues[ping_type];
  grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_INITIATE], on_initiate,
                           GRPC_ERROR_NONE);
  if (grpc_closure_list_append(&pq->lists[GRPC_CHTTP2_PCL_NEXT], on_ack,
                               GRPC_ERROR_NONE)) {
    grpc_chttp2_initiate_write(exec_ctx, t, false, "send_ping");
  }
}

void grpc_chttp2_ack_ping(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                          uint64_t id) {
  grpc_chttp2_ping_queue *pq =
      &t->ping_queues[id % GRPC_CHTTP2_PING_TYPE_COUNT];
  if (pq->inflight_id != id) {
    char *from = grpc_endpoint_get_peer(t->ep);
    gpr_log(GPR_DEBUG, "Unknown ping response from %s: %" PRIx64, from, id);
    gpr_free(from);
    return;
  }
  grpc_closure_list_sched(exec_ctx, &pq->lists[GRPC_CHTTP2_PCL_INFLIGHT]);
  if (!grpc_closure_list_empty(pq->lists[GRPC_CHTTP2_PCL_NEXT])) {
    grpc_chttp2_initiate_write(exec_ctx, t, false, "continue_pings");
  }
}

static void send_goaway(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                        grpc_chttp2_error_code error, grpc_slice data) {
  t->sent_goaway_state = GRPC_CHTTP2_GOAWAY_SEND_SCHEDULED;
  grpc_chttp2_goaway_append(t->last_new_stream_id, (uint32_t)error, data,
                            &t->qbuf);
  grpc_chttp2_initiate_write(exec_ctx, t, false, "goaway_sent");
}

static void perform_transport_op_locked(grpc_exec_ctx *exec_ctx,
                                        void *stream_op,
                                        grpc_error *error_ignored) {
  grpc_transport_op *op = stream_op;
  grpc_chttp2_transport *t = op->transport_private.args[0];
  grpc_error *close_transport = op->disconnect_with_error;

  if (op->on_connectivity_state_change != NULL) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &t->channel_callback.state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
  }

  if (op->send_goaway) {
    send_goaway(exec_ctx, t,
                grpc_chttp2_grpc_status_to_http2_error(op->goaway_status),
                grpc_slice_ref_internal(*op->goaway_message));
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
    send_ping_locked(exec_ctx, t, GRPC_CHTTP2_PING_ON_NEXT_WRITE, NULL,
                     op->send_ping);
  }

  if (close_transport != GRPC_ERROR_NONE) {
    close_transport_locked(exec_ctx, t, close_transport);
  }

  grpc_closure_run(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);

  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "transport_op");
}

static void perform_transport_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                 grpc_transport_op *op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  char *msg = grpc_transport_op_string(op);
  gpr_free(msg);
  op->transport_private.args[0] = gt;
  GRPC_CHTTP2_REF_TRANSPORT(t, "transport_op");
  grpc_closure_sched(
      exec_ctx, grpc_closure_init(&op->transport_private.closure,
                                  perform_transport_op_locked, op,
                                  grpc_combiner_scheduler(t->combiner, false)),
      GRPC_ERROR_NONE);
}

/*******************************************************************************
 * INPUT PROCESSING - GENERAL
 */

void grpc_chttp2_maybe_complete_recv_initial_metadata(grpc_exec_ctx *exec_ctx,
                                                      grpc_chttp2_transport *t,
                                                      grpc_chttp2_stream *s) {
  grpc_byte_stream *bs;
  if (s->recv_initial_metadata_ready != NULL &&
      s->published_metadata[0] != GRPC_METADATA_NOT_PUBLISHED) {
    if (s->seen_error) {
      while ((bs = grpc_chttp2_incoming_frame_queue_pop(&s->incoming_frames)) !=
             NULL) {
        incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
      }
    }
    grpc_chttp2_incoming_metadata_buffer_publish(&s->metadata_buffer[0],
                                                 s->recv_initial_metadata);
    null_then_run_closure(exec_ctx, &s->recv_initial_metadata_ready,
                          GRPC_ERROR_NONE);
  }
}

void grpc_chttp2_maybe_complete_recv_message(grpc_exec_ctx *exec_ctx,
                                             grpc_chttp2_transport *t,
                                             grpc_chttp2_stream *s) {
  grpc_byte_stream *bs;
  if (s->recv_message_ready != NULL) {
    while (s->final_metadata_requested && s->seen_error &&
           (bs = grpc_chttp2_incoming_frame_queue_pop(&s->incoming_frames)) !=
               NULL) {
      incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
    }
    if (s->incoming_frames.head != NULL) {
      *s->recv_message =
          grpc_chttp2_incoming_frame_queue_pop(&s->incoming_frames);
      GPR_ASSERT(*s->recv_message != NULL);
      null_then_run_closure(exec_ctx, &s->recv_message_ready, GRPC_ERROR_NONE);
    } else if (s->published_metadata[1] != GRPC_METADATA_NOT_PUBLISHED) {
      *s->recv_message = NULL;
      null_then_run_closure(exec_ctx, &s->recv_message_ready, GRPC_ERROR_NONE);
    }
  }
}

void grpc_chttp2_maybe_complete_recv_trailing_metadata(grpc_exec_ctx *exec_ctx,
                                                       grpc_chttp2_transport *t,
                                                       grpc_chttp2_stream *s) {
  grpc_byte_stream *bs;
  grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  if (s->recv_trailing_metadata_finished != NULL && s->read_closed &&
      s->write_closed) {
    if (s->seen_error) {
      while ((bs = grpc_chttp2_incoming_frame_queue_pop(&s->incoming_frames)) !=
             NULL) {
        incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
      }
    }
    if (s->all_incoming_byte_streams_finished &&
        s->recv_trailing_metadata_finished != NULL) {
      grpc_chttp2_incoming_metadata_buffer_publish(&s->metadata_buffer[1],
                                                   s->recv_trailing_metadata);
      grpc_chttp2_complete_closure_step(
          exec_ctx, t, s, &s->recv_trailing_metadata_finished, GRPC_ERROR_NONE,
          "recv_trailing_metadata_finished");
    }
  }
}

static void decrement_active_streams_locked(grpc_exec_ctx *exec_ctx,
                                            grpc_chttp2_transport *t,
                                            grpc_chttp2_stream *s) {
  if ((s->all_incoming_byte_streams_finished = gpr_unref(&s->active_streams))) {
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }
}

static void remove_stream(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                          uint32_t id, grpc_error *error) {
  grpc_chttp2_stream *s = grpc_chttp2_stream_map_delete(&t->stream_map, id);
  GPR_ASSERT(s);
  if (t->incoming_stream == s) {
    t->incoming_stream = NULL;
    grpc_chttp2_parsing_become_skip_parser(exec_ctx, t);
  }
  if (s->data_parser.parsing_frame != NULL) {
    grpc_chttp2_incoming_byte_stream_finished(
        exec_ctx, s->data_parser.parsing_frame, GRPC_ERROR_REF(error));
    s->data_parser.parsing_frame = NULL;
  }

  if (grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    post_benign_reclaimer(exec_ctx, t);
    if (t->sent_goaway_state == GRPC_CHTTP2_GOAWAY_SENT) {
      close_transport_locked(
          exec_ctx, t,
          GRPC_ERROR_CREATE_REFERENCING(
              "Last stream closed after sending GOAWAY", &error, 1));
    }
  }
  if (grpc_chttp2_list_remove_writable_stream(t, s)) {
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2_writing:remove_stream");
  }

  GRPC_ERROR_UNREF(error);

  maybe_start_some_streams(exec_ctx, t);
}

static void status_codes_from_error(grpc_error *error, gpr_timespec deadline,
                                    grpc_chttp2_error_code *http2_error,
                                    grpc_status_code *grpc_status) {
  intptr_t ip_http;
  intptr_t ip_grpc;
  bool have_http =
      grpc_error_get_int(error, GRPC_ERROR_INT_HTTP2_ERROR, &ip_http);
  bool have_grpc =
      grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &ip_grpc);
  if (have_http) {
    *http2_error = (grpc_chttp2_error_code)ip_http;
  } else if (have_grpc) {
    *http2_error =
        grpc_chttp2_grpc_status_to_http2_error((grpc_status_code)ip_grpc);
  } else {
    *http2_error = GRPC_CHTTP2_INTERNAL_ERROR;
  }
  if (have_grpc) {
    *grpc_status = (grpc_status_code)ip_grpc;
  } else if (have_http) {
    *grpc_status = grpc_chttp2_http2_error_to_grpc_status(
        (grpc_chttp2_error_code)ip_http, deadline);
  } else {
    *grpc_status = GRPC_STATUS_INTERNAL;
  }
}

void grpc_chttp2_cancel_stream(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                               grpc_error *due_to_error) {
  if (!s->read_closed || !s->write_closed) {
    grpc_status_code grpc_status;
    grpc_chttp2_error_code http_error;
    status_codes_from_error(due_to_error, s->deadline, &http_error,
                            &grpc_status);

    if (s->id != 0) {
      grpc_slice_buffer_add(
          &t->qbuf, grpc_chttp2_rst_stream_create(s->id, (uint32_t)http_error,
                                                  &s->stats.outgoing));
      grpc_chttp2_initiate_write(exec_ctx, t, false, "rst_stream");
    }

    const char *msg =
        grpc_error_get_str(due_to_error, GRPC_ERROR_STR_GRPC_MESSAGE);
    bool free_msg = false;
    if (msg == NULL) {
      free_msg = true;
      msg = grpc_error_string(due_to_error);
    }
    grpc_slice msg_slice = grpc_slice_from_copied_string(msg);
    grpc_chttp2_fake_status(exec_ctx, t, s, grpc_status, &msg_slice);
    if (free_msg) grpc_error_free_string(msg);
  }
  if (due_to_error != GRPC_ERROR_NONE && !s->seen_error) {
    s->seen_error = true;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }
  grpc_chttp2_mark_stream_closed(exec_ctx, t, s, 1, 1, due_to_error);
}

void grpc_chttp2_fake_status(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                             grpc_chttp2_stream *s, grpc_status_code status,
                             grpc_slice *slice) {
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
      s->recv_trailing_metadata_finished != NULL) {
    char status_string[GPR_LTOA_MIN_BUFSIZE];
    gpr_ltoa(status, status_string);
    grpc_chttp2_incoming_metadata_buffer_add(
        &s->metadata_buffer[1], grpc_mdelem_from_metadata_strings(
                                    exec_ctx, GRPC_MDSTR_GRPC_STATUS,
                                    grpc_mdstr_from_string(status_string)));
    if (slice) {
      grpc_chttp2_incoming_metadata_buffer_add(
          &s->metadata_buffer[1],
          grpc_mdelem_from_metadata_strings(
              exec_ctx, GRPC_MDSTR_GRPC_MESSAGE,
              grpc_mdstr_from_slice(exec_ctx,
                                    grpc_slice_ref_internal(*slice))));
    }
    s->published_metadata[1] = GRPC_METADATA_SYNTHESIZED_FROM_FAKE;
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }
  if (slice) {
    grpc_slice_unref_internal(exec_ctx, *slice);
  }
}

static void add_error(grpc_error *error, grpc_error **refs, size_t *nrefs) {
  if (error == GRPC_ERROR_NONE) return;
  for (size_t i = 0; i < *nrefs; i++) {
    if (error == refs[i]) {
      return;
    }
  }
  refs[*nrefs] = error;
  ++*nrefs;
}

static grpc_error *removal_error(grpc_error *extra_error, grpc_chttp2_stream *s,
                                 const char *master_error_msg) {
  grpc_error *refs[3];
  size_t nrefs = 0;
  add_error(s->read_closed_error, refs, &nrefs);
  add_error(s->write_closed_error, refs, &nrefs);
  add_error(extra_error, refs, &nrefs);
  grpc_error *error = GRPC_ERROR_NONE;
  if (nrefs > 0) {
    error = GRPC_ERROR_CREATE_REFERENCING(master_error_msg, refs, nrefs);
  }
  GRPC_ERROR_UNREF(extra_error);
  return error;
}

void grpc_chttp2_fail_pending_writes(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_chttp2_stream *s, grpc_error *error) {
  error =
      removal_error(error, s, "Pending writes failed due to stream closure");
  s->send_initial_metadata = NULL;
  grpc_chttp2_complete_closure_step(
      exec_ctx, t, s, &s->send_initial_metadata_finished, GRPC_ERROR_REF(error),
      "send_initial_metadata_finished");

  s->send_trailing_metadata = NULL;
  grpc_chttp2_complete_closure_step(
      exec_ctx, t, s, &s->send_trailing_metadata_finished,
      GRPC_ERROR_REF(error), "send_trailing_metadata_finished");

  s->fetching_send_message = NULL;
  grpc_chttp2_complete_closure_step(
      exec_ctx, t, s, &s->fetching_send_message_finished, GRPC_ERROR_REF(error),
      "fetching_send_message_finished");
  while (s->on_write_finished_cbs) {
    grpc_chttp2_write_cb *cb = s->on_write_finished_cbs;
    s->on_write_finished_cbs = cb->next;
    grpc_chttp2_complete_closure_step(exec_ctx, t, s, &cb->closure,
                                      GRPC_ERROR_REF(error),
                                      "on_write_finished_cb");
    cb->next = t->write_cb_pool;
    t->write_cb_pool = cb;
  }
  GRPC_ERROR_UNREF(error);
}

void grpc_chttp2_mark_stream_closed(grpc_exec_ctx *exec_ctx,
                                    grpc_chttp2_transport *t,
                                    grpc_chttp2_stream *s, int close_reads,
                                    int close_writes, grpc_error *error) {
  if (s->read_closed && s->write_closed) {
    /* already closed */
    GRPC_ERROR_UNREF(error);
    return;
  }
  if (close_reads && !s->read_closed) {
    s->read_closed_error = GRPC_ERROR_REF(error);
    s->read_closed = true;
    for (int i = 0; i < 2; i++) {
      if (s->published_metadata[i] == GRPC_METADATA_NOT_PUBLISHED) {
        s->published_metadata[i] = GPRC_METADATA_PUBLISHED_AT_CLOSE;
      }
    }
    decrement_active_streams_locked(exec_ctx, t, s);
    grpc_chttp2_maybe_complete_recv_initial_metadata(exec_ctx, t, s);
    grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }
  if (close_writes && !s->write_closed) {
    s->write_closed_error = GRPC_ERROR_REF(error);
    s->write_closed = true;
    grpc_chttp2_fail_pending_writes(exec_ctx, t, s, GRPC_ERROR_REF(error));
    grpc_chttp2_maybe_complete_recv_trailing_metadata(exec_ctx, t, s);
  }
  if (s->read_closed && s->write_closed) {
    if (s->id != 0) {
      remove_stream(exec_ctx, t, s->id,
                    removal_error(GRPC_ERROR_REF(error), s, "Stream removed"));
    } else {
      /* Purge streams waiting on concurrency still waiting for id assignment */
      grpc_chttp2_list_remove_waiting_for_concurrency(t, s);
    }
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, s, "chttp2");
  }
  GRPC_ERROR_UNREF(error);
}

static void close_from_api(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                           grpc_chttp2_stream *s, grpc_error *error) {
  grpc_slice hdr;
  grpc_slice status_hdr;
  grpc_slice message_pfx;
  uint8_t *p;
  uint32_t len = 0;
  grpc_status_code grpc_status;
  grpc_chttp2_error_code http_error;
  status_codes_from_error(error, s->deadline, &http_error, &grpc_status);

  GPR_ASSERT(grpc_status >= 0 && (int)grpc_status < 100);

  if (s->id != 0 && !t->is_client) {
    /* Hand roll a header block.
       This is unnecessarily ugly - at some point we should find a more
       elegant
       solution.
       It's complicated by the fact that our send machinery would be dead by
       the
       time we got around to sending this, so instead we ignore HPACK
       compression
       and just write the uncompressed bytes onto the wire. */
    status_hdr = grpc_slice_malloc(15 + (grpc_status >= 10));
    p = GRPC_SLICE_START_PTR(status_hdr);
    *p++ = 0x40; /* literal header */
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

    const char *optional_message =
        grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE);

    if (optional_message != NULL) {
      size_t msg_len = strlen(optional_message);
      GPR_ASSERT(msg_len <= UINT32_MAX);
      uint32_t msg_len_len = GRPC_CHTTP2_VARINT_LENGTH((uint32_t)msg_len, 0);
      message_pfx = grpc_slice_malloc(14 + msg_len_len);
      p = GRPC_SLICE_START_PTR(message_pfx);
      *p++ = 0x40;
      *p++ = 12; /* len(grpc-message) */
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
      GRPC_CHTTP2_WRITE_VARINT((uint32_t)msg_len, 0, 0, p,
                               (uint32_t)msg_len_len);
      p += msg_len_len;
      GPR_ASSERT(p == GRPC_SLICE_END_PTR(message_pfx));
      len += (uint32_t)GRPC_SLICE_LENGTH(message_pfx);
      len += (uint32_t)msg_len;
    }

    hdr = grpc_slice_malloc(9);
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
    grpc_slice_buffer_add(&t->qbuf, status_hdr);
    if (optional_message) {
      grpc_slice_buffer_add(&t->qbuf, message_pfx);
      grpc_slice_buffer_add(&t->qbuf,
                            grpc_slice_from_copied_string(optional_message));
    }
    grpc_slice_buffer_add(
        &t->qbuf, grpc_chttp2_rst_stream_create(s->id, GRPC_CHTTP2_NO_ERROR,
                                                &s->stats.outgoing));
  }

  const char *msg = grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE);
  bool free_msg = false;
  if (msg == NULL) {
    free_msg = true;
    msg = grpc_error_string(error);
  }
  grpc_slice msg_slice = grpc_slice_from_copied_string(msg);
  grpc_chttp2_fake_status(exec_ctx, t, s, grpc_status, &msg_slice);
  if (free_msg) grpc_error_free_string(msg);

  grpc_chttp2_mark_stream_closed(exec_ctx, t, s, 1, 1, error);
  grpc_chttp2_initiate_write(exec_ctx, t, false, "close_from_api");
}

typedef struct {
  grpc_exec_ctx *exec_ctx;
  grpc_error *error;
  grpc_chttp2_transport *t;
} cancel_stream_cb_args;

static void cancel_stream_cb(void *user_data, uint32_t key, void *stream) {
  cancel_stream_cb_args *args = user_data;
  grpc_chttp2_stream *s = stream;
  grpc_chttp2_cancel_stream(args->exec_ctx, args->t, s,
                            GRPC_ERROR_REF(args->error));
}

static void end_all_the_calls(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                              grpc_error *error) {
  cancel_stream_cb_args args = {exec_ctx, error, t};
  grpc_chttp2_stream_map_for_each(&t->stream_map, cancel_stream_cb, &args);
  GRPC_ERROR_UNREF(error);
}

/*******************************************************************************
 * INPUT PROCESSING - PARSING
 */

static void update_bdp(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                       double bdp_dbl) {
  uint32_t bdp;
  if (bdp_dbl <= 0) {
    bdp = 0;
  } else if (bdp_dbl > UINT32_MAX) {
    bdp = UINT32_MAX;
  } else {
    bdp = (uint32_t)(bdp_dbl);
  }
  int64_t delta =
      (int64_t)bdp -
      (int64_t)t->settings[GRPC_LOCAL_SETTINGS]
                          [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
  if (delta == 0 || (bdp != 0 && delta > -1024 && delta < 1024)) {
    return;
  }
  push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, bdp);
}

/*******************************************************************************
 * INPUT PROCESSING - PARSING
 */

static grpc_error *try_http_parsing(grpc_exec_ctx *exec_ctx,
                                    grpc_chttp2_transport *t) {
  grpc_http_parser parser;
  size_t i = 0;
  grpc_error *error = GRPC_ERROR_NONE;
  grpc_http_response response;
  memset(&response, 0, sizeof(response));

  grpc_http_parser_init(&parser, GRPC_HTTP_RESPONSE, &response);

  grpc_error *parse_error = GRPC_ERROR_NONE;
  for (; i < t->read_buffer.count && parse_error == GRPC_ERROR_NONE; i++) {
    parse_error =
        grpc_http_parser_parse(&parser, t->read_buffer.slices[i], NULL);
  }
  if (parse_error == GRPC_ERROR_NONE &&
      (parse_error = grpc_http_parser_eof(&parser)) == GRPC_ERROR_NONE) {
    error = grpc_error_set_int(
        GRPC_ERROR_CREATE("Trying to connect an http1.x server"),
        GRPC_ERROR_INT_HTTP_STATUS, response.status);
  }
  GRPC_ERROR_UNREF(parse_error);

  grpc_http_parser_destroy(&parser);
  grpc_http_response_destroy(&response);
  return error;
}

static void read_action_locked(grpc_exec_ctx *exec_ctx, void *tp,
                               grpc_error *error) {
  GPR_TIMER_BEGIN("reading_action_locked", 0);

  grpc_chttp2_transport *t = tp;
  bool need_bdp_ping = false;

  GRPC_ERROR_REF(error);

  grpc_error *err = error;
  if (err != GRPC_ERROR_NONE) {
    err = grpc_error_set_int(
        GRPC_ERROR_CREATE_REFERENCING("Endpoint read failed", &err, 1),
        GRPC_ERROR_INT_OCCURRED_DURING_WRITE, t->write_state);
  }
  GPR_SWAP(grpc_error *, err, error);
  GRPC_ERROR_UNREF(err);
  if (!t->closed) {
    GPR_TIMER_BEGIN("reading_action.parse", 0);
    size_t i = 0;
    grpc_error *errors[3] = {GRPC_ERROR_REF(error), GRPC_ERROR_NONE,
                             GRPC_ERROR_NONE};
    for (; i < t->read_buffer.count && errors[1] == GRPC_ERROR_NONE; i++) {
      if (grpc_bdp_estimator_add_incoming_bytes(
              &t->bdp_estimator,
              (int64_t)GRPC_SLICE_LENGTH(t->read_buffer.slices[i]))) {
        need_bdp_ping = true;
      }
      errors[1] =
          grpc_chttp2_perform_read(exec_ctx, t, t->read_buffer.slices[i]);
    }
    if (!t->parse_saw_data_frames) {
      need_bdp_ping = false;
    }
    t->parse_saw_data_frames = false;
    if (errors[1] != GRPC_ERROR_NONE) {
      errors[2] = try_http_parsing(exec_ctx, t);
      GRPC_ERROR_UNREF(error);
      error = GRPC_ERROR_CREATE_REFERENCING("Failed parsing HTTP/2", errors,
                                            GPR_ARRAY_SIZE(errors));
    }
    for (i = 0; i < GPR_ARRAY_SIZE(errors); i++) {
      GRPC_ERROR_UNREF(errors[i]);
    }
    GPR_TIMER_END("reading_action.parse", 0);

    GPR_TIMER_BEGIN("post_parse_locked", 0);
    if (t->initial_window_update != 0) {
      if (t->initial_window_update > 0) {
        grpc_chttp2_stream *s;
        while (grpc_chttp2_list_pop_stalled_by_stream(t, &s)) {
          grpc_chttp2_become_writable(exec_ctx, t, s, false, "unstalled");
        }
      }
      t->initial_window_update = 0;
    }
    GPR_TIMER_END("post_parse_locked", 0);
  }

  GPR_TIMER_BEGIN("post_reading_action_locked", 0);
  bool keep_reading = false;
  if (error == GRPC_ERROR_NONE && t->closed) {
    error = GRPC_ERROR_CREATE("Transport closed");
  }
  if (error != GRPC_ERROR_NONE) {
    close_transport_locked(exec_ctx, t, GRPC_ERROR_REF(error));
    t->endpoint_reading = 0;
  } else if (!t->closed) {
    keep_reading = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "keep_reading");
  }
  grpc_slice_buffer_reset_and_unref_internal(exec_ctx, &t->read_buffer);

  if (keep_reading) {
    grpc_endpoint_read(exec_ctx, t->ep, &t->read_buffer,
                       &t->read_action_locked);

    if (need_bdp_ping &&
        gpr_time_cmp(gpr_time_add(t->last_bdp_ping_finished,
                                  gpr_time_from_millis(100, GPR_TIMESPAN)),
                     gpr_now(GPR_CLOCK_MONOTONIC)) < 0) {
      GRPC_CHTTP2_REF_TRANSPORT(t, "bdp_ping");
      grpc_bdp_estimator_schedule_ping(&t->bdp_estimator);
      send_ping_locked(exec_ctx, t,
                       GRPC_CHTTP2_PING_BEFORE_TRANSPORT_WINDOW_UPDATE,
                       &t->start_bdp_ping_locked, &t->finish_bdp_ping_locked);
    }

    int64_t estimate = -1;
    if (grpc_bdp_estimator_get_estimate(&t->bdp_estimator, &estimate)) {
      double target = log2((double)estimate);
      double memory_pressure = grpc_resource_quota_get_memory_pressure(
          grpc_resource_user_get_quota(grpc_endpoint_get_resource_user(t->ep)));
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
      gpr_log(GPR_DEBUG, "%s: err=%lf cur=%lf pressure=%lf target=%lf",
              t->peer_string, bdp_error, log2_bdp_guess, memory_pressure,
              target);
      update_bdp(exec_ctx, t, pow(2, log2_bdp_guess));
      t->last_pid_update = now;
    }
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "keep_reading");
  } else {
    GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "reading_action");
  }

  GPR_TIMER_END("post_reading_action_locked", 0);

  GRPC_ERROR_UNREF(error);

  GPR_TIMER_END("reading_action_locked", 0);
}

static void start_bdp_ping_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                  grpc_error *error) {
  grpc_chttp2_transport *t = tp;
  gpr_log(GPR_DEBUG, "%s: Start BDP ping", t->peer_string);
  grpc_bdp_estimator_start_ping(&t->bdp_estimator);
}

static void finish_bdp_ping_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                   grpc_error *error) {
  grpc_chttp2_transport *t = tp;
  gpr_log(GPR_DEBUG, "%s: Complete BDP ping", t->peer_string);
  grpc_bdp_estimator_complete_ping(&t->bdp_estimator);
  t->last_bdp_ping_finished = gpr_now(GPR_CLOCK_MONOTONIC);

  GRPC_CHTTP2_UNREF_TRANSPORT(exec_ctx, t, "bdp_ping");
}

/*******************************************************************************
 * CALLBACK LOOP
 */

static void connectivity_state_set(grpc_exec_ctx *exec_ctx,
                                   grpc_chttp2_transport *t,
                                   grpc_connectivity_state state,
                                   grpc_error *error, const char *reason) {
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_DEBUG, "set connectivity_state=%d", state));
  grpc_connectivity_state_set(exec_ctx, &t->channel_callback.state_tracker,
                              state, error, reason);
}

/*******************************************************************************
 * POLLSET STUFF
 */

static void set_pollset(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                        grpc_stream *gs, grpc_pollset *pollset) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_endpoint_add_to_pollset(exec_ctx, t->ep, pollset);
}

static void set_pollset_set(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                            grpc_stream *gs, grpc_pollset_set *pollset_set) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_endpoint_add_to_pollset_set(exec_ctx, t->ep, pollset_set);
}

/*******************************************************************************
 * BYTE STREAM
 */

static void incoming_byte_stream_unref(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_incoming_byte_stream *bs) {
  if (gpr_unref(&bs->refs)) {
    GRPC_ERROR_UNREF(bs->error);
    grpc_slice_buffer_destroy_internal(exec_ctx, &bs->slices);
    gpr_mu_destroy(&bs->slice_mu);
    gpr_free(bs);
  }
}

static void incoming_byte_stream_update_flow_control(grpc_exec_ctx *exec_ctx,
                                                     grpc_chttp2_transport *t,
                                                     grpc_chttp2_stream *s,
                                                     size_t max_size_hint,
                                                     size_t have_already) {
  uint32_t max_recv_bytes;
  uint32_t initial_window_size =
      t->settings[GRPC_SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];

  /* clamp max recv hint to an allowable size */
  if (max_size_hint >= UINT32_MAX - initial_window_size) {
    max_recv_bytes = UINT32_MAX - initial_window_size;
  } else {
    max_recv_bytes = (uint32_t)max_size_hint;
  }

  /* account for bytes already received but unknown to higher layers */
  if (max_recv_bytes >= have_already) {
    max_recv_bytes -= (uint32_t)have_already;
  } else {
    max_recv_bytes = 0;
  }

  /* add some small lookahead to keep pipelines flowing */
  GPR_ASSERT(max_recv_bytes <= UINT32_MAX - initial_window_size);
  if (s->incoming_window_delta < max_recv_bytes) {
    uint32_t add_max_recv_bytes =
        (uint32_t)(max_recv_bytes - s->incoming_window_delta);
    bool new_window_write_is_covered_by_poller =
        s->incoming_window_delta + initial_window_size < (int64_t)have_already;
    GRPC_CHTTP2_FLOW_CREDIT_STREAM("op", t, s, incoming_window_delta,
                                   add_max_recv_bytes);
    GRPC_CHTTP2_FLOW_CREDIT_STREAM("op", t, s, announce_window,
                                   add_max_recv_bytes);
    grpc_chttp2_become_writable(exec_ctx, t, s,
                                new_window_write_is_covered_by_poller,
                                "read_incoming_stream");
  }
}

static void incoming_byte_stream_next_locked(grpc_exec_ctx *exec_ctx,
                                             void *argp,
                                             grpc_error *error_ignored) {
  grpc_chttp2_incoming_byte_stream *bs = argp;
  grpc_chttp2_transport *t = bs->transport;
  grpc_chttp2_stream *s = bs->stream;

  if (bs->is_tail) {
    gpr_mu_lock(&bs->slice_mu);
    size_t cur_length = bs->slices.length;
    gpr_mu_unlock(&bs->slice_mu);
    incoming_byte_stream_update_flow_control(
        exec_ctx, t, s, bs->next_action.max_size_hint, cur_length);
  }
  gpr_mu_lock(&bs->slice_mu);
  if (bs->slices.count > 0) {
    *bs->next_action.slice = grpc_slice_buffer_take_first(&bs->slices);
    grpc_closure_run(exec_ctx, bs->next_action.on_complete, GRPC_ERROR_NONE);
  } else if (bs->error != GRPC_ERROR_NONE) {
    grpc_closure_run(exec_ctx, bs->next_action.on_complete,
                     GRPC_ERROR_REF(bs->error));
  } else {
    bs->on_next = bs->next_action.on_complete;
    bs->next = bs->next_action.slice;
  }
  gpr_mu_unlock(&bs->slice_mu);
  incoming_byte_stream_unref(exec_ctx, bs);
}

static int incoming_byte_stream_next(grpc_exec_ctx *exec_ctx,
                                     grpc_byte_stream *byte_stream,
                                     grpc_slice *slice, size_t max_size_hint,
                                     grpc_closure *on_complete) {
  GPR_TIMER_BEGIN("incoming_byte_stream_next", 0);
  grpc_chttp2_incoming_byte_stream *bs =
      (grpc_chttp2_incoming_byte_stream *)byte_stream;
  gpr_ref(&bs->refs);
  bs->next_action.slice = slice;
  bs->next_action.max_size_hint = max_size_hint;
  bs->next_action.on_complete = on_complete;
  grpc_closure_sched(
      exec_ctx,
      grpc_closure_init(
          &bs->next_action.closure, incoming_byte_stream_next_locked, bs,
          grpc_combiner_scheduler(bs->transport->combiner, false)),
      GRPC_ERROR_NONE);
  GPR_TIMER_END("incoming_byte_stream_next", 0);
  return 0;
}

static void incoming_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                                         grpc_byte_stream *byte_stream);

static void incoming_byte_stream_destroy_locked(grpc_exec_ctx *exec_ctx,
                                                void *byte_stream,
                                                grpc_error *error_ignored) {
  grpc_chttp2_incoming_byte_stream *bs = byte_stream;
  GPR_ASSERT(bs->base.destroy == incoming_byte_stream_destroy);
  decrement_active_streams_locked(exec_ctx, bs->transport, bs->stream);
  incoming_byte_stream_unref(exec_ctx, bs);
}

static void incoming_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                                         grpc_byte_stream *byte_stream) {
  GPR_TIMER_BEGIN("incoming_byte_stream_destroy", 0);
  grpc_chttp2_incoming_byte_stream *bs =
      (grpc_chttp2_incoming_byte_stream *)byte_stream;
  grpc_closure_sched(
      exec_ctx,
      grpc_closure_init(
          &bs->destroy_action, incoming_byte_stream_destroy_locked, bs,
          grpc_combiner_scheduler(bs->transport->combiner, false)),
      GRPC_ERROR_NONE);
  GPR_TIMER_END("incoming_byte_stream_destroy", 0);
}

static void incoming_byte_stream_publish_error(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_byte_stream *bs,
    grpc_error *error) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  grpc_closure_sched(exec_ctx, bs->on_next, GRPC_ERROR_REF(error));
  bs->on_next = NULL;
  GRPC_ERROR_UNREF(bs->error);
  bs->error = error;
}

void grpc_chttp2_incoming_byte_stream_push(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_incoming_byte_stream *bs,
                                           grpc_slice slice) {
  gpr_mu_lock(&bs->slice_mu);
  if (bs->remaining_bytes < GRPC_SLICE_LENGTH(slice)) {
    incoming_byte_stream_publish_error(
        exec_ctx, bs, GRPC_ERROR_CREATE("Too many bytes in stream"));
  } else {
    bs->remaining_bytes -= (uint32_t)GRPC_SLICE_LENGTH(slice);
    if (bs->on_next != NULL) {
      *bs->next = slice;
      grpc_closure_sched(exec_ctx, bs->on_next, GRPC_ERROR_NONE);
      bs->on_next = NULL;
    } else {
      grpc_slice_buffer_add(&bs->slices, slice);
    }
  }
  gpr_mu_unlock(&bs->slice_mu);
}

void grpc_chttp2_incoming_byte_stream_finished(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_byte_stream *bs,
    grpc_error *error) {
  if (error == GRPC_ERROR_NONE) {
    gpr_mu_lock(&bs->slice_mu);
    if (bs->remaining_bytes != 0) {
      error = GRPC_ERROR_CREATE("Truncated message");
    }
    gpr_mu_unlock(&bs->slice_mu);
  }
  if (error != GRPC_ERROR_NONE) {
    incoming_byte_stream_publish_error(exec_ctx, bs, error);
  }
  incoming_byte_stream_unref(exec_ctx, bs);
}

grpc_chttp2_incoming_byte_stream *grpc_chttp2_incoming_byte_stream_create(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t, grpc_chttp2_stream *s,
    uint32_t frame_size, uint32_t flags) {
  grpc_chttp2_incoming_byte_stream *incoming_byte_stream =
      gpr_malloc(sizeof(*incoming_byte_stream));
  incoming_byte_stream->base.length = frame_size;
  incoming_byte_stream->remaining_bytes = frame_size;
  incoming_byte_stream->base.flags = flags;
  incoming_byte_stream->base.next = incoming_byte_stream_next;
  incoming_byte_stream->base.destroy = incoming_byte_stream_destroy;
  gpr_mu_init(&incoming_byte_stream->slice_mu);
  gpr_ref_init(&incoming_byte_stream->refs, 2);
  incoming_byte_stream->next_message = NULL;
  incoming_byte_stream->transport = t;
  incoming_byte_stream->stream = s;
  gpr_ref(&incoming_byte_stream->stream->active_streams);
  grpc_slice_buffer_init(&incoming_byte_stream->slices);
  incoming_byte_stream->on_next = NULL;
  incoming_byte_stream->is_tail = 1;
  incoming_byte_stream->error = GRPC_ERROR_NONE;
  grpc_chttp2_incoming_frame_queue *q = &s->incoming_frames;
  if (q->head == NULL) {
    q->head = incoming_byte_stream;
  } else {
    q->tail->is_tail = 0;
    q->tail->next_message = incoming_byte_stream;
  }
  q->tail = incoming_byte_stream;
  grpc_chttp2_maybe_complete_recv_message(exec_ctx, t, s);
  return incoming_byte_stream;
}

/*******************************************************************************
 * RESOURCE QUOTAS
 */

static void post_benign_reclaimer(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t) {
  if (!t->benign_reclaimer_registered) {
    t->benign_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "benign_reclaimer");
    grpc_resource_user_post_reclaimer(exec_ctx,
                                      grpc_endpoint_get_resource_user(t->ep),
                                      false, &t->benign_reclaimer_locked);
  }
}

static void post_destructive_reclaimer(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_transport *t) {
  if (!t->destructive_reclaimer_registered) {
    t->destructive_reclaimer_registered = true;
    GRPC_CHTTP2_REF_TRANSPORT(t, "destructive_reclaimer");
    grpc_resource_user_post_reclaimer(exec_ctx,
                                      grpc_endpoint_get_resource_user(t->ep),
                                      true, &t->destructive_reclaimer_locked);
  }
}

static void benign_reclaimer_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_error *error) {
  grpc_chttp2_transport *t = arg;
  if (error == GRPC_ERROR_NONE &&
      grpc_chttp2_stream_map_size(&t->stream_map) == 0) {
    /* Channel with no active streams: send a goaway to try and make it
     * disconnect cleanly */
    if (grpc_resource_quota_trace) {
      gpr_log(GPR_DEBUG, "HTTP2: %s - send goaway to free memory",
              t->peer_string);
    }
    send_goaway(exec_ctx, t, GRPC_CHTTP2_ENHANCE_YOUR_CALM,
                grpc_slice_from_static_string("Buffers full"));
  } else if (error == GRPC_ERROR_NONE && grpc_resource_quota_trace) {
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

static void destructive_reclaimer_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                         grpc_error *error) {
  grpc_chttp2_transport *t = arg;
  size_t n = grpc_chttp2_stream_map_size(&t->stream_map);
  t->destructive_reclaimer_registered = false;
  if (error == GRPC_ERROR_NONE && n > 0) {
    grpc_chttp2_stream *s = grpc_chttp2_stream_map_rand(&t->stream_map);
    if (grpc_resource_quota_trace) {
      gpr_log(GPR_DEBUG, "HTTP2: %s - abandon stream id %d", t->peer_string,
              s->id);
    }
    grpc_chttp2_cancel_stream(
        exec_ctx, t, s, grpc_error_set_int(GRPC_ERROR_CREATE("Buffers full"),
                                           GRPC_ERROR_INT_HTTP2_ERROR,
                                           GRPC_CHTTP2_ENHANCE_YOUR_CALM));
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
 * TRACING
 */

static char *format_flowctl_context_var(const char *context, const char *var,
                                        int64_t val, uint32_t id) {
  char *name;
  if (context == NULL) {
    name = gpr_strdup(var);
  } else if (0 == strcmp(context, "t")) {
    GPR_ASSERT(id == 0);
    gpr_asprintf(&name, "TRANSPORT:%s", var);
  } else if (0 == strcmp(context, "s")) {
    GPR_ASSERT(id != 0);
    gpr_asprintf(&name, "STREAM[%d]:%s", id, var);
  } else {
    gpr_asprintf(&name, "BAD_CONTEXT[%s][%d]:%s", context, id, var);
  }
  char *name_fld = gpr_leftpad(name, ' ', 64);
  char *value;
  gpr_asprintf(&value, "%" PRId64, val);
  char *value_fld = gpr_leftpad(value, ' ', 8);
  char *result;
  gpr_asprintf(&result, "%s %s", name_fld, value_fld);
  gpr_free(name);
  gpr_free(name_fld);
  gpr_free(value);
  gpr_free(value_fld);
  return result;
}

void grpc_chttp2_flowctl_trace(const char *file, int line, const char *phase,
                               grpc_chttp2_flowctl_op op, const char *context1,
                               const char *var1, const char *context2,
                               const char *var2, int is_client,
                               uint32_t stream_id, int64_t val1, int64_t val2) {
  char *tmp_phase;
  char *label1 = format_flowctl_context_var(context1, var1, val1, stream_id);
  char *label2 = format_flowctl_context_var(context2, var2, val2, stream_id);
  char *clisvr = is_client ? "client" : "server";
  char *prefix;

  tmp_phase = gpr_leftpad(phase, ' ', 8);
  gpr_asprintf(&prefix, "FLOW %s: %s ", tmp_phase, clisvr);
  gpr_free(tmp_phase);

  switch (op) {
    case GRPC_CHTTP2_FLOWCTL_MOVE:
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sMOVE   %s <- %s giving %" PRId64, prefix, label1, label2,
                val1 + val2);
      }
      break;
    case GRPC_CHTTP2_FLOWCTL_CREDIT:
      GPR_ASSERT(val2 >= 0);
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sCREDIT %s by %s giving %" PRId64, prefix, label1, label2,
                val1 + val2);
      }
      break;
    case GRPC_CHTTP2_FLOWCTL_DEBIT:
      GPR_ASSERT(val2 >= 0);
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sDEBIT  %s by %s giving %" PRId64, prefix, label1, label2,
                val1 - val2);
      }
      break;
  }

  gpr_free(label1);
  gpr_free(label2);
  gpr_free(prefix);
}

/*******************************************************************************
 * INTEGRATION GLUE
 */

static char *chttp2_get_peer(grpc_exec_ctx *exec_ctx, grpc_transport *t) {
  return gpr_strdup(((grpc_chttp2_transport *)t)->peer_string);
}

/*******************************************************************************
 * MONITORING
 */
static grpc_endpoint *chttp2_get_endpoint(grpc_exec_ctx *exec_ctx,
                                          grpc_transport *t) {
  return ((grpc_chttp2_transport *)t)->ep;
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
                                             chttp2_get_peer,
                                             chttp2_get_endpoint};

grpc_transport *grpc_create_chttp2_transport(
    grpc_exec_ctx *exec_ctx, const grpc_channel_args *channel_args,
    grpc_endpoint *ep, int is_client) {
  grpc_chttp2_transport *t = gpr_malloc(sizeof(grpc_chttp2_transport));
  init_transport(exec_ctx, t, channel_args, ep, is_client != 0);
  return &t->base;
}

void grpc_chttp2_transport_start_reading(grpc_exec_ctx *exec_ctx,
                                         grpc_transport *transport,
                                         grpc_slice_buffer *read_buffer) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)transport;
  GRPC_CHTTP2_REF_TRANSPORT(
      t, "reading_action"); /* matches unref inside reading_action */
  if (read_buffer != NULL) {
    grpc_slice_buffer_move_into(read_buffer, &t->read_buffer);
    gpr_free(read_buffer);
  }
  grpc_closure_sched(exec_ctx, &t->read_action_locked, GRPC_ERROR_NONE);
}
