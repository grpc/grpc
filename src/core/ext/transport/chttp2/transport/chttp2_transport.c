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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/transport/http2_errors.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/ext/transport/chttp2/transport/status_conversion.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/workqueue.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/transport/timeout_encoding.h"
#include "src/core/lib/transport/transport_impl.h"

#define DEFAULT_WINDOW 65535
#define DEFAULT_CONNECTION_WINDOW_TARGET (1024 * 1024)
#define MAX_WINDOW 0x7fffffffu

#define DEFAULT_MAX_HEADER_LIST_SIZE (16 * 1024)

#define MAX_CLIENT_STREAM_ID 0x7fffffffu
int grpc_http_trace = 0;
int grpc_flowctl_trace = 0;
int grpc_http_write_state_trace = 0;

#define TRANSPORT_FROM_WRITING(tw)                                        \
  ((grpc_chttp2_transport *)((char *)(tw)-offsetof(grpc_chttp2_transport, \
                                                   writing)))

#define TRANSPORT_FROM_PARSING(tp)                                        \
  ((grpc_chttp2_transport *)((char *)(tp)-offsetof(grpc_chttp2_transport, \
                                                   parsing)))

#define TRANSPORT_FROM_GLOBAL(tg)                                         \
  ((grpc_chttp2_transport *)((char *)(tg)-offsetof(grpc_chttp2_transport, \
                                                   global)))

#define STREAM_FROM_GLOBAL(sg) \
  ((grpc_chttp2_stream *)((char *)(sg)-offsetof(grpc_chttp2_stream, global)))

#define STREAM_FROM_PARSING(sg) \
  ((grpc_chttp2_stream *)((char *)(sg)-offsetof(grpc_chttp2_stream, parsing)))

static const grpc_transport_vtable vtable;

/* forward declarations of various callbacks that we'll build closures around */
static void writing_action(grpc_exec_ctx *exec_ctx, void *t, grpc_error *error);
static void reading_action(grpc_exec_ctx *exec_ctx, void *t, grpc_error *error);
static void parsing_action(grpc_exec_ctx *exec_ctx, void *t, grpc_error *error);
static void reading_action_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                  grpc_error *error);
static void post_parse_locked(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_error *error);
static void initiate_writing_locked(grpc_exec_ctx *exec_ctx, void *t,
                                    grpc_error *error);
static void initiate_read_flush_locked(grpc_exec_ctx *exec_ctx, void *t,
                                       grpc_error *error);
static void terminate_writing_with_lock(grpc_exec_ctx *exec_ctx, void *t,
                                        grpc_error *error);

static void start_writing(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t);
static void end_waiting_for_write(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t, grpc_error *error);

/** Set a transport level setting, and push it to our peer */
static void push_setting(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                         grpc_chttp2_setting_id id, uint32_t value);

/** Start disconnection chain */
static void drop_connection(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                            grpc_error *error);

/** Cancel a stream: coming from the transport API */
static void cancel_from_api(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_global *transport_global,
                            grpc_chttp2_stream_global *stream_global,
                            grpc_error *error);

static void close_from_api(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global,
                           grpc_chttp2_stream_global *stream_global,
                           grpc_error *error);

/** Start new streams that have been created if we can */
static void maybe_start_some_streams(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);

static void connectivity_state_set(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_connectivity_state state, grpc_error *error, const char *reason);

static void check_read_ops(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global);

static void incoming_byte_stream_update_flow_control(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, size_t max_size_hint,
    size_t have_already);
static void incoming_byte_stream_destroy_locked(grpc_exec_ctx *exec_ctx,
                                                void *byte_stream,
                                                grpc_error *error_ignored);
static void fail_pending_writes(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_transport_global *transport_global,
                                grpc_chttp2_stream_global *stream_global,
                                grpc_error *error);

static void set_write_state(grpc_chttp2_transport *t,
                            grpc_chttp2_write_state state, const char *reason);

/*******************************************************************************
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

static void destruct_transport(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport *t) {
  size_t i;

  grpc_endpoint_destroy(exec_ctx, t->ep);

  gpr_slice_buffer_destroy(&t->global.qbuf);

  gpr_slice_buffer_destroy(&t->writing.outbuf);
  grpc_chttp2_hpack_compressor_destroy(&t->writing.hpack_compressor);

  gpr_slice_buffer_destroy(&t->parsing.qbuf);
  gpr_slice_buffer_destroy(&t->read_buffer);
  grpc_chttp2_hpack_parser_destroy(&t->parsing.hpack_parser);
  grpc_chttp2_goaway_parser_destroy(&t->parsing.goaway_parser);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(t->lists[i].head == NULL);
    GPR_ASSERT(t->lists[i].tail == NULL);
  }

  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->parsing_stream_map) == 0);
  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->new_stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&t->parsing_stream_map);
  grpc_chttp2_stream_map_destroy(&t->new_stream_map);
  grpc_connectivity_state_destroy(exec_ctx, &t->channel_callback.state_tracker);

  grpc_combiner_destroy(exec_ctx, t->executor.combiner);

  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  while (t->global.pings.next != &t->global.pings) {
    grpc_chttp2_outstanding_ping *ping = t->global.pings.next;
    grpc_exec_ctx_sched(exec_ctx, ping->on_recv,
                        GRPC_ERROR_CREATE("Transport closed"), NULL);
    ping->next->prev = ping->prev;
    ping->prev->next = ping->next;
    gpr_free(ping);
  }

  gpr_free(t->peer_string);
  gpr_free(t);
}

/*#define REFCOUNTING_DEBUG 1*/
#ifdef REFCOUNTING_DEBUG
#define REF_TRANSPORT(t, r) ref_transport(t, r, __FILE__, __LINE__)
#define UNREF_TRANSPORT(cl, t, r) unref_transport(cl, t, r, __FILE__, __LINE__)
static void unref_transport(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                            const char *reason, const char *file, int line) {
  gpr_log(GPR_DEBUG, "chttp2:unref:%p %d->%d %s [%s:%d]", t, t->refs.count,
          t->refs.count - 1, reason, file, line);
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(exec_ctx, t);
}

static void ref_transport(grpc_chttp2_transport *t, const char *reason,
                          const char *file, int line) {
  gpr_log(GPR_DEBUG, "chttp2:  ref:%p %d->%d %s [%s:%d]", t, t->refs.count,
          t->refs.count + 1, reason, file, line);
  gpr_ref(&t->refs);
}
#else
#define REF_TRANSPORT(t, r) ref_transport(t)
#define UNREF_TRANSPORT(cl, t, r) unref_transport(cl, t)
static void unref_transport(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t) {
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(exec_ctx, t);
}

static void ref_transport(grpc_chttp2_transport *t) { gpr_ref(&t->refs); }
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
  t->executor.write_state = GRPC_CHTTP2_WRITES_CORKED;
  t->ep = ep;
  /* one ref is for destroy */
  gpr_ref_init(&t->refs, 1);
  /* ref is dropped at transport close() */
  gpr_ref_init(&t->shutdown_ep_refs, 1);
  t->executor.combiner = grpc_combiner_create(grpc_endpoint_get_workqueue(ep));
  t->peer_string = grpc_endpoint_get_peer(ep);
  t->endpoint_reading = 1;
  t->global.next_stream_id = is_client ? 1 : 2;
  t->global.is_client = is_client;
  t->writing.outgoing_window = DEFAULT_WINDOW;
  t->parsing.incoming_window = DEFAULT_WINDOW;
  t->global.stream_lookahead = DEFAULT_WINDOW;
  t->global.connection_window_target = DEFAULT_CONNECTION_WINDOW_TARGET;
  t->global.ping_counter = 1;
  t->global.pings.next = t->global.pings.prev = &t->global.pings;
  t->parsing.is_client = is_client;
  t->parsing.deframe_state =
      is_client ? GRPC_DTS_FH_0 : GRPC_DTS_CLIENT_PREFIX_0;
  t->parsing.is_first_frame = true;
  t->writing.is_client = is_client;
  grpc_connectivity_state_init(
      &t->channel_callback.state_tracker, GRPC_CHANNEL_READY,
      is_client ? "client_transport" : "server_transport");

  gpr_slice_buffer_init(&t->global.qbuf);

  gpr_slice_buffer_init(&t->writing.outbuf);
  grpc_chttp2_hpack_compressor_init(&t->writing.hpack_compressor);
  grpc_closure_init(&t->writing_action, writing_action, t);
  grpc_closure_init(&t->reading_action, reading_action, t);
  grpc_closure_init(&t->reading_action_locked, reading_action_locked, t);
  grpc_closure_init(&t->parsing_action, parsing_action, t);
  grpc_closure_init(&t->post_parse_locked, post_parse_locked, t);
  grpc_closure_init(&t->initiate_writing, initiate_writing_locked, t);
  grpc_closure_init(&t->terminate_writing, terminate_writing_with_lock, t);
  grpc_closure_init(&t->initiate_read_flush_locked, initiate_read_flush_locked,
                    t);
  grpc_closure_init(&t->writing.done_cb, grpc_chttp2_terminate_writing,
                    &t->writing);

  gpr_slice_buffer_init(&t->parsing.qbuf);
  grpc_chttp2_goaway_parser_init(&t->parsing.goaway_parser);
  grpc_chttp2_hpack_parser_init(&t->parsing.hpack_parser);

  gpr_slice_buffer_init(&t->read_buffer);

  /* 8 is a random stab in the dark as to a good initial size: it's small enough
     that it shouldn't waste memory for infrequently used connections, yet
     large enough that the exponential growth should happen nicely when it's
     needed.
     TODO(ctiller): tune this */
  grpc_chttp2_stream_map_init(&t->parsing_stream_map, 8);
  grpc_chttp2_stream_map_init(&t->new_stream_map, 8);

  /* copy in initial settings to all setting sets */
  for (i = 0; i < GRPC_CHTTP2_NUM_SETTINGS; i++) {
    t->parsing.settings[i] = grpc_chttp2_settings_parameters[i].default_value;
    for (j = 0; j < GRPC_NUM_SETTING_SETS; j++) {
      t->global.settings[j][i] =
          grpc_chttp2_settings_parameters[i].default_value;
    }
  }
  t->global.dirtied_local_settings = 1;
  /* Hack: it's common for implementations to assume 65536 bytes initial send
     window -- this should by rights be 0 */
  t->global.force_send_settings = 1 << GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
  t->global.sent_local_settings = 0;

  if (is_client) {
    gpr_slice_buffer_add(
        &t->writing.outbuf,
        gpr_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING));
    grpc_chttp2_initiate_write(exec_ctx, &t->global, false, "initial_write");
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

  if (channel_args) {
    for (i = 0; i < channel_args->num_args; i++) {
      if (0 ==
          strcmp(channel_args->args[i].key, GRPC_ARG_MAX_CONCURRENT_STREAMS)) {
        if (is_client) {
          gpr_log(GPR_ERROR, "%s: is ignored on the client",
                  GRPC_ARG_MAX_CONCURRENT_STREAMS);
        } else {
          const grpc_integer_options options = {-1, 0, INT_MAX};
          const int value =
              grpc_channel_arg_get_integer(&channel_args->args[i], options);
          if (value >= 0) {
            push_setting(exec_ctx, t,
                         GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                         (uint32_t)value);
          }
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER)) {
        const grpc_integer_options options = {-1, 0, INT_MAX};
        const int value =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
        if (value >= 0) {
          if ((t->global.next_stream_id & 1) != (value & 1)) {
            gpr_log(GPR_ERROR, "%s: low bit must be %d on %s",
                    GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER,
                    t->global.next_stream_id & 1,
                    is_client ? "client" : "server");
          } else {
            t->global.next_stream_id = (uint32_t)value;
          }
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES)) {
        const grpc_integer_options options = {-1, 5, INT_MAX};
        const int value =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
        if (value >= 0) {
          t->global.stream_lookahead = (uint32_t)value;
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER)) {
        const grpc_integer_options options = {-1, 0, INT_MAX};
        const int value =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
        if (value >= 0) {
          push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE,
                       (uint32_t)value);
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER)) {
        const grpc_integer_options options = {-1, 0, INT_MAX};
        const int value =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
        if (value >= 0) {
          grpc_chttp2_hpack_compressor_set_max_usable_size(
              &t->writing.hpack_compressor, (uint32_t)value);
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_MAX_METADATA_SIZE)) {
        const grpc_integer_options options = {-1, 0, INT_MAX};
        const int value =
            grpc_channel_arg_get_integer(&channel_args->args[i], options);
        if (value >= 0) {
          push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
                       (uint32_t)value);
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_MAX_FRAME_SIZE)) {
        if (channel_args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s: must be an integer",
                  GRPC_ARG_HTTP2_MAX_FRAME_SIZE);
        } else if (channel_args->args[i].value.integer < 16384 ||
                   channel_args->args[i].value.integer > 16777215) {
          gpr_log(GPR_ERROR, "%s: must be between 16384 and 16777215",
                  GRPC_ARG_HTTP2_MAX_FRAME_SIZE);
        } else {
          push_setting(exec_ctx, t, GRPC_CHTTP2_SETTINGS_MAX_FRAME_SIZE,
                       (uint32_t)channel_args->args[i].value.integer);
        }
      }
    }
  }

  set_write_state(t, GRPC_CHTTP2_WRITING_INACTIVE, "uncork");
  grpc_chttp2_initiate_write(exec_ctx, &t->global, false, "init");
}

static void destroy_transport_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                     grpc_error *error) {
  grpc_chttp2_transport *t = tp;
  t->destroying = 1;
  drop_connection(exec_ctx, t, GRPC_ERROR_CREATE("Transport destroyed"));
  UNREF_TRANSPORT(exec_ctx, t, "destroy");
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_combiner_execute(exec_ctx, t->executor.combiner,
                        grpc_closure_create(destroy_transport_locked, t),
                        GRPC_ERROR_NONE);
}

/** block grpc_endpoint_shutdown being called until a paired
    allow_endpoint_shutdown is made */
static void prevent_endpoint_shutdown(grpc_chttp2_transport *t) {
  gpr_ref(&t->shutdown_ep_refs);
}

static void allow_endpoint_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_transport *t) {
  if (gpr_unref(&t->shutdown_ep_refs)) {
    grpc_endpoint_shutdown(exec_ctx, t->ep);
  }
}

static void close_transport_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_chttp2_transport *t,
                                   grpc_error *error) {
  if (!t->closed) {
    if (grpc_http_write_state_trace) {
      gpr_log(GPR_DEBUG, "W:%p close transport", t);
    }
    t->closed = 1;
    connectivity_state_set(exec_ctx, &t->global, GRPC_CHANNEL_SHUTDOWN,
                           GRPC_ERROR_REF(error), "close_transport");
    allow_endpoint_shutdown_locked(exec_ctx, t);

    /* flush writable stream list to avoid dangling references */
    grpc_chttp2_stream_global *stream_global;
    grpc_chttp2_stream_writing *stream_writing;
    while (grpc_chttp2_list_pop_writable_stream(
        &t->global, &t->writing, &stream_global, &stream_writing)) {
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2_writing");
    }
  }
  GRPC_ERROR_UNREF(error);
}

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_chttp2_stream_ref(grpc_chttp2_stream_global *stream_global,
                            const char *reason) {
  grpc_stream_ref(STREAM_FROM_GLOBAL(stream_global)->refcount, reason);
}
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx,
                              grpc_chttp2_stream_global *stream_global,
                              const char *reason) {
  grpc_stream_unref(exec_ctx, STREAM_FROM_GLOBAL(stream_global)->refcount,
                    reason);
}
#else
void grpc_chttp2_stream_ref(grpc_chttp2_stream_global *stream_global) {
  grpc_stream_ref(STREAM_FROM_GLOBAL(stream_global)->refcount);
}
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx,
                              grpc_chttp2_stream_global *stream_global) {
  grpc_stream_unref(exec_ctx, STREAM_FROM_GLOBAL(stream_global)->refcount);
}
#endif

static void finish_init_stream_locked(grpc_exec_ctx *exec_ctx, void *sp,
                                      grpc_error *error) {
  grpc_chttp2_stream *s = sp;
  grpc_chttp2_register_stream(s->t, s);
  GRPC_CHTTP2_STREAM_UNREF(exec_ctx, &s->global, "init");
}

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
  gpr_ref_init(&s->global.active_streams, 1);
  GRPC_CHTTP2_STREAM_REF(&s->global, "chttp2");

  grpc_chttp2_incoming_metadata_buffer_init(&s->parsing.metadata_buffer[0]);
  grpc_chttp2_incoming_metadata_buffer_init(&s->parsing.metadata_buffer[1]);
  grpc_chttp2_incoming_metadata_buffer_init(
      &s->global.received_initial_metadata);
  grpc_chttp2_incoming_metadata_buffer_init(
      &s->global.received_trailing_metadata);
  grpc_chttp2_data_parser_init(&s->parsing.data_parser);
  gpr_slice_buffer_init(&s->writing.flow_controlled_buffer);
  s->global.deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);

  REF_TRANSPORT(t, "stream");

  if (server_data) {
    GPR_ASSERT(t->executor.parsing_active);
    s->global.id = (uint32_t)(uintptr_t)server_data;
    s->parsing.id = s->global.id;
    s->global.outgoing_window =
        t->global.settings[GRPC_PEER_SETTINGS]
                          [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    s->parsing.incoming_window = s->global.max_recv_bytes =
        t->global.settings[GRPC_SENT_SETTINGS]
                          [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    *t->accepting_stream = s;
    grpc_chttp2_stream_map_add(&t->parsing_stream_map, s->global.id, s);
    s->global.in_stream_map = true;
  }

  grpc_closure_init(&s->init_stream, finish_init_stream_locked, s);
  GRPC_CHTTP2_STREAM_REF(&s->global, "init");
  grpc_combiner_execute(exec_ctx, t->executor.combiner, &s->init_stream,
                        GRPC_ERROR_NONE);

  GPR_TIMER_END("init_stream", 0);

  return 0;
}

static void destroy_stream_locked(grpc_exec_ctx *exec_ctx, void *sp,
                                  grpc_error *error) {
  grpc_byte_stream *bs;
  grpc_chttp2_stream *s = sp;
  grpc_chttp2_transport *t = s->t;

  GPR_TIMER_BEGIN("destroy_stream", 0);

  GPR_ASSERT((s->global.write_closed && s->global.read_closed) ||
             s->global.id == 0);
  GPR_ASSERT(!s->global.in_stream_map);
  if (grpc_chttp2_unregister_stream(t, s) && t->global.sent_goaway) {
    close_transport_locked(
        exec_ctx, t,
        GRPC_ERROR_CREATE("Last stream closed after sending goaway"));
  }
  if (!t->executor.parsing_active && s->global.id) {
    GPR_ASSERT(grpc_chttp2_stream_map_find(&t->parsing_stream_map,
                                           s->global.id) == NULL);
  }

  while (
      (bs = grpc_chttp2_incoming_frame_queue_pop(&s->global.incoming_frames))) {
    incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
  }

  grpc_chttp2_list_remove_unannounced_incoming_window_available(&t->global,
                                                                &s->global);
  grpc_chttp2_list_remove_stalled_by_transport(&t->global, &s->global);
  grpc_chttp2_list_remove_check_read_ops(&t->global, &s->global);

  for (int i = 0; i < STREAM_LIST_COUNT; i++) {
    if (s->included[i]) {
      gpr_log(GPR_ERROR, "%s stream %d still included in list %d",
              t->global.is_client ? "client" : "server", s->global.id, i);
      abort();
    }
  }

  GPR_ASSERT(s->global.send_initial_metadata_finished == NULL);
  GPR_ASSERT(s->global.send_message_finished == NULL);
  GPR_ASSERT(s->global.send_trailing_metadata_finished == NULL);
  GPR_ASSERT(s->global.recv_initial_metadata_ready == NULL);
  GPR_ASSERT(s->global.recv_message_ready == NULL);
  GPR_ASSERT(s->global.recv_trailing_metadata_finished == NULL);
  grpc_chttp2_data_parser_destroy(exec_ctx, &s->parsing.data_parser);
  grpc_chttp2_incoming_metadata_buffer_destroy(&s->parsing.metadata_buffer[0]);
  grpc_chttp2_incoming_metadata_buffer_destroy(&s->parsing.metadata_buffer[1]);
  grpc_chttp2_incoming_metadata_buffer_destroy(
      &s->global.received_initial_metadata);
  grpc_chttp2_incoming_metadata_buffer_destroy(
      &s->global.received_trailing_metadata);
  gpr_slice_buffer_destroy(&s->writing.flow_controlled_buffer);
  GRPC_ERROR_UNREF(s->global.read_closed_error);
  GRPC_ERROR_UNREF(s->global.write_closed_error);

  UNREF_TRANSPORT(exec_ctx, t, "stream");

  GPR_TIMER_END("destroy_stream", 0);

  gpr_free(s->destroy_stream_arg);
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs, void *and_free_memory) {
  GPR_TIMER_BEGIN("destroy_stream", 0);
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  s->destroy_stream_arg = and_free_memory;
  grpc_closure_init(&s->destroy_stream, destroy_stream_locked, s);
  grpc_combiner_execute(exec_ctx, t->executor.combiner, &s->destroy_stream,
                        GRPC_ERROR_NONE);
  GPR_TIMER_END("destroy_stream", 0);
}

grpc_chttp2_stream_parsing *grpc_chttp2_parsing_lookup_stream(
    grpc_chttp2_transport_parsing *transport_parsing, uint32_t id) {
  grpc_chttp2_transport *t = TRANSPORT_FROM_PARSING(transport_parsing);
  grpc_chttp2_stream *s =
      grpc_chttp2_stream_map_find(&t->parsing_stream_map, id);
  return s ? &s->parsing : NULL;
}

grpc_chttp2_stream_parsing *grpc_chttp2_parsing_accept_stream(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    uint32_t id) {
  grpc_chttp2_stream *accepting;
  grpc_chttp2_transport *t = TRANSPORT_FROM_PARSING(transport_parsing);
  GPR_ASSERT(t->accepting_stream == NULL);
  t->accepting_stream = &accepting;
  t->channel_callback.accept_stream(exec_ctx,
                                    t->channel_callback.accept_stream_user_data,
                                    &t->base, (void *)(uintptr_t)id);
  t->accepting_stream = NULL;
  return &accepting->parsing;
}

/*******************************************************************************
 * LOCK MANAGEMENT
 */

static const char *write_state_name(grpc_chttp2_write_state state) {
  switch (state) {
    case GRPC_CHTTP2_WRITES_CORKED:
      return "CORKED";
    case GRPC_CHTTP2_WRITING_INACTIVE:
      return "INACTIVE";
    case GRPC_CHTTP2_WRITE_SCHEDULED:
      return "SCHEDULED";
    case GRPC_CHTTP2_WRITING:
      return "WRITING";
    case GRPC_CHTTP2_WRITING_STALE_WITH_POLLER:
      return "WRITING[p=1]";
    case GRPC_CHTTP2_WRITING_STALE_NO_POLLER:
      return "WRITING[p=0]";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

static void set_write_state(grpc_chttp2_transport *t,
                            grpc_chttp2_write_state state, const char *reason) {
  if (grpc_http_write_state_trace) {
    gpr_log(GPR_DEBUG, "W:%p %s -> %s because %s", t,
            write_state_name(t->executor.write_state), write_state_name(state),
            reason);
  }
  t->executor.write_state = state;
}

static void initiate_writing_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                    grpc_error *error) {
  grpc_chttp2_transport *t = tp;
  GPR_ASSERT(t->executor.write_state == GRPC_CHTTP2_WRITE_SCHEDULED);
  start_writing(exec_ctx, t);
}

static void initiate_read_flush_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                       grpc_error *error) {
  grpc_chttp2_transport *t = tp;
  t->executor.check_read_ops_scheduled = false;
  check_read_ops(exec_ctx, &t->global);
}

/*******************************************************************************
 * OUTPUT PROCESSING
 */

void grpc_chttp2_initiate_write(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_transport_global *transport_global,
                                bool covered_by_poller, const char *reason) {
  GPR_TIMER_BEGIN("grpc_chttp2_initiate_write", 0);

  /* Perform state checks, and transition to a scheduled state if appropriate.
     If we are inactive, schedule a write chain to begin once the transport
     combiner finishes any executions in its current batch (which may be
     scheduled AFTER this code executes). The write chain will:
      - call start_writing, which verifies (under the global lock) that there
        are things that need to be written by calling
        grpc_chttp2_unlocking_check_writes, and if so schedules writing_action
        against the current exec_ctx, to be executed OUTSIDE of the global lock
      - eventually writing_action results in grpc_chttp2_terminate_writing being
        called, which re-takes the global lock, updates state, checks if we need
        to do *another* write immediately, and if so loops back to
        start_writing.

     Current problems:
       - too much lock entry/exiting
       - the writing thread can become stuck indefinitely (punt through the
         workqueue periodically to fix) */

  grpc_chttp2_transport *t = TRANSPORT_FROM_GLOBAL(transport_global);
  switch (t->executor.write_state) {
    case GRPC_CHTTP2_WRITES_CORKED:
      break;
    case GRPC_CHTTP2_WRITING_INACTIVE:
      set_write_state(t, GRPC_CHTTP2_WRITE_SCHEDULED, reason);
      REF_TRANSPORT(t, "writing");
      grpc_combiner_execute_finally(exec_ctx, t->executor.combiner,
                                    &t->initiate_writing, GRPC_ERROR_NONE,
                                    covered_by_poller);
      break;
    case GRPC_CHTTP2_WRITE_SCHEDULED:
      if (covered_by_poller) {
        /* upgrade to note poller is available to cover the write */
        grpc_combiner_force_async_finally(t->executor.combiner);
      }
      break;
    case GRPC_CHTTP2_WRITING:
      set_write_state(t,
                      covered_by_poller ? GRPC_CHTTP2_WRITING_STALE_WITH_POLLER
                                        : GRPC_CHTTP2_WRITING_STALE_NO_POLLER,
                      reason);
      break;
    case GRPC_CHTTP2_WRITING_STALE_WITH_POLLER:
      /* nothing to do: write already requested */
      break;
    case GRPC_CHTTP2_WRITING_STALE_NO_POLLER:
      if (covered_by_poller) {
        /* upgrade to note poller is available to cover the write */
        set_write_state(t, GRPC_CHTTP2_WRITING_STALE_WITH_POLLER, reason);
      }
      break;
  }
  GPR_TIMER_END("grpc_chttp2_initiate_write", 0);
}

static void start_writing(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t) {
  GPR_TIMER_BEGIN("start_writing", 0);
  GPR_ASSERT(t->executor.write_state == GRPC_CHTTP2_WRITE_SCHEDULED);
  if (!t->closed &&
      grpc_chttp2_unlocking_check_writes(exec_ctx, &t->global, &t->writing)) {
    set_write_state(t, GRPC_CHTTP2_WRITING, "start_writing");
    prevent_endpoint_shutdown(t);
    grpc_exec_ctx_sched(exec_ctx, &t->writing_action, GRPC_ERROR_NONE, NULL);
  } else {
    if (t->closed) {
      set_write_state(t, GRPC_CHTTP2_WRITING_INACTIVE,
                      "start_writing:transport_closed");
    } else {
      set_write_state(t, GRPC_CHTTP2_WRITING_INACTIVE,
                      "start_writing:nothing_to_write");
    }
    end_waiting_for_write(exec_ctx, t, GRPC_ERROR_NONE);
    UNREF_TRANSPORT(exec_ctx, t, "writing");
  }
  GPR_TIMER_END("start_writing", 0);
}

void grpc_chttp2_become_writable(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport_global *transport_global,
                                 grpc_chttp2_stream_global *stream_global,
                                 bool covered_by_poller, const char *reason) {
  if (!TRANSPORT_FROM_GLOBAL(transport_global)->closed &&
      grpc_chttp2_list_add_writable_stream(transport_global, stream_global)) {
    GRPC_CHTTP2_STREAM_REF(stream_global, "chttp2_writing");
    grpc_chttp2_initiate_write(exec_ctx, transport_global, covered_by_poller,
                               reason);
  }
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
  if (use_value != t->global.settings[GRPC_LOCAL_SETTINGS][id]) {
    t->global.settings[GRPC_LOCAL_SETTINGS][id] = use_value;
    t->global.dirtied_local_settings = 1;
    grpc_chttp2_initiate_write(exec_ctx, &t->global, false, "push_setting");
  }
}

/* error may be GRPC_ERROR_NONE if there is no error allocated yet.
   In that case, use "reason" as the text for a new error. */
static void end_waiting_for_write(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t, grpc_error *error) {
  grpc_chttp2_stream_global *stream_global;
  while (grpc_chttp2_list_pop_closed_waiting_for_writing(&t->global,
                                                         &stream_global)) {
    fail_pending_writes(exec_ctx, &t->global, stream_global,
                        GRPC_ERROR_REF(error));
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "finish_writes");
  }
  GRPC_ERROR_UNREF(error);
}

static void terminate_writing_with_lock(grpc_exec_ctx *exec_ctx, void *tp,
                                        grpc_error *error) {
  GPR_TIMER_BEGIN("terminate_writing_with_lock", 0);
  grpc_chttp2_transport *t = tp;
  allow_endpoint_shutdown_locked(exec_ctx, t);

  if (error != GRPC_ERROR_NONE) {
    drop_connection(exec_ctx, t, GRPC_ERROR_REF(error));
  }

  grpc_chttp2_cleanup_writing(exec_ctx, &t->global, &t->writing);

  end_waiting_for_write(exec_ctx, t, GRPC_ERROR_REF(error));

  switch (t->executor.write_state) {
    case GRPC_CHTTP2_WRITES_CORKED:
    case GRPC_CHTTP2_WRITING_INACTIVE:
    case GRPC_CHTTP2_WRITE_SCHEDULED:
      GPR_UNREACHABLE_CODE(break);
    case GRPC_CHTTP2_WRITING:
      GPR_TIMER_MARK("state=writing", 0);
      set_write_state(t, GRPC_CHTTP2_WRITING_INACTIVE, "terminate_writing");
      break;
    case GRPC_CHTTP2_WRITING_STALE_WITH_POLLER:
      GPR_TIMER_MARK("state=writing_stale_with_poller", 0);
      set_write_state(t, GRPC_CHTTP2_WRITE_SCHEDULED, "terminate_writing");
      REF_TRANSPORT(t, "writing");
      grpc_combiner_execute_finally(exec_ctx, t->executor.combiner,
                                    &t->initiate_writing, GRPC_ERROR_NONE,
                                    true);
      break;
    case GRPC_CHTTP2_WRITING_STALE_NO_POLLER:
      GPR_TIMER_MARK("state=writing_stale_no_poller", 0);
      set_write_state(t, GRPC_CHTTP2_WRITE_SCHEDULED, "terminate_writing");
      REF_TRANSPORT(t, "writing");
      grpc_combiner_execute_finally(exec_ctx, t->executor.combiner,
                                    &t->initiate_writing, GRPC_ERROR_NONE,
                                    false);
      break;
  }

  UNREF_TRANSPORT(exec_ctx, t, "writing");
  GPR_TIMER_END("terminate_writing_with_lock", 0);
}

void grpc_chttp2_terminate_writing(grpc_exec_ctx *exec_ctx,
                                   void *transport_writing, grpc_error *error) {
  GPR_TIMER_BEGIN("grpc_chttp2_terminate_writing", 0);
  grpc_chttp2_transport *t = TRANSPORT_FROM_WRITING(transport_writing);
  grpc_combiner_execute(exec_ctx, t->executor.combiner, &t->terminate_writing,
                        GRPC_ERROR_REF(error));
  GPR_TIMER_END("grpc_chttp2_terminate_writing", 0);
}

static void writing_action(grpc_exec_ctx *exec_ctx, void *gt,
                           grpc_error *error) {
  grpc_chttp2_transport *t = gt;
  GPR_TIMER_BEGIN("writing_action", 0);
  grpc_chttp2_perform_writes(exec_ctx, &t->writing, t->ep);
  GPR_TIMER_END("writing_action", 0);
}

void grpc_chttp2_add_incoming_goaway(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    uint32_t goaway_error, gpr_slice goaway_text) {
  char *msg = gpr_dump_slice(goaway_text, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_DEBUG, "got goaway [%d]: %s", goaway_error, msg));
  gpr_slice_unref(goaway_text);
  transport_global->seen_goaway = 1;
  /* lie: use transient failure from the transport to indicate goaway has been
   * received */
  connectivity_state_set(
      exec_ctx, transport_global, GRPC_CHANNEL_TRANSIENT_FAILURE,
      grpc_error_set_str(
          grpc_error_set_int(GRPC_ERROR_CREATE("GOAWAY received"),
                             GRPC_ERROR_INT_HTTP2_ERROR,
                             (intptr_t)goaway_error),
          GRPC_ERROR_STR_RAW_BYTES, msg),
      "got_goaway");
  gpr_free(msg);
}

static void maybe_start_some_streams(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global) {
  grpc_chttp2_stream_global *stream_global;
  uint32_t stream_incoming_window;
  /* start streams where we have free grpc_chttp2_stream ids and free
   * concurrency */
  while (transport_global->next_stream_id <= MAX_CLIENT_STREAM_ID &&
         transport_global->concurrent_stream_count <
             transport_global
                 ->settings[GRPC_PEER_SETTINGS]
                           [GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS] &&
         grpc_chttp2_list_pop_waiting_for_concurrency(transport_global,
                                                      &stream_global)) {
    /* safe since we can't (legally) be parsing this stream yet */
    grpc_chttp2_stream_parsing *stream_parsing =
        &STREAM_FROM_GLOBAL(stream_global)->parsing;
    GRPC_CHTTP2_IF_TRACING(gpr_log(
        GPR_DEBUG, "HTTP:%s: Allocating new grpc_chttp2_stream %p to id %d",
        transport_global->is_client ? "CLI" : "SVR", stream_global,
        transport_global->next_stream_id));

    GPR_ASSERT(stream_global->id == 0);
    stream_global->id = stream_parsing->id = transport_global->next_stream_id;
    transport_global->next_stream_id += 2;

    if (transport_global->next_stream_id >= MAX_CLIENT_STREAM_ID) {
      connectivity_state_set(
          exec_ctx, transport_global, GRPC_CHANNEL_TRANSIENT_FAILURE,
          GRPC_ERROR_CREATE("Stream IDs exhausted"), "no_more_stream_ids");
    }

    stream_global->outgoing_window =
        transport_global->settings[GRPC_PEER_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    stream_parsing->incoming_window = stream_incoming_window =
        transport_global->settings[GRPC_SENT_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    stream_global->max_recv_bytes =
        GPR_MAX(stream_incoming_window, stream_global->max_recv_bytes);
    grpc_chttp2_stream_map_add(
        &TRANSPORT_FROM_GLOBAL(transport_global)->new_stream_map,
        stream_global->id, STREAM_FROM_GLOBAL(stream_global));
    stream_global->in_stream_map = true;
    transport_global->concurrent_stream_count++;
    grpc_chttp2_become_writable(exec_ctx, transport_global, stream_global, true,
                                "new_stream");
  }
  /* cancel out streams that will never be started */
  while (transport_global->next_stream_id >= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_list_pop_waiting_for_concurrency(transport_global,
                                                      &stream_global)) {
    cancel_from_api(exec_ctx, transport_global, stream_global,
                    grpc_error_set_int(
                        GRPC_ERROR_CREATE("Stream IDs exhausted"),
                        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE));
  }
}

#define CLOSURE_BARRIER_STATS_BIT (1 << 0)
#define CLOSURE_BARRIER_FIRST_REF_BIT (1 << 16)

static grpc_closure *add_closure_barrier(grpc_closure *closure) {
  closure->next_data.scratch += CLOSURE_BARRIER_FIRST_REF_BIT;
  return closure;
}

void grpc_chttp2_complete_closure_step(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, grpc_closure **pclosure,
    grpc_error *error) {
  grpc_closure *closure = *pclosure;
  if (closure == NULL) {
    GRPC_ERROR_UNREF(error);
    return;
  }
  closure->next_data.scratch -= CLOSURE_BARRIER_FIRST_REF_BIT;
  if (error != GRPC_ERROR_NONE) {
    if (closure->error == GRPC_ERROR_NONE) {
      closure->error =
          GRPC_ERROR_CREATE("Error in HTTP transport completing operation");
      closure->error = grpc_error_set_str(
          closure->error, GRPC_ERROR_STR_TARGET_ADDRESS,
          TRANSPORT_FROM_GLOBAL(transport_global)->peer_string);
    }
    closure->error = grpc_error_add_child(closure->error, error);
  }
  if (closure->next_data.scratch < CLOSURE_BARRIER_FIRST_REF_BIT) {
    if (closure->next_data.scratch & CLOSURE_BARRIER_STATS_BIT) {
      grpc_transport_move_stats(&stream_global->stats,
                                stream_global->collecting_stats);
      stream_global->collecting_stats = NULL;
    }
    grpc_exec_ctx_sched(exec_ctx, closure, closure->error, NULL);
  }
  *pclosure = NULL;
}

static int contains_non_ok_status(
    grpc_chttp2_transport_global *transport_global,
    grpc_metadata_batch *batch) {
  grpc_linked_mdelem *l;
  for (l = batch->list.head; l; l = l->next) {
    if (l->md->key == GRPC_MDSTR_GRPC_STATUS &&
        l->md != GRPC_MDELEM_GRPC_STATUS_0) {
      return 1;
    }
  }
  return 0;
}

static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

static void perform_stream_op_locked(grpc_exec_ctx *exec_ctx, void *stream_op,
                                     grpc_error *error_ignored) {
  GPR_TIMER_BEGIN("perform_stream_op_locked", 0);

  grpc_transport_stream_op *op = stream_op;
  grpc_chttp2_transport *t = op->transport_private.args[0];
  grpc_chttp2_stream *s = op->transport_private.args[1];
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global = &s->global;

  if (grpc_http_trace) {
    char *str = grpc_transport_stream_op_string(op);
    gpr_log(GPR_DEBUG, "perform_stream_op_locked: %s", str);
    gpr_free(str);
  }

  grpc_closure *on_complete = op->on_complete;
  if (on_complete == NULL) {
    on_complete = grpc_closure_create(do_nothing, NULL);
  }
  /* use final_data as a barrier until enqueue time; the inital counter is
     dropped at the end of this function */
  on_complete->next_data.scratch = CLOSURE_BARRIER_FIRST_REF_BIT;
  on_complete->error = GRPC_ERROR_NONE;

  if (op->collect_stats != NULL) {
    GPR_ASSERT(stream_global->collecting_stats == NULL);
    stream_global->collecting_stats = op->collect_stats;
    on_complete->next_data.scratch |= CLOSURE_BARRIER_STATS_BIT;
  }

  if (op->cancel_error != GRPC_ERROR_NONE) {
    cancel_from_api(exec_ctx, transport_global, stream_global,
                    GRPC_ERROR_REF(op->cancel_error));
  }

  if (op->close_error != GRPC_ERROR_NONE) {
    close_from_api(exec_ctx, transport_global, stream_global,
                   GRPC_ERROR_REF(op->close_error));
  }

  if (op->send_initial_metadata != NULL) {
    GPR_ASSERT(stream_global->send_initial_metadata_finished == NULL);
    stream_global->send_initial_metadata_finished =
        add_closure_barrier(on_complete);
    stream_global->send_initial_metadata = op->send_initial_metadata;
    const size_t metadata_size =
        grpc_metadata_batch_size(op->send_initial_metadata);
    const size_t metadata_peer_limit =
        transport_global->settings[GRPC_PEER_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (transport_global->is_client) {
      stream_global->deadline =
          gpr_time_min(stream_global->deadline,
                       stream_global->send_initial_metadata->deadline);
    }
    if (metadata_size > metadata_peer_limit) {
      cancel_from_api(
          exec_ctx, transport_global, stream_global,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(
                      GRPC_ERROR_CREATE("to-be-sent initial metadata size "
                                        "exceeds peer limit"),
                      GRPC_ERROR_INT_SIZE, (intptr_t)metadata_size),
                  GRPC_ERROR_INT_LIMIT, (intptr_t)metadata_peer_limit),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
    } else {
      if (contains_non_ok_status(transport_global, op->send_initial_metadata)) {
        stream_global->seen_error = true;
        grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                            stream_global);
      }
      if (!stream_global->write_closed) {
        if (transport_global->is_client) {
          GPR_ASSERT(stream_global->id == 0);
          grpc_chttp2_list_add_waiting_for_concurrency(transport_global,
                                                       stream_global);
          maybe_start_some_streams(exec_ctx, transport_global);
        } else {
          GPR_ASSERT(stream_global->id != 0);
          grpc_chttp2_become_writable(exec_ctx, transport_global, stream_global,
                                      true, "op.send_initial_metadata");
        }
      } else {
        stream_global->send_trailing_metadata = NULL;
        grpc_chttp2_complete_closure_step(
            exec_ctx, transport_global, stream_global,
            &stream_global->send_initial_metadata_finished,
            GRPC_ERROR_CREATE(
                "Attempt to send initial metadata after stream was closed"));
      }
    }
  }

  if (op->send_message != NULL) {
    GPR_ASSERT(stream_global->send_message_finished == NULL);
    GPR_ASSERT(stream_global->send_message == NULL);
    stream_global->send_message_finished = add_closure_barrier(on_complete);
    if (stream_global->write_closed) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, transport_global, stream_global,
          &stream_global->send_message_finished,
          GRPC_ERROR_CREATE("Attempt to send message after stream was closed"));
    } else {
      stream_global->send_message = op->send_message;
      if (stream_global->id != 0) {
        grpc_chttp2_become_writable(exec_ctx, transport_global, stream_global,
                                    true, "op.send_message");
      }
    }
  }

  if (op->send_trailing_metadata != NULL) {
    GPR_ASSERT(stream_global->send_trailing_metadata_finished == NULL);
    stream_global->send_trailing_metadata_finished =
        add_closure_barrier(on_complete);
    stream_global->send_trailing_metadata = op->send_trailing_metadata;
    const size_t metadata_size =
        grpc_metadata_batch_size(op->send_trailing_metadata);
    const size_t metadata_peer_limit =
        transport_global->settings[GRPC_PEER_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE];
    if (metadata_size > metadata_peer_limit) {
      cancel_from_api(
          exec_ctx, transport_global, stream_global,
          grpc_error_set_int(
              grpc_error_set_int(
                  grpc_error_set_int(
                      GRPC_ERROR_CREATE("to-be-sent trailing metadata size "
                                        "exceeds peer limit"),
                      GRPC_ERROR_INT_SIZE, (intptr_t)metadata_size),
                  GRPC_ERROR_INT_LIMIT, (intptr_t)metadata_peer_limit),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
    } else {
      if (contains_non_ok_status(transport_global,
                                 op->send_trailing_metadata)) {
        stream_global->seen_error = true;
        grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                            stream_global);
      }
      if (stream_global->write_closed) {
        stream_global->send_trailing_metadata = NULL;
        grpc_chttp2_complete_closure_step(
            exec_ctx, transport_global, stream_global,
            &stream_global->send_trailing_metadata_finished,
            grpc_metadata_batch_is_empty(op->send_trailing_metadata)
                ? GRPC_ERROR_NONE
                : GRPC_ERROR_CREATE("Attempt to send trailing metadata after "
                                    "stream was closed"));
      } else if (stream_global->id != 0) {
        /* TODO(ctiller): check if there's flow control for any outstanding
           bytes before going writable */
        grpc_chttp2_become_writable(exec_ctx, transport_global, stream_global,
                                    true, "op.send_trailing_metadata");
      }
    }
  }

  if (op->recv_initial_metadata != NULL) {
    GPR_ASSERT(stream_global->recv_initial_metadata_ready == NULL);
    stream_global->recv_initial_metadata_ready =
        op->recv_initial_metadata_ready;
    stream_global->recv_initial_metadata = op->recv_initial_metadata;
    grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                        stream_global);
  }

  if (op->recv_message != NULL) {
    GPR_ASSERT(stream_global->recv_message_ready == NULL);
    stream_global->recv_message_ready = op->recv_message_ready;
    stream_global->recv_message = op->recv_message;
    if (stream_global->id != 0 &&
        (stream_global->incoming_frames.head == NULL ||
         stream_global->incoming_frames.head->is_tail)) {
      incoming_byte_stream_update_flow_control(
          exec_ctx, transport_global, stream_global,
          transport_global->stream_lookahead, 0);
    }
    grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                        stream_global);
  }

  if (op->recv_trailing_metadata != NULL) {
    GPR_ASSERT(stream_global->recv_trailing_metadata_finished == NULL);
    stream_global->recv_trailing_metadata_finished =
        add_closure_barrier(on_complete);
    stream_global->recv_trailing_metadata = op->recv_trailing_metadata;
    stream_global->final_metadata_requested = true;
    grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                        stream_global);
  }

  grpc_chttp2_complete_closure_step(exec_ctx, transport_global, stream_global,
                                    &on_complete, GRPC_ERROR_NONE);

  GPR_TIMER_END("perform_stream_op_locked", 0);
  GRPC_CHTTP2_STREAM_UNREF(exec_ctx, &s->global, "perform_stream_op");
}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs, grpc_transport_stream_op *op) {
  GPR_TIMER_BEGIN("perform_stream_op", 0);
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;
  grpc_closure_init(&op->transport_private.closure, perform_stream_op_locked,
                    op);
  op->transport_private.args[0] = gt;
  op->transport_private.args[1] = gs;
  GRPC_CHTTP2_STREAM_REF(&s->global, "perform_stream_op");
  grpc_combiner_execute(exec_ctx, t->executor.combiner,
                        &op->transport_private.closure, GRPC_ERROR_NONE);
  GPR_TIMER_END("perform_stream_op", 0);
}

static void send_ping_locked(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                             grpc_closure *on_recv) {
  grpc_chttp2_outstanding_ping *p = gpr_malloc(sizeof(*p));
  p->next = &t->global.pings;
  p->prev = p->next->prev;
  p->prev->next = p->next->prev = p;
  p->id[0] = (uint8_t)((t->global.ping_counter >> 56) & 0xff);
  p->id[1] = (uint8_t)((t->global.ping_counter >> 48) & 0xff);
  p->id[2] = (uint8_t)((t->global.ping_counter >> 40) & 0xff);
  p->id[3] = (uint8_t)((t->global.ping_counter >> 32) & 0xff);
  p->id[4] = (uint8_t)((t->global.ping_counter >> 24) & 0xff);
  p->id[5] = (uint8_t)((t->global.ping_counter >> 16) & 0xff);
  p->id[6] = (uint8_t)((t->global.ping_counter >> 8) & 0xff);
  p->id[7] = (uint8_t)(t->global.ping_counter & 0xff);
  p->on_recv = on_recv;
  gpr_slice_buffer_add(&t->global.qbuf, grpc_chttp2_ping_create(0, p->id));
  grpc_chttp2_initiate_write(exec_ctx, &t->global, true, "send_ping");
}

typedef struct ack_ping_args {
  grpc_closure closure;
  grpc_chttp2_transport *t;
  uint8_t opaque_8bytes[8];
} ack_ping_args;

static void ack_ping_locked(grpc_exec_ctx *exec_ctx, void *a,
                            grpc_error *error_ignored) {
  ack_ping_args *args = a;
  grpc_chttp2_outstanding_ping *ping;
  grpc_chttp2_transport_global *transport_global = &args->t->global;
  for (ping = transport_global->pings.next; ping != &transport_global->pings;
       ping = ping->next) {
    if (0 == memcmp(args->opaque_8bytes, ping->id, 8)) {
      grpc_exec_ctx_sched(exec_ctx, ping->on_recv, GRPC_ERROR_NONE, NULL);
      ping->next->prev = ping->prev;
      ping->prev->next = ping->next;
      gpr_free(ping);
      break;
    }
  }
  UNREF_TRANSPORT(exec_ctx, args->t, "ack_ping");
  gpr_free(args);
}

void grpc_chttp2_ack_ping(grpc_exec_ctx *exec_ctx,
                          grpc_chttp2_transport_parsing *transport_parsing,
                          const uint8_t *opaque_8bytes) {
  ack_ping_args *args = gpr_malloc(sizeof(*args));
  args->t = TRANSPORT_FROM_PARSING(transport_parsing);
  memcpy(args->opaque_8bytes, opaque_8bytes, sizeof(args->opaque_8bytes));
  grpc_closure_init(&args->closure, ack_ping_locked, args);
  REF_TRANSPORT(args->t, "ack_ping");
  grpc_combiner_execute(exec_ctx, args->t->executor.combiner, &args->closure,
                        GRPC_ERROR_NONE);
}

static void perform_transport_op_locked(grpc_exec_ctx *exec_ctx,
                                        void *stream_op,
                                        grpc_error *error_ignored) {
  grpc_transport_op *op = stream_op;
  grpc_chttp2_transport *t = op->transport_private.args[0];
  grpc_error *close_transport = op->disconnect_with_error;

  /* If there's a set_accept_stream ensure that we're not parsing
     to avoid changing things out from underneath */
  if (t->executor.parsing_active && op->set_accept_stream) {
    GPR_ASSERT(t->post_parsing_op == NULL);
    t->post_parsing_op = gpr_malloc(sizeof(*op));
    memcpy(t->post_parsing_op, op, sizeof(*op));
    return;
  }

  if (op->on_connectivity_state_change != NULL) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &t->channel_callback.state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
  }

  if (op->send_goaway) {
    t->global.sent_goaway = 1;
    grpc_chttp2_goaway_append(
        t->global.last_incoming_stream_id,
        (uint32_t)grpc_chttp2_grpc_status_to_http2_error(op->goaway_status),
        gpr_slice_ref(*op->goaway_message), &t->global.qbuf);
    close_transport = grpc_chttp2_has_streams(t)
                          ? GRPC_ERROR_NONE
                          : GRPC_ERROR_CREATE("GOAWAY sent");
    grpc_chttp2_initiate_write(exec_ctx, &t->global, false, "goaway_sent");
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
    send_ping_locked(exec_ctx, t, op->send_ping);
  }

  if (close_transport != GRPC_ERROR_NONE) {
    close_transport_locked(exec_ctx, t, close_transport);
  }

  grpc_exec_ctx_sched(exec_ctx, op->on_consumed, GRPC_ERROR_NONE, NULL);

  UNREF_TRANSPORT(exec_ctx, t, "transport_op");
}

static void perform_transport_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                 grpc_transport_op *op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  op->transport_private.args[0] = gt;
  grpc_closure_init(&op->transport_private.closure, perform_transport_op_locked,
                    op);
  REF_TRANSPORT(t, "transport_op");
  grpc_combiner_execute(exec_ctx, t->executor.combiner,
                        &op->transport_private.closure, GRPC_ERROR_NONE);
}

/*******************************************************************************
 * INPUT PROCESSING - GENERAL
 */

static void check_read_ops(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global) {
  GPR_TIMER_BEGIN("check_read_ops", 0);
  grpc_chttp2_stream_global *stream_global;
  grpc_byte_stream *bs;
  while (
      grpc_chttp2_list_pop_check_read_ops(transport_global, &stream_global)) {
    if (stream_global->recv_initial_metadata_ready != NULL &&
        stream_global->published_initial_metadata) {
      if (stream_global->seen_error) {
        while ((bs = grpc_chttp2_incoming_frame_queue_pop(
                    &stream_global->incoming_frames)) != NULL) {
          incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
        }
        if (stream_global->exceeded_metadata_size) {
          cancel_from_api(
              exec_ctx, transport_global, stream_global,
              grpc_error_set_int(
                  GRPC_ERROR_CREATE(
                      "received initial metadata size exceeds limit"),
                  GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
        }
      }
      grpc_chttp2_incoming_metadata_buffer_publish(
          &stream_global->received_initial_metadata,
          stream_global->recv_initial_metadata);
      grpc_exec_ctx_sched(exec_ctx, stream_global->recv_initial_metadata_ready,
                          GRPC_ERROR_NONE, NULL);
      stream_global->recv_initial_metadata_ready = NULL;
    }
    if (stream_global->recv_message_ready != NULL) {
      while (stream_global->final_metadata_requested &&
             stream_global->seen_error &&
             (bs = grpc_chttp2_incoming_frame_queue_pop(
                  &stream_global->incoming_frames)) != NULL) {
        incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
      }
      if (stream_global->incoming_frames.head != NULL) {
        *stream_global->recv_message = grpc_chttp2_incoming_frame_queue_pop(
            &stream_global->incoming_frames);
        GPR_ASSERT(*stream_global->recv_message != NULL);
        grpc_exec_ctx_sched(exec_ctx, stream_global->recv_message_ready,
                            GRPC_ERROR_NONE, NULL);
        stream_global->recv_message_ready = NULL;
      } else if (stream_global->published_trailing_metadata) {
        *stream_global->recv_message = NULL;
        grpc_exec_ctx_sched(exec_ctx, stream_global->recv_message_ready,
                            GRPC_ERROR_NONE, NULL);
        stream_global->recv_message_ready = NULL;
      }
    }
    if (stream_global->recv_trailing_metadata_finished != NULL &&
        stream_global->read_closed && stream_global->write_closed) {
      if (stream_global->seen_error) {
        while ((bs = grpc_chttp2_incoming_frame_queue_pop(
                    &stream_global->incoming_frames)) != NULL) {
          incoming_byte_stream_destroy_locked(exec_ctx, bs, GRPC_ERROR_NONE);
        }
        if (stream_global->exceeded_metadata_size) {
          cancel_from_api(
              exec_ctx, transport_global, stream_global,
              grpc_error_set_int(
                  GRPC_ERROR_CREATE(
                      "received trailing metadata size exceeds limit"),
                  GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_RESOURCE_EXHAUSTED));
        }
      }
      if (stream_global->all_incoming_byte_streams_finished) {
        grpc_chttp2_incoming_metadata_buffer_publish(
            &stream_global->received_trailing_metadata,
            stream_global->recv_trailing_metadata);
        grpc_chttp2_complete_closure_step(
            exec_ctx, transport_global, stream_global,
            &stream_global->recv_trailing_metadata_finished, GRPC_ERROR_NONE);
      }
    }
  }
  GPR_TIMER_END("check_read_ops", 0);
}

static void decrement_active_streams_locked(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  if ((stream_global->all_incoming_byte_streams_finished =
           gpr_unref(&stream_global->active_streams))) {
    grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                        stream_global);
  }
}

static void remove_stream(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                          uint32_t id, grpc_error *error) {
  size_t new_stream_count;
  grpc_chttp2_stream *s =
      grpc_chttp2_stream_map_delete(&t->parsing_stream_map, id);
  if (!s) {
    s = grpc_chttp2_stream_map_delete(&t->new_stream_map, id);
  }
  GPR_ASSERT(s);
  s->global.in_stream_map = false;
  if (t->parsing.incoming_stream == &s->parsing) {
    t->parsing.incoming_stream = NULL;
    grpc_chttp2_parsing_become_skip_parser(exec_ctx, &t->parsing);
  }
  if (s->parsing.data_parser.parsing_frame != NULL) {
    grpc_chttp2_incoming_byte_stream_finished(
        exec_ctx, s->parsing.data_parser.parsing_frame, GRPC_ERROR_REF(error),
        0);
    s->parsing.data_parser.parsing_frame = NULL;
  }

  if (grpc_chttp2_unregister_stream(t, s) && t->global.sent_goaway) {
    close_transport_locked(
        exec_ctx, t, GRPC_ERROR_CREATE_REFERENCING(
                         "Last stream closed after sending GOAWAY", &error, 1));
  }
  if (grpc_chttp2_list_remove_writable_stream(&t->global, &s->global)) {
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, &s->global, "chttp2_writing");
  }

  new_stream_count = grpc_chttp2_stream_map_size(&t->parsing_stream_map) +
                     grpc_chttp2_stream_map_size(&t->new_stream_map);
  GPR_ASSERT(new_stream_count <= UINT32_MAX);
  if (new_stream_count != t->global.concurrent_stream_count) {
    t->global.concurrent_stream_count = (uint32_t)new_stream_count;
    maybe_start_some_streams(exec_ctx, &t->global);
  }
  GRPC_ERROR_UNREF(error);
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

static void cancel_from_api(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_global *transport_global,
                            grpc_chttp2_stream_global *stream_global,
                            grpc_error *due_to_error) {
  if (!stream_global->read_closed || !stream_global->write_closed) {
    grpc_status_code grpc_status;
    grpc_chttp2_error_code http_error;
    status_codes_from_error(due_to_error, stream_global->deadline, &http_error,
                            &grpc_status);

    if (stream_global->id != 0) {
      gpr_slice_buffer_add(
          &transport_global->qbuf,
          grpc_chttp2_rst_stream_create(stream_global->id, (uint32_t)http_error,
                                        &stream_global->stats.outgoing));
      grpc_chttp2_initiate_write(exec_ctx, transport_global, false,
                                 "rst_stream");
    }

    const char *msg =
        grpc_error_get_str(due_to_error, GRPC_ERROR_STR_GRPC_MESSAGE);
    bool free_msg = false;
    if (msg == NULL) {
      free_msg = true;
      msg = grpc_error_string(due_to_error);
    }
    gpr_slice msg_slice = gpr_slice_from_copied_string(msg);
    grpc_chttp2_fake_status(exec_ctx, transport_global, stream_global,
                            grpc_status, &msg_slice);
    if (free_msg) grpc_error_free_string(msg);
  }
  if (due_to_error != GRPC_ERROR_NONE && !stream_global->seen_error) {
    stream_global->seen_error = true;
    grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                        stream_global);
  }
  grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global, 1,
                                 1, due_to_error);
}

void grpc_chttp2_fake_status(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_global *transport_global,
                             grpc_chttp2_stream_global *stream_global,
                             grpc_status_code status, gpr_slice *slice) {
  if (status != GRPC_STATUS_OK) {
    stream_global->seen_error = true;
    grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                        stream_global);
  }
  /* stream_global->recv_trailing_metadata_finished gives us a
     last chance replacement: we've received trailing metadata,
     but something more important has become available to signal
     to the upper layers - drop what we've got, and then publish
     what we want - which is safe because we haven't told anyone
     about the metadata yet */
  if (!stream_global->published_trailing_metadata ||
      stream_global->recv_trailing_metadata_finished != NULL) {
    char status_string[GPR_LTOA_MIN_BUFSIZE];
    gpr_ltoa(status, status_string);
    grpc_chttp2_incoming_metadata_buffer_add(
        &stream_global->received_trailing_metadata,
        grpc_mdelem_from_metadata_strings(
            GRPC_MDSTR_GRPC_STATUS, grpc_mdstr_from_string(status_string)));
    if (slice) {
      grpc_chttp2_incoming_metadata_buffer_add(
          &stream_global->received_trailing_metadata,
          grpc_mdelem_from_metadata_strings(
              GRPC_MDSTR_GRPC_MESSAGE,
              grpc_mdstr_from_slice(gpr_slice_ref(*slice))));
    }
    stream_global->published_trailing_metadata = true;
    grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                        stream_global);
  }
  if (slice) {
    gpr_slice_unref(*slice);
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

static grpc_error *removal_error(grpc_error *extra_error,
                                 grpc_chttp2_stream_global *stream_global) {
  grpc_error *refs[3];
  size_t nrefs = 0;
  add_error(stream_global->read_closed_error, refs, &nrefs);
  add_error(stream_global->write_closed_error, refs, &nrefs);
  add_error(extra_error, refs, &nrefs);
  grpc_error *error = GRPC_ERROR_NONE;
  if (nrefs > 0) {
    error = GRPC_ERROR_CREATE_REFERENCING("Failed due to stream removal", refs,
                                          nrefs);
  }
  GRPC_ERROR_UNREF(extra_error);
  return error;
}

static void fail_pending_writes(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_transport_global *transport_global,
                                grpc_chttp2_stream_global *stream_global,
                                grpc_error *error) {
  error = removal_error(error, stream_global);
  stream_global->send_message = NULL;
  grpc_chttp2_complete_closure_step(
      exec_ctx, transport_global, stream_global,
      &stream_global->send_initial_metadata_finished, GRPC_ERROR_REF(error));
  grpc_chttp2_complete_closure_step(
      exec_ctx, transport_global, stream_global,
      &stream_global->send_trailing_metadata_finished, GRPC_ERROR_REF(error));
  grpc_chttp2_complete_closure_step(exec_ctx, transport_global, stream_global,
                                    &stream_global->send_message_finished,
                                    error);
}

void grpc_chttp2_mark_stream_closed(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, int close_reads, int close_writes,
    grpc_error *error) {
  if (stream_global->read_closed && stream_global->write_closed) {
    /* already closed */
    GRPC_ERROR_UNREF(error);
    return;
  }
  grpc_chttp2_list_add_check_read_ops(exec_ctx, transport_global,
                                      stream_global);
  if (close_reads && !stream_global->read_closed) {
    stream_global->read_closed_error = GRPC_ERROR_REF(error);
    stream_global->read_closed = true;
    stream_global->published_initial_metadata = true;
    stream_global->published_trailing_metadata = true;
    decrement_active_streams_locked(exec_ctx, transport_global, stream_global);
  }
  if (close_writes && !stream_global->write_closed) {
    stream_global->write_closed_error = GRPC_ERROR_REF(error);
    stream_global->write_closed = true;
    if (TRANSPORT_FROM_GLOBAL(transport_global)->executor.write_state !=
        GRPC_CHTTP2_WRITING_INACTIVE) {
      GRPC_CHTTP2_STREAM_REF(stream_global, "finish_writes");
      grpc_chttp2_list_add_closed_waiting_for_writing(transport_global,
                                                      stream_global);
    } else {
      fail_pending_writes(exec_ctx, transport_global, stream_global,
                          GRPC_ERROR_REF(error));
    }
  }
  if (stream_global->read_closed && stream_global->write_closed) {
    if (stream_global->id != 0 &&
        TRANSPORT_FROM_GLOBAL(transport_global)->executor.parsing_active) {
      grpc_chttp2_list_add_closed_waiting_for_parsing(transport_global,
                                                      stream_global);
    } else {
      if (stream_global->id != 0) {
        remove_stream(exec_ctx, TRANSPORT_FROM_GLOBAL(transport_global),
                      stream_global->id,
                      removal_error(GRPC_ERROR_REF(error), stream_global));
      }
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2");
    }
  }
  GRPC_ERROR_UNREF(error);
}

static void close_from_api(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global,
                           grpc_chttp2_stream_global *stream_global,
                           grpc_error *error) {
  gpr_slice hdr;
  gpr_slice status_hdr;
  gpr_slice message_pfx;
  uint8_t *p;
  uint32_t len = 0;
  grpc_status_code grpc_status;
  grpc_chttp2_error_code http_error;
  status_codes_from_error(error, stream_global->deadline, &http_error,
                          &grpc_status);

  GPR_ASSERT(grpc_status >= 0 && (int)grpc_status < 100);

  if (stream_global->id != 0 && !transport_global->is_client) {
    /* Hand roll a header block.
       This is unnecessarily ugly - at some point we should find a more elegant
       solution.
       It's complicated by the fact that our send machinery would be dead by the
       time we got around to sending this, so instead we ignore HPACK
       compression
       and just write the uncompressed bytes onto the wire. */
    status_hdr = gpr_slice_malloc(15 + (grpc_status >= 10));
    p = GPR_SLICE_START_PTR(status_hdr);
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
    GPR_ASSERT(p == GPR_SLICE_END_PTR(status_hdr));
    len += (uint32_t)GPR_SLICE_LENGTH(status_hdr);

    const char *optional_message =
        grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE);

    if (optional_message != NULL) {
      size_t msg_len = strlen(optional_message);
      GPR_ASSERT(msg_len < 127);
      message_pfx = gpr_slice_malloc(15);
      p = GPR_SLICE_START_PTR(message_pfx);
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
      *p++ = (uint8_t)msg_len;
      GPR_ASSERT(p == GPR_SLICE_END_PTR(message_pfx));
      len += (uint32_t)GPR_SLICE_LENGTH(message_pfx);
      len += (uint32_t)msg_len;
    }

    hdr = gpr_slice_malloc(9);
    p = GPR_SLICE_START_PTR(hdr);
    *p++ = (uint8_t)(len >> 16);
    *p++ = (uint8_t)(len >> 8);
    *p++ = (uint8_t)(len);
    *p++ = GRPC_CHTTP2_FRAME_HEADER;
    *p++ = GRPC_CHTTP2_DATA_FLAG_END_STREAM | GRPC_CHTTP2_DATA_FLAG_END_HEADERS;
    *p++ = (uint8_t)(stream_global->id >> 24);
    *p++ = (uint8_t)(stream_global->id >> 16);
    *p++ = (uint8_t)(stream_global->id >> 8);
    *p++ = (uint8_t)(stream_global->id);
    GPR_ASSERT(p == GPR_SLICE_END_PTR(hdr));

    gpr_slice_buffer_add(&transport_global->qbuf, hdr);
    gpr_slice_buffer_add(&transport_global->qbuf, status_hdr);
    if (optional_message) {
      gpr_slice_buffer_add(&transport_global->qbuf, message_pfx);
      gpr_slice_buffer_add(&transport_global->qbuf,
                           gpr_slice_from_copied_string(optional_message));
    }
    gpr_slice_buffer_add(
        &transport_global->qbuf,
        grpc_chttp2_rst_stream_create(stream_global->id, GRPC_CHTTP2_NO_ERROR,
                                      &stream_global->stats.outgoing));
  }

  const char *msg = grpc_error_get_str(error, GRPC_ERROR_STR_GRPC_MESSAGE);
  bool free_msg = false;
  if (msg == NULL) {
    free_msg = true;
    msg = grpc_error_string(error);
  }
  gpr_slice msg_slice = gpr_slice_from_copied_string(msg);
  grpc_chttp2_fake_status(exec_ctx, transport_global, stream_global,
                          grpc_status, &msg_slice);
  if (free_msg) grpc_error_free_string(msg);

  grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global, 1,
                                 1, error);
  grpc_chttp2_initiate_write(exec_ctx, transport_global, false,
                             "close_from_api");
}

typedef struct {
  grpc_exec_ctx *exec_ctx;
  grpc_error *error;
} cancel_stream_cb_args;

static void cancel_stream_cb(grpc_chttp2_transport_global *transport_global,
                             void *user_data,
                             grpc_chttp2_stream_global *stream_global) {
  cancel_stream_cb_args *args = user_data;
  cancel_from_api(args->exec_ctx, transport_global, stream_global,
                  GRPC_ERROR_REF(args->error));
}

static void end_all_the_calls(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                              grpc_error *error) {
  cancel_stream_cb_args args = {exec_ctx, error};
  grpc_chttp2_for_all_streams(&t->global, &args, cancel_stream_cb);
  GRPC_ERROR_UNREF(error);
}

static void drop_connection(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                            grpc_error *error) {
  if (!grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, NULL)) {
    error = grpc_error_set_int(error, GRPC_ERROR_INT_GRPC_STATUS,
                               GRPC_STATUS_UNAVAILABLE);
  }
  close_transport_locked(exec_ctx, t, GRPC_ERROR_REF(error));
  end_all_the_calls(exec_ctx, t, error);
}

/** update window from a settings change */
typedef struct {
  grpc_chttp2_transport *t;
  grpc_exec_ctx *exec_ctx;
} update_global_window_args;

static void update_global_window(void *args, uint32_t id, void *stream) {
  update_global_window_args *a = args;
  grpc_chttp2_transport *t = a->t;
  grpc_chttp2_stream *s = stream;
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global = &s->global;
  int was_zero;
  int is_zero;
  int64_t initial_window_update = t->parsing.initial_window_update;

  was_zero = stream_global->outgoing_window <= 0;
  GRPC_CHTTP2_FLOW_CREDIT_STREAM("settings", transport_global, stream_global,
                                 outgoing_window, initial_window_update);
  is_zero = stream_global->outgoing_window <= 0;

  if (was_zero && !is_zero) {
    grpc_chttp2_become_writable(a->exec_ctx, transport_global, stream_global,
                                true, "update_global_window");
  }
}

/*******************************************************************************
 * INPUT PROCESSING - PARSING
 */

static void parsing_action(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error);
static void post_reading_action_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                       grpc_error *error);
static void post_parse_locked(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_error *error);

static void reading_action(grpc_exec_ctx *exec_ctx, void *tp,
                           grpc_error *error) {
  /* Control flow:
     reading_action_locked ->
       (parse_unlocked -> post_parse_locked)? ->
       post_reading_action_locked */
  GPR_TIMER_BEGIN("reading_action", 0);
  grpc_chttp2_transport *t = tp;
  grpc_combiner_execute(exec_ctx, t->executor.combiner,
                        &t->reading_action_locked, GRPC_ERROR_REF(error));
  GPR_TIMER_END("reading_action", 0);
}

static void reading_action_locked(grpc_exec_ctx *exec_ctx, void *tp,
                                  grpc_error *error) {
  GPR_TIMER_BEGIN("reading_action_locked", 0);

  grpc_chttp2_transport *t = tp;
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_transport_parsing *transport_parsing = &t->parsing;

  GPR_ASSERT(!t->executor.parsing_active);
  if (!t->closed) {
    t->executor.parsing_active = 1;
    /* merge stream lists */
    grpc_chttp2_stream_map_move_into(&t->new_stream_map,
                                     &t->parsing_stream_map);
    grpc_chttp2_prepare_to_read(transport_global, transport_parsing);
    grpc_exec_ctx_sched(exec_ctx, &t->parsing_action, GRPC_ERROR_REF(error),
                        NULL);
  } else {
    post_reading_action_locked(exec_ctx, t, error);
  }

  GPR_TIMER_END("reading_action_locked", 0);
}

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
    parse_error = grpc_http_parser_parse(&parser, t->read_buffer.slices[i]);
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

static void parsing_action(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  grpc_chttp2_transport *t = arg;
  grpc_error *err = GRPC_ERROR_NONE;
  GPR_TIMER_BEGIN("reading_action.parse", 0);
  size_t i = 0;
  grpc_error *errors[3] = {GRPC_ERROR_REF(error), GRPC_ERROR_NONE,
                           GRPC_ERROR_NONE};
  for (; i < t->read_buffer.count && errors[1] == GRPC_ERROR_NONE; i++) {
    errors[1] = grpc_chttp2_perform_read(exec_ctx, &t->parsing,
                                         t->read_buffer.slices[i]);
  };
  if (errors[1] == GRPC_ERROR_NONE) {
    err = GRPC_ERROR_REF(error);
  } else {
    errors[2] = try_http_parsing(exec_ctx, t);
    err = GRPC_ERROR_CREATE_REFERENCING("Failed parsing HTTP/2", errors,
                                        GPR_ARRAY_SIZE(errors));
  }
  for (i = 0; i < GPR_ARRAY_SIZE(errors); i++) {
    GRPC_ERROR_UNREF(errors[i]);
  }
  grpc_combiner_execute(exec_ctx, t->executor.combiner, &t->post_parse_locked,
                        err);
  GPR_TIMER_END("reading_action.parse", 0);
}

static void post_parse_locked(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_error *error) {
  GPR_TIMER_BEGIN("post_parse_locked", 0);
  grpc_chttp2_transport *t = arg;
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_transport_parsing *transport_parsing = &t->parsing;
  /* copy parsing qbuf to global qbuf */
  if (t->parsing.qbuf.count > 0) {
    gpr_slice_buffer_move_into(&t->parsing.qbuf, &t->global.qbuf);
    grpc_chttp2_initiate_write(exec_ctx, transport_global, false,
                               "parsing_qbuf");
  }
  /* merge stream lists */
  grpc_chttp2_stream_map_move_into(&t->new_stream_map, &t->parsing_stream_map);
  transport_global->concurrent_stream_count =
      (uint32_t)grpc_chttp2_stream_map_size(&t->parsing_stream_map);
  if (transport_parsing->initial_window_update != 0) {
    update_global_window_args args = {t, exec_ctx};
    grpc_chttp2_stream_map_for_each(&t->parsing_stream_map,
                                    update_global_window, &args);
    transport_parsing->initial_window_update = 0;
  }
  /* handle higher level things */
  grpc_chttp2_publish_reads(exec_ctx, transport_global, transport_parsing);
  t->executor.parsing_active = 0;
  /* handle delayed transport ops (if there is one) */
  if (t->post_parsing_op) {
    grpc_transport_op *op = t->post_parsing_op;
    t->post_parsing_op = NULL;
    perform_transport_op_locked(exec_ctx, op, GRPC_ERROR_NONE);
    gpr_free(op);
  }
  /* if a stream is in the stream map, and gets cancelled, we need to
   * ensure we are not parsing before continuing the cancellation to keep
   * things in a sane state */
  grpc_chttp2_stream_global *stream_global;
  while (grpc_chttp2_list_pop_closed_waiting_for_parsing(transport_global,
                                                         &stream_global)) {
    GPR_ASSERT(stream_global->in_stream_map);
    GPR_ASSERT(stream_global->write_closed);
    GPR_ASSERT(stream_global->read_closed);
    remove_stream(exec_ctx, t, stream_global->id,
                  removal_error(GRPC_ERROR_NONE, stream_global));
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2");
  }

  post_reading_action_locked(exec_ctx, t, error);
  GPR_TIMER_END("post_parse_locked", 0);
}

static void post_reading_action_locked(grpc_exec_ctx *exec_ctx, void *arg,
                                       grpc_error *error) {
  GPR_TIMER_BEGIN("post_reading_action_locked", 0);
  grpc_chttp2_transport *t = arg;
  bool keep_reading = false;
  GRPC_ERROR_REF(error);
  if (error == GRPC_ERROR_NONE && t->closed) {
    error = GRPC_ERROR_CREATE("Transport closed");
  }
  if (error != GRPC_ERROR_NONE) {
    drop_connection(exec_ctx, t, GRPC_ERROR_REF(error));
    t->endpoint_reading = 0;
    if (grpc_http_write_state_trace) {
      gpr_log(GPR_DEBUG, "R:%p -> 0 ws=%s", t,
              write_state_name(t->executor.write_state));
    }
  } else if (!t->closed) {
    keep_reading = true;
    REF_TRANSPORT(t, "keep_reading");
    prevent_endpoint_shutdown(t);
  }
  gpr_slice_buffer_reset_and_unref(&t->read_buffer);

  if (keep_reading) {
    grpc_endpoint_read(exec_ctx, t->ep, &t->read_buffer, &t->reading_action);
    allow_endpoint_shutdown_locked(exec_ctx, t);
    UNREF_TRANSPORT(exec_ctx, t, "keep_reading");
  } else {
    UNREF_TRANSPORT(exec_ctx, t, "reading_action");
  }
  GRPC_ERROR_UNREF(error);

  GPR_TIMER_END("post_reading_action_locked", 0);
}

/*******************************************************************************
 * CALLBACK LOOP
 */

static void connectivity_state_set(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_connectivity_state state, grpc_error *error, const char *reason) {
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_DEBUG, "set connectivity_state=%d", state));
  grpc_connectivity_state_set(
      exec_ctx,
      &TRANSPORT_FROM_GLOBAL(transport_global)->channel_callback.state_tracker,
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
    gpr_slice_buffer_destroy(&bs->slices);
    gpr_mu_destroy(&bs->slice_mu);
    gpr_free(bs);
  }
}

static void incoming_byte_stream_update_flow_control(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, size_t max_size_hint,
    size_t have_already) {
  uint32_t max_recv_bytes;

  /* clamp max recv hint to an allowable size */
  if (max_size_hint >= UINT32_MAX - transport_global->stream_lookahead) {
    max_recv_bytes = UINT32_MAX - transport_global->stream_lookahead;
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
  GPR_ASSERT(max_recv_bytes <= UINT32_MAX - transport_global->stream_lookahead);
  max_recv_bytes += transport_global->stream_lookahead;
  if (stream_global->max_recv_bytes < max_recv_bytes) {
    uint32_t add_max_recv_bytes =
        max_recv_bytes - stream_global->max_recv_bytes;
    GRPC_CHTTP2_FLOW_CREDIT_STREAM("op", transport_global, stream_global,
                                   max_recv_bytes, add_max_recv_bytes);
    GRPC_CHTTP2_FLOW_CREDIT_STREAM("op", transport_global, stream_global,
                                   unannounced_incoming_window_for_parse,
                                   add_max_recv_bytes);
    GRPC_CHTTP2_FLOW_CREDIT_STREAM("op", transport_global, stream_global,
                                   unannounced_incoming_window_for_writing,
                                   add_max_recv_bytes);
    grpc_chttp2_list_add_unannounced_incoming_window_available(transport_global,
                                                               stream_global);
    grpc_chttp2_become_writable(exec_ctx, transport_global, stream_global,
                                false, "read_incoming_stream");
  }
}

static void incoming_byte_stream_next_locked(grpc_exec_ctx *exec_ctx,
                                             void *argp,
                                             grpc_error *error_ignored) {
  grpc_chttp2_incoming_byte_stream *bs = argp;
  grpc_chttp2_transport_global *transport_global = &bs->transport->global;
  grpc_chttp2_stream_global *stream_global = &bs->stream->global;

  if (bs->is_tail) {
    gpr_mu_lock(&bs->slice_mu);
    size_t cur_length = bs->slices.length;
    gpr_mu_unlock(&bs->slice_mu);
    incoming_byte_stream_update_flow_control(
        exec_ctx, transport_global, stream_global,
        bs->next_action.max_size_hint, cur_length);
  }
  gpr_mu_lock(&bs->slice_mu);
  if (bs->slices.count > 0) {
    *bs->next_action.slice = gpr_slice_buffer_take_first(&bs->slices);
    grpc_exec_ctx_sched(exec_ctx, bs->next_action.on_complete, GRPC_ERROR_NONE,
                        NULL);
  } else if (bs->error != GRPC_ERROR_NONE) {
    grpc_exec_ctx_sched(exec_ctx, bs->next_action.on_complete,
                        GRPC_ERROR_REF(bs->error), NULL);
  } else {
    bs->on_next = bs->next_action.on_complete;
    bs->next = bs->next_action.slice;
  }
  gpr_mu_unlock(&bs->slice_mu);
  incoming_byte_stream_unref(exec_ctx, bs);
}

static int incoming_byte_stream_next(grpc_exec_ctx *exec_ctx,
                                     grpc_byte_stream *byte_stream,
                                     gpr_slice *slice, size_t max_size_hint,
                                     grpc_closure *on_complete) {
  GPR_TIMER_BEGIN("incoming_byte_stream_next", 0);
  grpc_chttp2_incoming_byte_stream *bs =
      (grpc_chttp2_incoming_byte_stream *)byte_stream;
  gpr_ref(&bs->refs);
  bs->next_action.slice = slice;
  bs->next_action.max_size_hint = max_size_hint;
  bs->next_action.on_complete = on_complete;
  grpc_closure_init(&bs->next_action.closure, incoming_byte_stream_next_locked,
                    bs);
  grpc_combiner_execute(exec_ctx, bs->transport->executor.combiner,
                        &bs->next_action.closure, GRPC_ERROR_NONE);
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
  decrement_active_streams_locked(exec_ctx, &bs->transport->global,
                                  &bs->stream->global);
  incoming_byte_stream_unref(exec_ctx, bs);
}

static void incoming_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                                         grpc_byte_stream *byte_stream) {
  GPR_TIMER_BEGIN("incoming_byte_stream_destroy", 0);
  grpc_chttp2_incoming_byte_stream *bs =
      (grpc_chttp2_incoming_byte_stream *)byte_stream;
  grpc_closure_init(&bs->destroy_action, incoming_byte_stream_destroy_locked,
                    bs);
  grpc_combiner_execute(exec_ctx, bs->transport->executor.combiner,
                        &bs->destroy_action, GRPC_ERROR_NONE);
  GPR_TIMER_END("incoming_byte_stream_destroy", 0);
}

typedef struct {
  grpc_chttp2_incoming_byte_stream *byte_stream;
  gpr_slice slice;
} incoming_byte_stream_push_arg;

void grpc_chttp2_incoming_byte_stream_push(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_incoming_byte_stream *bs,
                                           gpr_slice slice) {
  gpr_mu_lock(&bs->slice_mu);
  if (bs->on_next != NULL) {
    *bs->next = slice;
    grpc_exec_ctx_sched(exec_ctx, bs->on_next, GRPC_ERROR_NONE, NULL);
    bs->on_next = NULL;
  } else {
    gpr_slice_buffer_add(&bs->slices, slice);
  }
  gpr_mu_unlock(&bs->slice_mu);
}

static void incoming_byte_stream_finished_locked(grpc_exec_ctx *exec_ctx,
                                                 void *bsp, grpc_error *error) {
  grpc_chttp2_incoming_byte_stream *bs = bsp;
  if (error != GRPC_ERROR_NONE) {
    grpc_exec_ctx_sched(exec_ctx, bs->on_next, GRPC_ERROR_REF(error), NULL);
    bs->on_next = NULL;
    GRPC_ERROR_UNREF(bs->error);
    bs->error = error;
  }
  incoming_byte_stream_unref(exec_ctx, bs);
}

void grpc_chttp2_incoming_byte_stream_finished(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_byte_stream *bs,
    grpc_error *error, int from_parsing_thread) {
  GPR_TIMER_BEGIN("grpc_chttp2_incoming_byte_stream_finished", 0);
  if (from_parsing_thread) {
    grpc_closure_init(&bs->finished_action,
                      incoming_byte_stream_finished_locked, bs);
    grpc_combiner_execute(exec_ctx, bs->transport->executor.combiner,
                          &bs->finished_action, GRPC_ERROR_REF(error));
  } else {
    incoming_byte_stream_finished_locked(exec_ctx, bs, error);
  }
  GPR_TIMER_END("grpc_chttp2_incoming_byte_stream_finished", 0);
}

grpc_chttp2_incoming_byte_stream *grpc_chttp2_incoming_byte_stream_create(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, uint32_t frame_size,
    uint32_t flags, grpc_chttp2_incoming_frame_queue *add_to_queue) {
  grpc_chttp2_incoming_byte_stream *incoming_byte_stream =
      gpr_malloc(sizeof(*incoming_byte_stream));
  incoming_byte_stream->base.length = frame_size;
  incoming_byte_stream->base.flags = flags;
  incoming_byte_stream->base.next = incoming_byte_stream_next;
  incoming_byte_stream->base.destroy = incoming_byte_stream_destroy;
  gpr_mu_init(&incoming_byte_stream->slice_mu);
  gpr_ref_init(&incoming_byte_stream->refs, 2);
  incoming_byte_stream->next_message = NULL;
  incoming_byte_stream->transport = TRANSPORT_FROM_PARSING(transport_parsing);
  incoming_byte_stream->stream = STREAM_FROM_PARSING(stream_parsing);
  gpr_ref(&incoming_byte_stream->stream->global.active_streams);
  gpr_slice_buffer_init(&incoming_byte_stream->slices);
  incoming_byte_stream->on_next = NULL;
  incoming_byte_stream->is_tail = 1;
  incoming_byte_stream->error = GRPC_ERROR_NONE;
  if (add_to_queue->head == NULL) {
    add_to_queue->head = incoming_byte_stream;
  } else {
    add_to_queue->tail->is_tail = 0;
    add_to_queue->tail->next_message = incoming_byte_stream;
  }
  add_to_queue->tail = incoming_byte_stream;
  return incoming_byte_stream;
}

/*******************************************************************************
 * TRACING
 */

static char *format_flowctl_context_var(const char *context, const char *var,
                                        int64_t val, uint32_t id,
                                        char **scope) {
  char *underscore_pos;
  char *buf;
  char *result;
  if (context == NULL) {
    *scope = NULL;
    gpr_asprintf(&buf, "%s(%" PRId64 ")", var, val);
    result = gpr_leftpad(buf, ' ', 60);
    gpr_free(buf);
    return result;
  }
  underscore_pos = strchr(context, '_');
  *scope = gpr_strdup(context);
  (*scope)[underscore_pos - context] = 0;
  if (id != 0) {
    char *tmp = *scope;
    gpr_asprintf(scope, "%s[%d]", tmp, id);
    gpr_free(tmp);
  }
  gpr_asprintf(&buf, "%s.%s(%" PRId64 ")", underscore_pos + 1, var, val);
  result = gpr_leftpad(buf, ' ', 60);
  gpr_free(buf);
  return result;
}

static int samestr(char *a, char *b) {
  if (a == NULL) {
    return b == NULL;
  }
  if (b == NULL) {
    return 0;
  }
  return 0 == strcmp(a, b);
}

void grpc_chttp2_flowctl_trace(const char *file, int line, const char *phase,
                               grpc_chttp2_flowctl_op op, const char *context1,
                               const char *var1, const char *context2,
                               const char *var2, int is_client,
                               uint32_t stream_id, int64_t val1, int64_t val2) {
  char *scope1;
  char *scope2;
  char *tmp_phase;
  char *tmp_scope1;
  char *label1 =
      format_flowctl_context_var(context1, var1, val1, stream_id, &scope1);
  char *label2 =
      format_flowctl_context_var(context2, var2, val2, stream_id, &scope2);
  char *clisvr = is_client ? "client" : "server";
  char *prefix;

  tmp_phase = gpr_leftpad(phase, ' ', 8);
  tmp_scope1 = gpr_leftpad(scope1, ' ', 11);
  gpr_asprintf(&prefix, "FLOW %s: %s %s ", tmp_phase, clisvr, scope1);
  gpr_free(tmp_phase);
  gpr_free(tmp_scope1);

  switch (op) {
    case GRPC_CHTTP2_FLOWCTL_MOVE:
      GPR_ASSERT(samestr(scope1, scope2));
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

  gpr_free(scope1);
  gpr_free(scope2);
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

static const grpc_transport_vtable vtable = {sizeof(grpc_chttp2_stream),
                                             "chttp2",
                                             init_stream,
                                             set_pollset,
                                             set_pollset_set,
                                             perform_stream_op,
                                             perform_transport_op,
                                             destroy_stream,
                                             destroy_transport,
                                             chttp2_get_peer};

grpc_transport *grpc_create_chttp2_transport(
    grpc_exec_ctx *exec_ctx, const grpc_channel_args *channel_args,
    grpc_endpoint *ep, int is_client) {
  grpc_chttp2_transport *t = gpr_malloc(sizeof(grpc_chttp2_transport));
  init_transport(exec_ctx, t, channel_args, ep, is_client != 0);
  return &t->base;
}

void grpc_chttp2_transport_start_reading(grpc_exec_ctx *exec_ctx,
                                         grpc_transport *transport,
                                         gpr_slice_buffer *read_buffer) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)transport;
  REF_TRANSPORT(t, "reading_action"); /* matches unref inside reading_action */
  if (read_buffer != NULL) {
    gpr_slice_buffer_move_into(read_buffer, &t->read_buffer);
    gpr_free(read_buffer);
  }
  reading_action(exec_ctx, t, GRPC_ERROR_NONE);
}
