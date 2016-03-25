/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include "src/core/transport/chttp2_transport.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/profiling/timers.h"
#include "src/core/support/string.h"
#include "src/core/transport/chttp2/http2_errors.h"
#include "src/core/transport/chttp2/internal.h"
#include "src/core/transport/chttp2/status_conversion.h"
#include "src/core/transport/chttp2/timeout_encoding.h"
#include "src/core/transport/static_metadata.h"
#include "src/core/transport/transport_impl.h"

#define DEFAULT_WINDOW 65535
#define DEFAULT_CONNECTION_WINDOW_TARGET (1024 * 1024)
#define MAX_WINDOW 0x7fffffffu

#define MAX_CLIENT_STREAM_ID 0x7fffffffu

int grpc_http_trace = 0;
int grpc_flowctl_trace = 0;

#define TRANSPORT_FROM_WRITING(tw)                                        \
  ((grpc_chttp2_transport *)((char *)(tw)-offsetof(grpc_chttp2_transport, \
                                                   writing)))

#define TRANSPORT_FROM_PARSING(tw)                                        \
  ((grpc_chttp2_transport *)((char *)(tw)-offsetof(grpc_chttp2_transport, \
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
static void writing_action(grpc_exec_ctx *exec_ctx, void *t,
                           bool iomgr_success_ignored);
static void reading_action(grpc_exec_ctx *exec_ctx, void *t,
                           bool iomgr_success_ignored);
static void parsing_action(grpc_exec_ctx *exec_ctx, void *t,
                           bool iomgr_success_ignored);

/** Set a transport level setting, and push it to our peer */
static void push_setting(grpc_chttp2_transport *t, grpc_chttp2_setting_id id,
                         uint32_t value);

/** Start disconnection chain */
static void drop_connection(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t);

/** Perform a transport_op */
static void perform_stream_op_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_chttp2_stream *s, void *transport_op);

/** Cancel a stream: coming from the transport API */
static void cancel_from_api(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_global *transport_global,
                            grpc_chttp2_stream_global *stream_global,
                            grpc_status_code status);

static void close_from_api(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global,
                           grpc_chttp2_stream_global *stream_global,
                           grpc_status_code status,
                           gpr_slice *optional_message);

/** Add endpoint from this transport to pollset */
static void add_to_pollset_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t,
                                  grpc_chttp2_stream *s_ignored, void *pollset);
static void add_to_pollset_set_locked(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t,
                                      grpc_chttp2_stream *s_ignored,
                                      void *pollset_set);

/** Start new streams that have been created if we can */
static void maybe_start_some_streams(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);

static void finish_global_actions(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t);

static void connectivity_state_set(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_connectivity_state state, const char *reason);

static void check_read_ops(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global);

static void incoming_byte_stream_update_flow_control(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, size_t max_size_hint,
    size_t have_already);
static void incoming_byte_stream_destroy_locked(grpc_exec_ctx *exec_ctx,
                                                grpc_chttp2_transport *t,
                                                grpc_chttp2_stream *s,
                                                void *byte_stream);
static void fail_pending_writes(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_stream_global *stream_global);

/*******************************************************************************
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

static void destruct_transport(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport *t) {
  size_t i;

  gpr_mu_lock(&t->executor.mu);

  GPR_ASSERT(t->ep == NULL);

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

  gpr_mu_unlock(&t->executor.mu);
  gpr_mu_destroy(&t->executor.mu);

  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  while (t->global.pings.next != &t->global.pings) {
    grpc_chttp2_outstanding_ping *ping = t->global.pings.next;
    grpc_exec_ctx_enqueue(exec_ctx, ping->on_recv, false, NULL);
    ping->next->prev = ping->prev;
    ping->prev->next = ping->next;
    gpr_free(ping);
  }

  gpr_free(t->peer_string);
  gpr_free(t);
}

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
                           grpc_endpoint *ep, uint8_t is_client) {
  size_t i;
  int j;

  GPR_ASSERT(strlen(GRPC_CHTTP2_CLIENT_CONNECT_STRING) ==
             GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);

  memset(t, 0, sizeof(*t));

  t->base.vtable = &vtable;
  t->ep = ep;
  /* one ref is for destroy, the other for when ep becomes NULL */
  gpr_ref_init(&t->refs, 2);
  /* ref is dropped at transport close() */
  gpr_ref_init(&t->shutdown_ep_refs, 1);
  gpr_mu_init(&t->executor.mu);
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
  t->writing.is_client = is_client;
  grpc_connectivity_state_init(
      &t->channel_callback.state_tracker, GRPC_CHANNEL_READY,
      is_client ? "client_transport" : "server_transport");

  gpr_slice_buffer_init(&t->global.qbuf);

  gpr_slice_buffer_init(&t->writing.outbuf);
  grpc_chttp2_hpack_compressor_init(&t->writing.hpack_compressor);
  grpc_closure_init(&t->writing_action, writing_action, t);
  grpc_closure_init(&t->reading_action, reading_action, t);
  grpc_closure_init(&t->parsing_action, parsing_action, t);

  gpr_slice_buffer_init(&t->parsing.qbuf);
  grpc_chttp2_goaway_parser_init(&t->parsing.goaway_parser);
  grpc_chttp2_hpack_parser_init(&t->parsing.hpack_parser);

  grpc_closure_init(&t->writing.done_cb, grpc_chttp2_terminate_writing,
                    &t->writing);
  gpr_slice_buffer_init(&t->read_buffer);

  if (is_client) {
    gpr_slice_buffer_add(
        &t->global.qbuf,
        gpr_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING));
  }
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

  /* configure http2 the way we like it */
  if (is_client) {
    push_setting(t, GRPC_CHTTP2_SETTINGS_ENABLE_PUSH, 0);
    push_setting(t, GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 0);
  }
  push_setting(t, GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, DEFAULT_WINDOW);

  if (channel_args) {
    for (i = 0; i < channel_args->num_args; i++) {
      if (0 ==
          strcmp(channel_args->args[i].key, GRPC_ARG_MAX_CONCURRENT_STREAMS)) {
        if (is_client) {
          gpr_log(GPR_ERROR, "%s: is ignored on the client",
                  GRPC_ARG_MAX_CONCURRENT_STREAMS);
        } else if (channel_args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s: must be an integer",
                  GRPC_ARG_MAX_CONCURRENT_STREAMS);
        } else {
          push_setting(t, GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                       (uint32_t)channel_args->args[i].value.integer);
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER)) {
        if (channel_args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s: must be an integer",
                  GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER);
        } else if ((t->global.next_stream_id & 1) !=
                   (channel_args->args[i].value.integer & 1)) {
          gpr_log(GPR_ERROR, "%s: low bit must be %d on %s",
                  GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER,
                  t->global.next_stream_id & 1,
                  is_client ? "client" : "server");
        } else {
          t->global.next_stream_id =
              (uint32_t)channel_args->args[i].value.integer;
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES)) {
        if (channel_args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s: must be an integer",
                  GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES);
        } else if (channel_args->args[i].value.integer <= 5) {
          gpr_log(GPR_ERROR, "%s: must be at least 5",
                  GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES);
        } else {
          t->global.stream_lookahead =
              (uint32_t)channel_args->args[i].value.integer;
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER)) {
        if (channel_args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s: must be an integer",
                  GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER);
        } else if (channel_args->args[i].value.integer < 0) {
          gpr_log(GPR_ERROR, "%s: must be non-negative",
                  GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER);
        } else {
          push_setting(t, GRPC_CHTTP2_SETTINGS_HEADER_TABLE_SIZE,
                       (uint32_t)channel_args->args[i].value.integer);
        }
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER)) {
        if (channel_args->args[i].type != GRPC_ARG_INTEGER) {
          gpr_log(GPR_ERROR, "%s: must be an integer",
                  GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER);
        } else if (channel_args->args[i].value.integer < 0) {
          gpr_log(GPR_ERROR, "%s: must be non-negative",
                  GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER);
        } else {
          grpc_chttp2_hpack_compressor_set_max_usable_size(
              &t->writing.hpack_compressor,
              (uint32_t)channel_args->args[i].value.integer);
        }
      }
    }
  }
}

static void destroy_transport_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_chttp2_stream *s_ignored,
                                     void *arg_ignored) {
  t->destroying = 1;
  drop_connection(exec_ctx, t);
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_run_with_global_lock(exec_ctx, t, NULL, destroy_transport_locked,
                                   NULL, 0);
  UNREF_TRANSPORT(exec_ctx, t, "destroy");
}

/** block grpc_endpoint_shutdown being called until a paired
    allow_endpoint_shutdown is made */
static void prevent_endpoint_shutdown(grpc_chttp2_transport *t) {
  GPR_ASSERT(t->ep);
  gpr_ref(&t->shutdown_ep_refs);
}

static void allow_endpoint_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_transport *t) {
  if (gpr_unref(&t->shutdown_ep_refs)) {
    if (t->ep) {
      grpc_endpoint_shutdown(exec_ctx, t->ep);
    }
  }
}

static void destroy_endpoint(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport *t) {
  grpc_endpoint_destroy(exec_ctx, t->ep);
  t->ep = NULL;
  /* safe because we'll still have the ref for write */
  UNREF_TRANSPORT(exec_ctx, t, "disconnect");
}

static void close_transport_locked(grpc_exec_ctx *exec_ctx,
                                   grpc_chttp2_transport *t,
                                   grpc_chttp2_stream *s_ignored,
                                   void *arg_ignored) {
  if (!t->closed) {
    t->closed = 1;
    connectivity_state_set(exec_ctx, &t->global, GRPC_CHANNEL_FATAL_FAILURE,
                           "close_transport");
    if (t->ep) {
      allow_endpoint_shutdown_locked(exec_ctx, t);
    }

    /* flush writable stream list to avoid dangling references */
    grpc_chttp2_stream_global *stream_global;
    grpc_chttp2_stream_writing *stream_writing;
    while (grpc_chttp2_list_pop_writable_stream(
        &t->global, &t->writing, &stream_global, &stream_writing)) {
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2_writing");
    }
  }
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

static void finish_init_stream_locked(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t,
                                      grpc_chttp2_stream *s,
                                      void *arg_ignored) {
  grpc_chttp2_register_stream(t, s);
}

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, grpc_stream_refcount *refcount,
                       const void *server_data) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  memset(s, 0, sizeof(*s));

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
    s->global.in_stream_map = 1;
  }

  grpc_chttp2_run_with_global_lock(exec_ctx, t, s, finish_init_stream_locked,
                                   NULL, 0);

  return 0;
}

static void destroy_stream_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t,
                                  grpc_chttp2_stream *s, void *arg) {
  grpc_byte_stream *bs;

  GPR_TIMER_BEGIN("destroy_stream", 0);

  GPR_ASSERT((s->global.write_closed && s->global.read_closed) ||
             s->global.id == 0);
  GPR_ASSERT(!s->global.in_stream_map);
  if (grpc_chttp2_unregister_stream(t, s) && t->global.sent_goaway) {
    close_transport_locked(exec_ctx, t, NULL, NULL);
  }
  if (!t->executor.parsing_active && s->global.id) {
    GPR_ASSERT(grpc_chttp2_stream_map_find(&t->parsing_stream_map,
                                           s->global.id) == NULL);
  }

  while (
      (bs = grpc_chttp2_incoming_frame_queue_pop(&s->global.incoming_frames))) {
    incoming_byte_stream_destroy_locked(exec_ctx, NULL, NULL, bs);
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

  UNREF_TRANSPORT(exec_ctx, t, "stream");

  GPR_TIMER_END("destroy_stream", 0);

  gpr_free(arg);
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs, void *and_free_memory) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  grpc_chttp2_run_with_global_lock(exec_ctx, t, s, destroy_stream_locked,
                                   and_free_memory, 0);
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

static void finish_global_actions(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t) {
  grpc_chttp2_executor_action_header *hdr;
  grpc_chttp2_executor_action_header *next;

  for (;;) {
    if (!t->executor.writing_active && !t->closed &&
        grpc_chttp2_unlocking_check_writes(exec_ctx, &t->global, &t->writing,
                                           t->executor.parsing_active)) {
      t->executor.writing_active = 1;
      REF_TRANSPORT(t, "writing");
      prevent_endpoint_shutdown(t);
      grpc_exec_ctx_enqueue(exec_ctx, &t->writing_action, true, NULL);
    }
    check_read_ops(exec_ctx, &t->global);

    gpr_mu_lock(&t->executor.mu);
    if (t->executor.pending_actions != NULL) {
      hdr = t->executor.pending_actions;
      t->executor.pending_actions = NULL;
      gpr_mu_unlock(&t->executor.mu);
      while (hdr != NULL) {
        hdr->action(exec_ctx, t, hdr->stream, hdr->arg);
        next = hdr->next;
        gpr_free(hdr);
        UNREF_TRANSPORT(exec_ctx, t, "pending_action");
        hdr = next;
      }
      continue;
    } else {
      t->executor.global_active = false;
    }
    gpr_mu_unlock(&t->executor.mu);
    break;
  }
}

void grpc_chttp2_run_with_global_lock(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t,
                                      grpc_chttp2_stream *optional_stream,
                                      grpc_chttp2_locked_action action,
                                      void *arg, size_t sizeof_arg) {
  grpc_chttp2_executor_action_header *hdr;

  REF_TRANSPORT(t, "run_global");
  gpr_mu_lock(&t->executor.mu);

  for (;;) {
    if (!t->executor.global_active) {
      t->executor.global_active = 1;
      gpr_mu_unlock(&t->executor.mu);

      action(exec_ctx, t, optional_stream, arg);

      finish_global_actions(exec_ctx, t);
    } else {
      gpr_mu_unlock(&t->executor.mu);

      hdr = gpr_malloc(sizeof(*hdr) + sizeof_arg);
      hdr->stream = optional_stream;
      hdr->action = action;
      if (sizeof_arg == 0) {
        hdr->arg = arg;
      } else {
        hdr->arg = hdr + 1;
        memcpy(hdr->arg, arg, sizeof_arg);
      }

      gpr_mu_lock(&t->executor.mu);
      if (!t->executor.global_active) {
        /* global lock was released while allocating memory: release & retry */
        gpr_free(hdr);
        continue;
      }
      hdr->next = t->executor.pending_actions;
      t->executor.pending_actions = hdr;
      REF_TRANSPORT(t, "pending_action");
      gpr_mu_unlock(&t->executor.mu);
    }
    break;
  }

  UNREF_TRANSPORT(exec_ctx, t, "run_global");
}

/*******************************************************************************
 * OUTPUT PROCESSING
 */

void grpc_chttp2_become_writable(grpc_chttp2_transport_global *transport_global,
                                 grpc_chttp2_stream_global *stream_global) {
  if (!TRANSPORT_FROM_GLOBAL(transport_global)->closed &&
      grpc_chttp2_list_add_writable_stream(transport_global, stream_global)) {
    GRPC_CHTTP2_STREAM_REF(stream_global, "chttp2_writing");
  }
}

static void push_setting(grpc_chttp2_transport *t, grpc_chttp2_setting_id id,
                         uint32_t value) {
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
  }
}

static void terminate_writing_with_lock(grpc_exec_ctx *exec_ctx,
                                        grpc_chttp2_transport *t,
                                        grpc_chttp2_stream *s_ignored,
                                        void *a) {
  bool success = (bool)(uintptr_t)a;

  allow_endpoint_shutdown_locked(exec_ctx, t);

  if (!success) {
    drop_connection(exec_ctx, t);
  }

  grpc_chttp2_cleanup_writing(exec_ctx, &t->global, &t->writing);

  grpc_chttp2_stream_global *stream_global;
  while (grpc_chttp2_list_pop_closed_waiting_for_writing(&t->global,
                                                         &stream_global)) {
    fail_pending_writes(exec_ctx, stream_global);
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "finish_writes");
  }

  /* leave the writing flag up on shutdown to prevent further writes in
     unlock()
     from starting */
  t->executor.writing_active = 0;
  if (t->ep && !t->endpoint_reading) {
    destroy_endpoint(exec_ctx, t);
  }

  UNREF_TRANSPORT(exec_ctx, t, "writing");
}

void grpc_chttp2_terminate_writing(grpc_exec_ctx *exec_ctx,
                                   void *transport_writing, bool success) {
  grpc_chttp2_transport *t = TRANSPORT_FROM_WRITING(transport_writing);
  grpc_chttp2_run_with_global_lock(exec_ctx, t, NULL,
                                   terminate_writing_with_lock,
                                   (void *)(uintptr_t)success, 0);
}

static void writing_action(grpc_exec_ctx *exec_ctx, void *gt,
                           bool iomgr_success_ignored) {
  grpc_chttp2_transport *t = gt;
  GPR_TIMER_BEGIN("writing_action", 0);
  grpc_chttp2_perform_writes(exec_ctx, &t->writing, t->ep);
  GPR_TIMER_END("writing_action", 0);
}

void grpc_chttp2_add_incoming_goaway(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    uint32_t goaway_error, gpr_slice goaway_text) {
  char *msg = gpr_dump_slice(goaway_text, GPR_DUMP_HEX | GPR_DUMP_ASCII);
  gpr_log(GPR_DEBUG, "got goaway [%d]: %s", goaway_error, msg);
  gpr_free(msg);
  gpr_slice_unref(goaway_text);
  transport_global->seen_goaway = 1;
  connectivity_state_set(exec_ctx, transport_global, GRPC_CHANNEL_FATAL_FAILURE,
                         "got_goaway");
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
      connectivity_state_set(exec_ctx, transport_global,
                             GRPC_CHANNEL_TRANSIENT_FAILURE,
                             "no_more_stream_ids");
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
    stream_global->in_stream_map = 1;
    transport_global->concurrent_stream_count++;
    grpc_chttp2_become_writable(transport_global, stream_global);
  }
  /* cancel out streams that will never be started */
  while (transport_global->next_stream_id >= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_list_pop_waiting_for_concurrency(transport_global,
                                                      &stream_global)) {
    cancel_from_api(exec_ctx, transport_global, stream_global,
                    GRPC_STATUS_UNAVAILABLE);
  }
}

#define CLOSURE_BARRIER_STATS_BIT (1 << 0)
#define CLOSURE_BARRIER_FAILURE_BIT (1 << 1)
#define CLOSURE_BARRIER_FIRST_REF_BIT (1 << 16)

static grpc_closure *add_closure_barrier(grpc_closure *closure) {
  closure->final_data += CLOSURE_BARRIER_FIRST_REF_BIT;
  return closure;
}

void grpc_chttp2_complete_closure_step(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_stream_global *stream_global,
                                       grpc_closure **pclosure, int success) {
  grpc_closure *closure = *pclosure;
  if (closure == NULL) {
    return;
  }
  closure->final_data -= CLOSURE_BARRIER_FIRST_REF_BIT;
  if (!success) {
    closure->final_data |= CLOSURE_BARRIER_FAILURE_BIT;
  }
  if (closure->final_data < CLOSURE_BARRIER_FIRST_REF_BIT) {
    if (closure->final_data & CLOSURE_BARRIER_STATS_BIT) {
      grpc_transport_move_stats(&stream_global->stats,
                                stream_global->collecting_stats);
      stream_global->collecting_stats = NULL;
    }
    grpc_exec_ctx_enqueue(
        exec_ctx, closure,
        (closure->final_data & CLOSURE_BARRIER_FAILURE_BIT) == 0, NULL);
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

static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, bool success) {}

static void perform_stream_op_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_chttp2_stream *s, void *stream_op) {
  GPR_TIMER_BEGIN("perform_stream_op_locked", 0);

  grpc_transport_stream_op *op = stream_op;
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global = &s->global;

  grpc_closure *on_complete = op->on_complete;
  if (on_complete == NULL) {
    on_complete = grpc_closure_create(do_nothing, NULL);
  }
  /* use final_data as a barrier until enqueue time; the inital counter is
     dropped at the end of this function */
  on_complete->final_data = CLOSURE_BARRIER_FIRST_REF_BIT;

  if (op->collect_stats != NULL) {
    GPR_ASSERT(stream_global->collecting_stats == NULL);
    stream_global->collecting_stats = op->collect_stats;
    on_complete->final_data |= CLOSURE_BARRIER_STATS_BIT;
  }

  if (op->cancel_with_status != GRPC_STATUS_OK) {
    cancel_from_api(exec_ctx, transport_global, stream_global,
                    op->cancel_with_status);
  }

  if (op->close_with_status != GRPC_STATUS_OK) {
    close_from_api(exec_ctx, transport_global, stream_global,
                   op->close_with_status, op->optional_close_message);
  }

  if (op->send_initial_metadata != NULL) {
    GPR_ASSERT(stream_global->send_initial_metadata_finished == NULL);
    stream_global->send_initial_metadata_finished =
        add_closure_barrier(on_complete);
    stream_global->send_initial_metadata = op->send_initial_metadata;
    if (contains_non_ok_status(transport_global, op->send_initial_metadata)) {
      stream_global->seen_error = 1;
      grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
    }
    if (!stream_global->write_closed) {
      if (transport_global->is_client) {
        GPR_ASSERT(stream_global->id == 0);
        grpc_chttp2_list_add_waiting_for_concurrency(transport_global,
                                                     stream_global);
        maybe_start_some_streams(exec_ctx, transport_global);
      } else {
        GPR_ASSERT(stream_global->id != 0);
        grpc_chttp2_become_writable(transport_global, stream_global);
      }
    } else {
      grpc_chttp2_complete_closure_step(
          exec_ctx, stream_global,
          &stream_global->send_initial_metadata_finished, 0);
    }
  }

  if (op->send_message != NULL) {
    GPR_ASSERT(stream_global->send_message_finished == NULL);
    GPR_ASSERT(stream_global->send_message == NULL);
    stream_global->send_message_finished = add_closure_barrier(on_complete);
    if (stream_global->write_closed) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, stream_global, &stream_global->send_message_finished, 0);
    } else if (stream_global->id != 0) {
      stream_global->send_message = op->send_message;
      if (stream_global->id != 0) {
        grpc_chttp2_become_writable(transport_global, stream_global);
      }
    }
  }

  if (op->send_trailing_metadata != NULL) {
    GPR_ASSERT(stream_global->send_trailing_metadata_finished == NULL);
    stream_global->send_trailing_metadata_finished =
        add_closure_barrier(on_complete);
    stream_global->send_trailing_metadata = op->send_trailing_metadata;
    if (contains_non_ok_status(transport_global, op->send_trailing_metadata)) {
      stream_global->seen_error = 1;
      grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
    }
    if (stream_global->write_closed) {
      grpc_chttp2_complete_closure_step(
          exec_ctx, stream_global,
          &stream_global->send_trailing_metadata_finished,
          grpc_metadata_batch_is_empty(op->send_trailing_metadata));
    } else if (stream_global->id != 0) {
      /* TODO(ctiller): check if there's flow control for any outstanding
         bytes before going writable */
      grpc_chttp2_become_writable(transport_global, stream_global);
    }
  }

  if (op->recv_initial_metadata != NULL) {
    GPR_ASSERT(stream_global->recv_initial_metadata_ready == NULL);
    stream_global->recv_initial_metadata_ready =
        op->recv_initial_metadata_ready;
    stream_global->recv_initial_metadata = op->recv_initial_metadata;
    grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
  }

  if (op->recv_message != NULL) {
    GPR_ASSERT(stream_global->recv_message_ready == NULL);
    stream_global->recv_message_ready = op->recv_message_ready;
    stream_global->recv_message = op->recv_message;
    if (stream_global->id != 0 &&
        (stream_global->incoming_frames.head == NULL ||
         stream_global->incoming_frames.head->is_tail)) {
      incoming_byte_stream_update_flow_control(
          transport_global, stream_global, transport_global->stream_lookahead,
          0);
    }
    grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
  }

  if (op->recv_trailing_metadata != NULL) {
    GPR_ASSERT(stream_global->recv_trailing_metadata_finished == NULL);
    stream_global->recv_trailing_metadata_finished =
        add_closure_barrier(on_complete);
    stream_global->recv_trailing_metadata = op->recv_trailing_metadata;
    grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
  }

  grpc_chttp2_complete_closure_step(exec_ctx, stream_global, &on_complete, 1);

  GPR_TIMER_END("perform_stream_op_locked", 0);
}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs, grpc_transport_stream_op *op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;
  grpc_chttp2_run_with_global_lock(exec_ctx, t, s, perform_stream_op_locked, op,
                                   sizeof(*op));
}

static void send_ping_locked(grpc_chttp2_transport *t, grpc_closure *on_recv) {
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
}

static void ack_ping_locked(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                            grpc_chttp2_stream *s, void *opaque_8bytes) {
  grpc_chttp2_outstanding_ping *ping;
  grpc_chttp2_transport_global *transport_global = &t->global;
  for (ping = transport_global->pings.next; ping != &transport_global->pings;
       ping = ping->next) {
    if (0 == memcmp(opaque_8bytes, ping->id, 8)) {
      grpc_exec_ctx_enqueue(exec_ctx, ping->on_recv, true, NULL);
      ping->next->prev = ping->prev;
      ping->prev->next = ping->next;
      gpr_free(ping);
      break;
    }
  }
}

void grpc_chttp2_ack_ping(grpc_exec_ctx *exec_ctx,
                          grpc_chttp2_transport_parsing *transport_parsing,
                          const uint8_t *opaque_8bytes) {
  grpc_chttp2_run_with_global_lock(
      exec_ctx, TRANSPORT_FROM_PARSING(transport_parsing), NULL,
      ack_ping_locked, (void *)opaque_8bytes, 8);
}

static void perform_transport_op_locked(grpc_exec_ctx *exec_ctx,
                                        grpc_chttp2_transport *t,
                                        grpc_chttp2_stream *s_unused,
                                        void *stream_op) {
  grpc_transport_op *op = stream_op;
  bool close_transport = op->disconnect;

  /* If there's a set_accept_stream ensure that we're not parsing
     to avoid changing things out from underneath */
  if (t->executor.parsing_active && op->set_accept_stream) {
    GPR_ASSERT(t->post_parsing_op == NULL);
    t->post_parsing_op = gpr_malloc(sizeof(*op));
    memcpy(t->post_parsing_op, op, sizeof(*op));
  }

  grpc_exec_ctx_enqueue(exec_ctx, op->on_consumed, true, NULL);

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
    close_transport = !grpc_chttp2_has_streams(t);
  }

  if (op->set_accept_stream) {
    t->channel_callback.accept_stream = op->set_accept_stream_fn;
    t->channel_callback.accept_stream_user_data =
        op->set_accept_stream_user_data;
  }

  if (op->bind_pollset) {
    add_to_pollset_locked(exec_ctx, t, NULL, op->bind_pollset);
  }

  if (op->bind_pollset_set) {
    add_to_pollset_set_locked(exec_ctx, t, NULL, op->bind_pollset_set);
  }

  if (op->send_ping) {
    send_ping_locked(t, op->send_ping);
  }

  if (close_transport) {
    close_transport_locked(exec_ctx, t, NULL, NULL);
  }
}

static void perform_transport_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                 grpc_transport_op *op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_run_with_global_lock(
      exec_ctx, t, NULL, perform_transport_op_locked, op, sizeof(*op));
}

/*******************************************************************************
 * INPUT PROCESSING - GENERAL
 */

static void check_read_ops(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global) {
  grpc_chttp2_stream_global *stream_global;
  grpc_byte_stream *bs;
  while (
      grpc_chttp2_list_pop_check_read_ops(transport_global, &stream_global)) {
    if (stream_global->recv_initial_metadata_ready != NULL &&
        stream_global->published_initial_metadata) {
      grpc_chttp2_incoming_metadata_buffer_publish(
          &stream_global->received_initial_metadata,
          stream_global->recv_initial_metadata);
      grpc_exec_ctx_enqueue(
          exec_ctx, stream_global->recv_initial_metadata_ready, true, NULL);
      stream_global->recv_initial_metadata_ready = NULL;
    }
    if (stream_global->recv_message_ready != NULL) {
      while (stream_global->seen_error &&
             (bs = grpc_chttp2_incoming_frame_queue_pop(
                  &stream_global->incoming_frames)) != NULL) {
        incoming_byte_stream_destroy_locked(exec_ctx, NULL, NULL, bs);
      }
      if (stream_global->incoming_frames.head != NULL) {
        *stream_global->recv_message = grpc_chttp2_incoming_frame_queue_pop(
            &stream_global->incoming_frames);
        GPR_ASSERT(*stream_global->recv_message != NULL);
        grpc_exec_ctx_enqueue(exec_ctx, stream_global->recv_message_ready, true,
                              NULL);
        stream_global->recv_message_ready = NULL;
      } else if (stream_global->published_trailing_metadata) {
        *stream_global->recv_message = NULL;
        grpc_exec_ctx_enqueue(exec_ctx, stream_global->recv_message_ready, true,
                              NULL);
        stream_global->recv_message_ready = NULL;
      }
    }
    if (stream_global->recv_trailing_metadata_finished != NULL &&
        stream_global->read_closed && stream_global->write_closed) {
      while (stream_global->seen_error &&
             (bs = grpc_chttp2_incoming_frame_queue_pop(
                  &stream_global->incoming_frames)) != NULL) {
        incoming_byte_stream_destroy_locked(exec_ctx, NULL, NULL, bs);
      }
      if (stream_global->all_incoming_byte_streams_finished) {
        grpc_chttp2_incoming_metadata_buffer_publish(
            &stream_global->received_trailing_metadata,
            stream_global->recv_trailing_metadata);
        grpc_chttp2_complete_closure_step(
            exec_ctx, stream_global,
            &stream_global->recv_trailing_metadata_finished, 1);
      }
    }
  }
}

static void decrement_active_streams_locked(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) {
  if ((stream_global->all_incoming_byte_streams_finished =
           gpr_unref(&stream_global->active_streams))) {
    grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
  }
}

static void remove_stream(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                          uint32_t id) {
  size_t new_stream_count;
  grpc_chttp2_stream *s =
      grpc_chttp2_stream_map_delete(&t->parsing_stream_map, id);
  if (!s) {
    s = grpc_chttp2_stream_map_delete(&t->new_stream_map, id);
  }
  GPR_ASSERT(s);
  s->global.in_stream_map = 0;
  if (t->parsing.incoming_stream == &s->parsing) {
    t->parsing.incoming_stream = NULL;
    grpc_chttp2_parsing_become_skip_parser(exec_ctx, &t->parsing);
  }
  if (s->parsing.data_parser.parsing_frame != NULL) {
    grpc_chttp2_incoming_byte_stream_finished(
        exec_ctx, s->parsing.data_parser.parsing_frame, 0, 0);
    s->parsing.data_parser.parsing_frame = NULL;
  }

  if (grpc_chttp2_unregister_stream(t, s) && t->global.sent_goaway) {
    close_transport_locked(exec_ctx, t, NULL, NULL);
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
}

static void cancel_from_api(grpc_exec_ctx *exec_ctx,
                            grpc_chttp2_transport_global *transport_global,
                            grpc_chttp2_stream_global *stream_global,
                            grpc_status_code status) {
  if (stream_global->id != 0) {
    gpr_slice_buffer_add(
        &transport_global->qbuf,
        grpc_chttp2_rst_stream_create(
            stream_global->id,
            (uint32_t)grpc_chttp2_grpc_status_to_http2_error(status),
            &stream_global->stats.outgoing));
  }
  grpc_chttp2_fake_status(exec_ctx, transport_global, stream_global, status,
                          NULL);
  grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global, 1,
                                 1);
}

void grpc_chttp2_fake_status(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_global *transport_global,
                             grpc_chttp2_stream_global *stream_global,
                             grpc_status_code status, gpr_slice *slice) {
  if (status != GRPC_STATUS_OK) {
    stream_global->seen_error = 1;
    grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
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
    stream_global->published_trailing_metadata = 1;
    grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
  }
  if (slice) {
    gpr_slice_unref(*slice);
  }
}

static void fail_pending_writes(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_stream_global *stream_global) {
  grpc_chttp2_complete_closure_step(
      exec_ctx, stream_global, &stream_global->send_initial_metadata_finished,
      0);
  grpc_chttp2_complete_closure_step(
      exec_ctx, stream_global, &stream_global->send_trailing_metadata_finished,
      0);
  grpc_chttp2_complete_closure_step(exec_ctx, stream_global,
                                    &stream_global->send_message_finished, 0);
}

void grpc_chttp2_mark_stream_closed(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, int close_reads,
    int close_writes) {
  if (stream_global->read_closed && stream_global->write_closed) {
    /* already closed */
    return;
  }
  grpc_chttp2_list_add_check_read_ops(transport_global, stream_global);
  if (close_reads && !stream_global->read_closed) {
    stream_global->read_closed = 1;
    stream_global->published_initial_metadata = 1;
    stream_global->published_trailing_metadata = 1;
    decrement_active_streams_locked(exec_ctx, transport_global, stream_global);
  }
  if (close_writes && !stream_global->write_closed) {
    stream_global->write_closed = 1;
    if (TRANSPORT_FROM_GLOBAL(transport_global)->executor.writing_active) {
      GRPC_CHTTP2_STREAM_REF(stream_global, "finish_writes");
      grpc_chttp2_list_add_closed_waiting_for_writing(transport_global,
                                                      stream_global);
    } else {
      fail_pending_writes(exec_ctx, stream_global);
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
                      stream_global->id);
      }
      GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2");
    }
  }
}

static void close_from_api(grpc_exec_ctx *exec_ctx,
                           grpc_chttp2_transport_global *transport_global,
                           grpc_chttp2_stream_global *stream_global,
                           grpc_status_code status,
                           gpr_slice *optional_message) {
  gpr_slice hdr;
  gpr_slice status_hdr;
  gpr_slice message_pfx;
  uint8_t *p;
  uint32_t len = 0;

  GPR_ASSERT(status >= 0 && (int)status < 100);

  GPR_ASSERT(stream_global->id != 0);

  /* Hand roll a header block.
     This is unnecessarily ugly - at some point we should find a more elegant
     solution.
     It's complicated by the fact that our send machinery would be dead by the
     time we got around to sending this, so instead we ignore HPACK compression
     and just write the uncompressed bytes onto the wire. */
  status_hdr = gpr_slice_malloc(15 + (status >= 10));
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
  if (status < 10) {
    *p++ = 1;
    *p++ = (uint8_t)('0' + status);
  } else {
    *p++ = 2;
    *p++ = (uint8_t)('0' + (status / 10));
    *p++ = (uint8_t)('0' + (status % 10));
  }
  GPR_ASSERT(p == GPR_SLICE_END_PTR(status_hdr));
  len += (uint32_t)GPR_SLICE_LENGTH(status_hdr);

  if (optional_message) {
    GPR_ASSERT(GPR_SLICE_LENGTH(*optional_message) < 127);
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
    *p++ = (uint8_t)GPR_SLICE_LENGTH(*optional_message);
    GPR_ASSERT(p == GPR_SLICE_END_PTR(message_pfx));
    len += (uint32_t)GPR_SLICE_LENGTH(message_pfx);
    len += (uint32_t)GPR_SLICE_LENGTH(*optional_message);
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
                         gpr_slice_ref(*optional_message));
  }

  gpr_slice_buffer_add(
      &transport_global->qbuf,
      grpc_chttp2_rst_stream_create(stream_global->id, GRPC_CHTTP2_NO_ERROR,
                                    &stream_global->stats.outgoing));

  if (optional_message) {
    gpr_slice_ref(*optional_message);
  }
  grpc_chttp2_fake_status(exec_ctx, transport_global, stream_global, status,
                          optional_message);
  grpc_chttp2_mark_stream_closed(exec_ctx, transport_global, stream_global, 1,
                                 1);
}

static void cancel_stream_cb(grpc_chttp2_transport_global *transport_global,
                             void *user_data,
                             grpc_chttp2_stream_global *stream_global) {
  cancel_from_api(user_data, transport_global, stream_global,
                  GRPC_STATUS_UNAVAILABLE);
}

static void end_all_the_calls(grpc_exec_ctx *exec_ctx,
                              grpc_chttp2_transport *t) {
  grpc_chttp2_for_all_streams(&t->global, exec_ctx, cancel_stream_cb);
}

static void drop_connection(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t) {
  close_transport_locked(exec_ctx, t, NULL, NULL);
  end_all_the_calls(exec_ctx, t);
}

/** update window from a settings change */
static void update_global_window(void *args, uint32_t id, void *stream) {
  grpc_chttp2_transport *t = args;
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
    grpc_chttp2_become_writable(transport_global, stream_global);
  }
}

/*******************************************************************************
 * INPUT PROCESSING - PARSING
 */

static void reading_action_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t,
                                  grpc_chttp2_stream *s_unused, void *arg);
static void parsing_action(grpc_exec_ctx *exec_ctx, void *arg, bool success);
static void post_reading_action_locked(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_transport *t,
                                       grpc_chttp2_stream *s_unused, void *arg);
static void post_parse_locked(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                              grpc_chttp2_stream *s_unused, void *arg);

static void reading_action(grpc_exec_ctx *exec_ctx, void *tp, bool success) {
  /* Control flow:
     reading_action_locked ->
       (parse_unlocked -> post_parse_locked)? ->
       post_reading_action_locked */
  grpc_chttp2_run_with_global_lock(exec_ctx, tp, NULL, reading_action_locked,
                                   (void *)(uintptr_t)success, 0);
}

static void reading_action_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t,
                                  grpc_chttp2_stream *s_unused, void *arg) {
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_transport_parsing *transport_parsing = &t->parsing;
  bool success = (bool)(uintptr_t)arg;

  GPR_ASSERT(!t->executor.parsing_active);
  if (!t->closed) {
    t->executor.parsing_active = 1;
    /* merge stream lists */
    grpc_chttp2_stream_map_move_into(&t->new_stream_map,
                                     &t->parsing_stream_map);
    grpc_chttp2_prepare_to_read(transport_global, transport_parsing);
    grpc_exec_ctx_enqueue(exec_ctx, &t->parsing_action, success, NULL);
  } else {
    post_reading_action_locked(exec_ctx, t, s_unused, arg);
  }
}

static void parsing_action(grpc_exec_ctx *exec_ctx, void *arg, bool success) {
  grpc_chttp2_transport *t = arg;
  GPR_TIMER_BEGIN("reading_action.parse", 0);
  size_t i = 0;
  for (; i < t->read_buffer.count &&
         grpc_chttp2_perform_read(exec_ctx, &t->parsing,
                                  t->read_buffer.slices[i]);
       i++)
    ;
  if (i != t->read_buffer.count) {
    success = false;
  }
  GPR_TIMER_END("reading_action.parse", 0);
  grpc_chttp2_run_with_global_lock(exec_ctx, t, NULL, post_parse_locked,
                                   (void *)(uintptr_t)success, 0);
}

static void post_parse_locked(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                              grpc_chttp2_stream *s_unused, void *arg) {
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_transport_parsing *transport_parsing = &t->parsing;
  /* copy parsing qbuf to global qbuf */
  gpr_slice_buffer_move_into(&t->parsing.qbuf, &t->global.qbuf);
  /* merge stream lists */
  grpc_chttp2_stream_map_move_into(&t->new_stream_map, &t->parsing_stream_map);
  transport_global->concurrent_stream_count =
      (uint32_t)grpc_chttp2_stream_map_size(&t->parsing_stream_map);
  if (transport_parsing->initial_window_update != 0) {
    grpc_chttp2_stream_map_for_each(&t->parsing_stream_map,
                                    update_global_window, t);
    transport_parsing->initial_window_update = 0;
  }
  /* handle higher level things */
  grpc_chttp2_publish_reads(exec_ctx, transport_global, transport_parsing);
  t->executor.parsing_active = 0;
  /* handle delayed transport ops (if there is one) */
  if (t->post_parsing_op) {
    grpc_transport_op *op = t->post_parsing_op;
    t->post_parsing_op = NULL;
    perform_transport_op_locked(exec_ctx, t, NULL, op);
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
    remove_stream(exec_ctx, t, stream_global->id);
    GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, "chttp2");
  }

  post_reading_action_locked(exec_ctx, t, s_unused, arg);
}

static void post_reading_action_locked(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_transport *t,
                                       grpc_chttp2_stream *s_unused,
                                       void *arg) {
  bool success = (bool)(uintptr_t)arg;
  bool keep_reading = false;
  if (!success || t->closed) {
    drop_connection(exec_ctx, t);
    t->endpoint_reading = 0;
    if (!t->executor.writing_active && t->ep) {
      grpc_endpoint_destroy(exec_ctx, t->ep);
      t->ep = NULL;
      /* safe as we still have a ref for read */
      UNREF_TRANSPORT(exec_ctx, t, "disconnect");
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
}

/*******************************************************************************
 * CALLBACK LOOP
 */

static void connectivity_state_set(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_connectivity_state state, const char *reason) {
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_DEBUG, "set connectivity_state=%d", state));
  grpc_connectivity_state_set(
      exec_ctx,
      &TRANSPORT_FROM_GLOBAL(transport_global)->channel_callback.state_tracker,
      state, reason);
}

/*******************************************************************************
 * POLLSET STUFF
 */

static void add_to_pollset_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t,
                                  grpc_chttp2_stream *s_unused, void *pollset) {
  if (t->ep) {
    grpc_endpoint_add_to_pollset(exec_ctx, t->ep, pollset);
  }
}

static void add_to_pollset_set_locked(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t,
                                      grpc_chttp2_stream *s_unused,
                                      void *pollset_set) {
  if (t->ep) {
    grpc_endpoint_add_to_pollset_set(exec_ctx, t->ep, pollset_set);
  }
}

static void set_pollset(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                        grpc_stream *gs, grpc_pollset *pollset) {
  /* TODO(ctiller): keep pollset alive */
  grpc_chttp2_run_with_global_lock(exec_ctx, (grpc_chttp2_transport *)gt,
                                   (grpc_chttp2_stream *)gs,
                                   add_to_pollset_locked, pollset, 0);
}

/*******************************************************************************
 * BYTE STREAM
 */

static void incoming_byte_stream_update_flow_control(
    grpc_chttp2_transport_global *transport_global,
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
    grpc_chttp2_become_writable(transport_global, stream_global);
  }
}

typedef struct {
  grpc_chttp2_incoming_byte_stream *byte_stream;
  gpr_slice *slice;
  size_t max_size_hint;
  grpc_closure *on_complete;
} incoming_byte_stream_next_arg;

static void incoming_byte_stream_next_locked(grpc_exec_ctx *exec_ctx,
                                             grpc_chttp2_transport *t,
                                             grpc_chttp2_stream *s,
                                             void *argp) {
  incoming_byte_stream_next_arg *arg = argp;
  grpc_chttp2_incoming_byte_stream *bs =
      (grpc_chttp2_incoming_byte_stream *)arg->byte_stream;
  grpc_chttp2_transport_global *transport_global = &bs->transport->global;
  grpc_chttp2_stream_global *stream_global = &bs->stream->global;

  if (bs->is_tail) {
    incoming_byte_stream_update_flow_control(
        transport_global, stream_global, arg->max_size_hint, bs->slices.length);
  }
  if (bs->slices.count > 0) {
    *arg->slice = gpr_slice_buffer_take_first(&bs->slices);
    grpc_exec_ctx_enqueue(exec_ctx, arg->on_complete, true, NULL);
  } else if (bs->failed) {
    grpc_exec_ctx_enqueue(exec_ctx, arg->on_complete, false, NULL);
  } else {
    bs->on_next = arg->on_complete;
    bs->next = arg->slice;
  }
}

static int incoming_byte_stream_next(grpc_exec_ctx *exec_ctx,
                                     grpc_byte_stream *byte_stream,
                                     gpr_slice *slice, size_t max_size_hint,
                                     grpc_closure *on_complete) {
  grpc_chttp2_incoming_byte_stream *bs =
      (grpc_chttp2_incoming_byte_stream *)byte_stream;
  incoming_byte_stream_next_arg arg = {bs, slice, max_size_hint, on_complete};
  grpc_chttp2_run_with_global_lock(exec_ctx, bs->transport, bs->stream,
                                   incoming_byte_stream_next_locked, &arg,
                                   sizeof(arg));
  return 0;
}

static void incoming_byte_stream_unref(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_incoming_byte_stream *bs) {
  if (gpr_unref(&bs->refs)) {
    gpr_slice_buffer_destroy(&bs->slices);
    gpr_free(bs);
  }
}

static void incoming_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                                         grpc_byte_stream *byte_stream);

static void incoming_byte_stream_destroy_locked(grpc_exec_ctx *exec_ctx,
                                                grpc_chttp2_transport *t,
                                                grpc_chttp2_stream *s,
                                                void *byte_stream) {
  grpc_chttp2_incoming_byte_stream *bs = byte_stream;
  GPR_ASSERT(bs->base.destroy == incoming_byte_stream_destroy);
  decrement_active_streams_locked(exec_ctx, &bs->transport->global,
                                  &bs->stream->global);
  incoming_byte_stream_unref(exec_ctx, bs);
}

static void incoming_byte_stream_destroy(grpc_exec_ctx *exec_ctx,
                                         grpc_byte_stream *byte_stream) {
  grpc_chttp2_incoming_byte_stream *bs =
      (grpc_chttp2_incoming_byte_stream *)byte_stream;
  grpc_chttp2_run_with_global_lock(exec_ctx, bs->transport, bs->stream,
                                   incoming_byte_stream_destroy_locked, bs, 0);
}

typedef struct {
  grpc_chttp2_incoming_byte_stream *byte_stream;
  gpr_slice slice;
} incoming_byte_stream_push_arg;

static void incoming_byte_stream_push_locked(grpc_exec_ctx *exec_ctx,
                                             grpc_chttp2_transport *t,
                                             grpc_chttp2_stream *s,
                                             void *argp) {
  incoming_byte_stream_push_arg *arg = argp;
  grpc_chttp2_incoming_byte_stream *bs = arg->byte_stream;
  if (bs->on_next != NULL) {
    *bs->next = arg->slice;
    grpc_exec_ctx_enqueue(exec_ctx, bs->on_next, true, NULL);
    bs->on_next = NULL;
  } else {
    gpr_slice_buffer_add(&bs->slices, arg->slice);
  }
}

void grpc_chttp2_incoming_byte_stream_push(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_incoming_byte_stream *bs,
                                           gpr_slice slice) {
  incoming_byte_stream_push_arg arg = {bs, slice};
  grpc_chttp2_run_with_global_lock(exec_ctx, bs->transport, bs->stream,
                                   incoming_byte_stream_push_locked, &arg,
                                   sizeof(arg));
}

static void incoming_byte_stream_finished_failed_locked(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t, grpc_chttp2_stream *s,
    void *argp) {
  grpc_chttp2_incoming_byte_stream *bs = argp;
  grpc_exec_ctx_enqueue(exec_ctx, bs->on_next, false, NULL);
  bs->on_next = NULL;
  bs->failed = 1;
  incoming_byte_stream_unref(exec_ctx, bs);
}

static void incoming_byte_stream_finished_ok_locked(grpc_exec_ctx *exec_ctx,
                                                    grpc_chttp2_transport *t,
                                                    grpc_chttp2_stream *s,
                                                    void *argp) {
  grpc_chttp2_incoming_byte_stream *bs = argp;
  incoming_byte_stream_unref(exec_ctx, bs);
}

void grpc_chttp2_incoming_byte_stream_finished(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_byte_stream *bs, int success,
    int from_parsing_thread) {
  if (from_parsing_thread) {
    if (success) {
      grpc_chttp2_run_with_global_lock(exec_ctx, bs->transport, bs->stream,
                                       incoming_byte_stream_finished_ok_locked,
                                       bs, 0);
    } else {
      incoming_byte_stream_finished_ok_locked(exec_ctx, bs->transport,
                                              bs->stream, bs);
    }
  } else {
    if (success) {
      grpc_chttp2_run_with_global_lock(
          exec_ctx, bs->transport, bs->stream,
          incoming_byte_stream_finished_failed_locked, bs, 0);
    } else {
      incoming_byte_stream_finished_failed_locked(exec_ctx, bs->transport,
                                                  bs->stream, bs);
    }
  }
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
  gpr_ref_init(&incoming_byte_stream->refs, 2);
  incoming_byte_stream->next_message = NULL;
  incoming_byte_stream->transport = TRANSPORT_FROM_PARSING(transport_parsing);
  incoming_byte_stream->stream = STREAM_FROM_PARSING(stream_parsing);
  gpr_ref(&incoming_byte_stream->stream->global.active_streams);
  gpr_slice_buffer_init(&incoming_byte_stream->slices);
  incoming_byte_stream->on_next = NULL;
  incoming_byte_stream->is_tail = 1;
  incoming_byte_stream->failed = 0;
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
  char *result;
  if (context == NULL) {
    *scope = NULL;
    gpr_asprintf(&result, "%s(%lld)", var, val);
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
  gpr_asprintf(&result, "%s.%s(%lld)", underscore_pos + 1, var, val);
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
  char *label1 =
      format_flowctl_context_var(context1, var1, val1, stream_id, &scope1);
  char *label2 =
      format_flowctl_context_var(context2, var2, val2, stream_id, &scope2);
  char *clisvr = is_client ? "client" : "server";
  char *prefix;

  gpr_asprintf(&prefix, "FLOW % 8s: %s % 11s ", phase, clisvr, scope1);

  switch (op) {
    case GRPC_CHTTP2_FLOWCTL_MOVE:
      GPR_ASSERT(samestr(scope1, scope2));
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sMOVE   % 40s <- % 40s giving %d", prefix, label1, label2,
                val1 + val2);
      }
      break;
    case GRPC_CHTTP2_FLOWCTL_CREDIT:
      GPR_ASSERT(val2 >= 0);
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sCREDIT % 40s by % 40s giving %d", prefix, label1, label2,
                val1 + val2);
      }
      break;
    case GRPC_CHTTP2_FLOWCTL_DEBIT:
      GPR_ASSERT(val2 >= 0);
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sDEBIT  % 40s by % 40s giving %d", prefix, label1, label2,
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
                                         gpr_slice *slices, size_t nslices) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)transport;
  REF_TRANSPORT(t, "reading_action"); /* matches unref inside reading_action */
  gpr_slice_buffer_addn(&t->read_buffer, slices, nslices);
  reading_action(exec_ctx, t, 1);
}
