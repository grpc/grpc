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

static const grpc_transport_vtable vtable;

static void lock(grpc_chttp2_transport *t);
static void unlock(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t);

static void unlock_check_read_write_state(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t);

/* forward declarations of various callbacks that we'll build closures around */
static void writing_action(grpc_exec_ctx *exec_ctx, void *t,
                           int iomgr_success_ignored);

/** Set a transport level setting, and push it to our peer */
static void push_setting(grpc_chttp2_transport *t, grpc_chttp2_setting_id id,
                         gpr_uint32 value);

/** Endpoint callback to process incoming data */
static void recv_data(grpc_exec_ctx *exec_ctx, void *tp, int success);

/** Start disconnection chain */
static void drop_connection(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t);

/** Perform a transport_op */
static void perform_stream_op_locked(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, grpc_transport_stream_op *op);

/** Cancel a stream: coming from the transport API */
static void cancel_from_api(grpc_chttp2_transport_global *transport_global,
                            grpc_chttp2_stream_global *stream_global,
                            grpc_status_code status);

static void close_from_api(grpc_chttp2_transport_global *transport_global,
                           grpc_chttp2_stream_global *stream_global,
                           grpc_status_code status,
                           gpr_slice *optional_message);

/** Add endpoint from this transport to pollset */
static void add_to_pollset_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t,
                                  grpc_pollset *pollset);
static void add_to_pollset_set_locked(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t,
                                      grpc_pollset_set *pollset_set);

/** Start new streams that have been created if we can */
static void maybe_start_some_streams(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global);

static void connectivity_state_set(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_connectivity_state state, const char *reason);

/*
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

static void destruct_transport(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport *t) {
  size_t i;

  gpr_mu_lock(&t->mu);

  GPR_ASSERT(t->ep == NULL);

  gpr_slice_buffer_destroy(&t->global.qbuf);

  gpr_slice_buffer_destroy(&t->writing.outbuf);
  grpc_chttp2_hpack_compressor_destroy(&t->writing.hpack_compressor);

  gpr_slice_buffer_destroy(&t->parsing.qbuf);
  gpr_slice_buffer_destroy(&t->read_buffer);
  grpc_chttp2_hpack_parser_destroy(&t->parsing.hpack_parser);
  grpc_chttp2_goaway_parser_destroy(&t->parsing.goaway_parser);

  GRPC_MDSTR_UNREF(t->parsing.str_grpc_timeout);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(t->lists[i].head == NULL);
    GPR_ASSERT(t->lists[i].tail == NULL);
  }

  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->parsing_stream_map) == 0);
  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->new_stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&t->parsing_stream_map);
  grpc_chttp2_stream_map_destroy(&t->new_stream_map);
  grpc_connectivity_state_destroy(exec_ctx, &t->channel_callback.state_tracker);

  gpr_mu_unlock(&t->mu);
  gpr_mu_destroy(&t->mu);

  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  while (t->global.pings.next != &t->global.pings) {
    grpc_chttp2_outstanding_ping *ping = t->global.pings.next;
    grpc_exec_ctx_enqueue(exec_ctx, ping->on_recv, 0);
    ping->next->prev = ping->prev;
    ping->prev->next = ping->next;
    gpr_free(ping);
  }

  grpc_mdctx_unref(t->metadata_context);

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
                           grpc_endpoint *ep, grpc_mdctx *mdctx,
                           gpr_uint8 is_client) {
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
  gpr_mu_init(&t->mu);
  grpc_mdctx_ref(mdctx);
  t->peer_string = grpc_endpoint_get_peer(ep);
  t->metadata_context = mdctx;
  t->endpoint_reading = 1;
  t->global.next_stream_id = is_client ? 1 : 2;
  t->global.is_client = is_client;
  t->global.outgoing_window = DEFAULT_WINDOW;
  t->global.incoming_window = DEFAULT_WINDOW;
  t->global.connection_window_target = DEFAULT_CONNECTION_WINDOW_TARGET;
  t->global.ping_counter = 1;
  t->global.pings.next = t->global.pings.prev = &t->global.pings;
  t->parsing.is_client = is_client;
  t->parsing.str_grpc_timeout =
      grpc_mdstr_from_string(t->metadata_context, "grpc-timeout");
  t->parsing.deframe_state =
      is_client ? GRPC_DTS_FH_0 : GRPC_DTS_CLIENT_PREFIX_0;
  t->writing.is_client = is_client;
  grpc_connectivity_state_init(
      &t->channel_callback.state_tracker, GRPC_CHANNEL_READY,
      is_client ? "client_transport" : "server_transport");

  gpr_slice_buffer_init(&t->global.qbuf);

  gpr_slice_buffer_init(&t->writing.outbuf);
  grpc_chttp2_hpack_compressor_init(&t->writing.hpack_compressor, mdctx);
  grpc_closure_init(&t->writing_action, writing_action, t);

  gpr_slice_buffer_init(&t->parsing.qbuf);
  grpc_chttp2_goaway_parser_init(&t->parsing.goaway_parser);
  grpc_chttp2_hpack_parser_init(&t->parsing.hpack_parser, t->metadata_context);

  grpc_closure_init(&t->writing.done_cb, grpc_chttp2_terminate_writing,
                    &t->writing);
  grpc_closure_init(&t->recv_data, recv_data, t);
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
                       (gpr_uint32)channel_args->args[i].value.integer);
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
              (gpr_uint32)channel_args->args[i].value.integer;
        }
      }
    }
  }
}

static void destroy_transport(grpc_exec_ctx *exec_ctx, grpc_transport *gt) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;

  lock(t);
  t->destroying = 1;
  drop_connection(exec_ctx, t);
  unlock(exec_ctx, t);

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

static void allow_endpoint_shutdown_unlocked(grpc_exec_ctx *exec_ctx,
                                             grpc_chttp2_transport *t) {
  if (gpr_unref(&t->shutdown_ep_refs)) {
    gpr_mu_lock(&t->mu);
    if (t->ep) {
      grpc_endpoint_shutdown(exec_ctx, t->ep);
    }
    gpr_mu_unlock(&t->mu);
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
                                   grpc_chttp2_transport *t) {
  if (!t->closed) {
    t->closed = 1;
    connectivity_state_set(exec_ctx, &t->global, GRPC_CHANNEL_FATAL_FAILURE,
                           "close_transport");
    if (t->ep) {
      allow_endpoint_shutdown_locked(exec_ctx, t);
    }
  }
}

static int init_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                       grpc_stream *gs, const void *server_data,
                       grpc_transport_stream_op *initial_op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  memset(s, 0, sizeof(*s));

  grpc_chttp2_incoming_metadata_buffer_init(&s->parsing.incoming_metadata);
  grpc_chttp2_incoming_metadata_buffer_init(&s->global.incoming_metadata);
  grpc_sopb_init(&s->writing.sopb);
  grpc_sopb_init(&s->global.incoming_sopb);
  grpc_chttp2_data_parser_init(&s->parsing.data_parser);

  REF_TRANSPORT(t, "stream");

  lock(t);
  grpc_chttp2_register_stream(t, s);
  if (server_data) {
    GPR_ASSERT(t->parsing_active);
    s->global.id = (gpr_uint32)(gpr_uintptr)server_data;
    s->global.outgoing_window =
        t->global.settings[GRPC_PEER_SETTINGS]
                          [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    s->global.max_recv_bytes = s->parsing.incoming_window =
        s->global.incoming_window =
            t->global.settings[GRPC_SENT_SETTINGS]
                              [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    *t->accepting_stream = s;
    grpc_chttp2_stream_map_add(&t->parsing_stream_map, s->global.id, s);
    s->global.in_stream_map = 1;
  }

  if (initial_op)
    perform_stream_op_locked(exec_ctx, &t->global, &s->global, initial_op);
  unlock(exec_ctx, t);

  return 0;
}

static void destroy_stream(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                           grpc_stream *gs) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;
  int i;

  gpr_mu_lock(&t->mu);

  GPR_ASSERT(s->global.published_state == GRPC_STREAM_CLOSED ||
             s->global.id == 0);
  GPR_ASSERT(!s->global.in_stream_map);
  if (grpc_chttp2_unregister_stream(t, s) && t->global.sent_goaway) {
    close_transport_locked(exec_ctx, t);
  }
  if (!t->parsing_active && s->global.id) {
    GPR_ASSERT(grpc_chttp2_stream_map_find(&t->parsing_stream_map,
                                           s->global.id) == NULL);
  }

  grpc_chttp2_list_remove_incoming_window_updated(&t->global, &s->global);
  grpc_chttp2_list_remove_writable_stream(&t->global, &s->global);

  gpr_mu_unlock(&t->mu);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    if (s->included[i]) {
      gpr_log(GPR_ERROR, "%s stream %d still included in list %d",
              t->global.is_client ? "client" : "server", s->global.id, i);
      abort();
    }
  }

  GPR_ASSERT(s->global.outgoing_sopb == NULL);
  GPR_ASSERT(s->global.publish_sopb == NULL);
  grpc_sopb_destroy(&s->writing.sopb);
  grpc_sopb_destroy(&s->global.incoming_sopb);
  grpc_chttp2_data_parser_destroy(&s->parsing.data_parser);
  grpc_chttp2_incoming_metadata_buffer_destroy(&s->parsing.incoming_metadata);
  grpc_chttp2_incoming_metadata_buffer_destroy(&s->global.incoming_metadata);
  grpc_chttp2_incoming_metadata_live_op_buffer_end(
      &s->global.outstanding_metadata);

  UNREF_TRANSPORT(exec_ctx, t, "stream");
}

grpc_chttp2_stream_parsing *grpc_chttp2_parsing_lookup_stream(
    grpc_chttp2_transport_parsing *transport_parsing, gpr_uint32 id) {
  grpc_chttp2_transport *t = TRANSPORT_FROM_PARSING(transport_parsing);
  grpc_chttp2_stream *s =
      grpc_chttp2_stream_map_find(&t->parsing_stream_map, id);
  return s ? &s->parsing : NULL;
}

grpc_chttp2_stream_parsing *grpc_chttp2_parsing_accept_stream(
    grpc_chttp2_transport_parsing *transport_parsing, gpr_uint32 id) {
  grpc_chttp2_stream *accepting;
  grpc_chttp2_transport *t = TRANSPORT_FROM_PARSING(transport_parsing);
  GPR_ASSERT(t->accepting_stream == NULL);
  t->accepting_stream = &accepting;
  t->channel_callback.accept_stream(t->channel_callback.accept_stream_user_data,
                                    &t->base, (void *)(gpr_uintptr)id);
  t->accepting_stream = NULL;
  return &accepting->parsing;
}

/*
 * LOCK MANAGEMENT
 */

/* We take a grpc_chttp2_transport-global lock in response to calls coming in
   from above,
   and in response to data being received from below. New data to be written
   is always queued, as are callbacks to process data. During unlock() we
   check our todo lists and initiate callbacks and flush writes. */

static void lock(grpc_chttp2_transport *t) { gpr_mu_lock(&t->mu); }

static void unlock(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t) {
  GRPC_TIMER_BEGIN("unlock", 0);
  unlock_check_read_write_state(exec_ctx, t);
  if (!t->writing_active && !t->closed &&
      grpc_chttp2_unlocking_check_writes(&t->global, &t->writing)) {
    t->writing_active = 1;
    REF_TRANSPORT(t, "writing");
    grpc_exec_ctx_enqueue(exec_ctx, &t->writing_action, 1);
    prevent_endpoint_shutdown(t);
  }

  gpr_mu_unlock(&t->mu);
  GRPC_TIMER_END("unlock", 0);
}

/*
 * OUTPUT PROCESSING
 */

static void push_setting(grpc_chttp2_transport *t, grpc_chttp2_setting_id id,
                         gpr_uint32 value) {
  const grpc_chttp2_setting_parameters *sp =
      &grpc_chttp2_settings_parameters[id];
  gpr_uint32 use_value = GPR_CLAMP(value, sp->min_value, sp->max_value);
  if (use_value != value) {
    gpr_log(GPR_INFO, "Requested parameter %s clamped from %d to %d", sp->name,
            value, use_value);
  }
  if (use_value != t->global.settings[GRPC_LOCAL_SETTINGS][id]) {
    t->global.settings[GRPC_LOCAL_SETTINGS][id] = use_value;
    t->global.dirtied_local_settings = 1;
  }
}

void grpc_chttp2_terminate_writing(grpc_exec_ctx *exec_ctx,
                                   void *transport_writing_ptr, int success) {
  grpc_chttp2_transport_writing *transport_writing = transport_writing_ptr;
  grpc_chttp2_transport *t = TRANSPORT_FROM_WRITING(transport_writing);

  GRPC_TIMER_BEGIN("grpc_chttp2_terminate_writing", 0);

  lock(t);

  allow_endpoint_shutdown_locked(exec_ctx, t);

  if (!success) {
    drop_connection(exec_ctx, t);
  }

  /* cleanup writing related jazz */
  grpc_chttp2_cleanup_writing(exec_ctx, &t->global, &t->writing);

  /* leave the writing flag up on shutdown to prevent further writes in unlock()
     from starting */
  t->writing_active = 0;
  if (t->ep && !t->endpoint_reading) {
    destroy_endpoint(exec_ctx, t);
  }

  unlock(exec_ctx, t);

  UNREF_TRANSPORT(exec_ctx, t, "writing");

  GRPC_TIMER_END("grpc_chttp2_terminate_writing", 0);
}

static void writing_action(grpc_exec_ctx *exec_ctx, void *gt,
                           int iomgr_success_ignored) {
  grpc_chttp2_transport *t = gt;
  GRPC_TIMER_BEGIN("writing_action", 0);
  grpc_chttp2_perform_writes(exec_ctx, &t->writing, t->ep);
  GRPC_TIMER_END("writing_action", 0);
}

void grpc_chttp2_add_incoming_goaway(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    gpr_uint32 goaway_error, gpr_slice goaway_text) {
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
  /* start streams where we have free grpc_chttp2_stream ids and free
   * concurrency */
  while (transport_global->next_stream_id <= MAX_CLIENT_STREAM_ID &&
         transport_global->concurrent_stream_count <
             transport_global
                 ->settings[GRPC_PEER_SETTINGS]
                           [GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS] &&
         grpc_chttp2_list_pop_waiting_for_concurrency(transport_global,
                                                      &stream_global)) {
    GRPC_CHTTP2_IF_TRACING(gpr_log(
        GPR_DEBUG, "HTTP:%s: Allocating new grpc_chttp2_stream %p to id %d",
        transport_global->is_client ? "CLI" : "SVR", stream_global,
        transport_global->next_stream_id));

    GPR_ASSERT(stream_global->id == 0);
    stream_global->id = transport_global->next_stream_id;
    transport_global->next_stream_id += 2;

    if (transport_global->next_stream_id >= MAX_CLIENT_STREAM_ID) {
      connectivity_state_set(exec_ctx, transport_global,
                             GRPC_CHANNEL_TRANSIENT_FAILURE,
                             "no_more_stream_ids");
    }

    stream_global->outgoing_window =
        transport_global->settings[GRPC_PEER_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    stream_global->incoming_window =
        transport_global->settings[GRPC_SENT_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    stream_global->max_recv_bytes =
        GPR_MAX(stream_global->incoming_window, stream_global->max_recv_bytes);
    grpc_chttp2_stream_map_add(
        &TRANSPORT_FROM_GLOBAL(transport_global)->new_stream_map,
        stream_global->id, STREAM_FROM_GLOBAL(stream_global));
    stream_global->in_stream_map = 1;
    transport_global->concurrent_stream_count++;
    grpc_chttp2_list_add_incoming_window_updated(transport_global,
                                                 stream_global);
    grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
  }
  /* cancel out streams that will never be started */
  while (transport_global->next_stream_id >= MAX_CLIENT_STREAM_ID &&
         grpc_chttp2_list_pop_waiting_for_concurrency(transport_global,
                                                      &stream_global)) {
    cancel_from_api(transport_global, stream_global, GRPC_STATUS_UNAVAILABLE);
  }
}

static void perform_stream_op_locked(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, grpc_transport_stream_op *op) {
  GRPC_TIMER_BEGIN("perform_stream_op_locked", 0);
  if (op->cancel_with_status != GRPC_STATUS_OK) {
    cancel_from_api(transport_global, stream_global, op->cancel_with_status);
  }

  if (op->close_with_status != GRPC_STATUS_OK) {
    close_from_api(transport_global, stream_global, op->close_with_status,
                   op->optional_close_message);
  }

  if (op->send_ops) {
    GPR_ASSERT(stream_global->outgoing_sopb == NULL);
    stream_global->send_done_closure = op->on_done_send;
    if (!stream_global->cancelled) {
      stream_global->written_anything = 1;
      stream_global->outgoing_sopb = op->send_ops;
      if (op->is_last_send &&
          stream_global->write_state == GRPC_WRITE_STATE_OPEN) {
        stream_global->write_state = GRPC_WRITE_STATE_QUEUED_CLOSE;
      }
      if (stream_global->id == 0) {
        GRPC_CHTTP2_IF_TRACING(gpr_log(
            GPR_DEBUG,
            "HTTP:%s: New grpc_chttp2_stream %p waiting for concurrency",
            transport_global->is_client ? "CLI" : "SVR", stream_global));
        grpc_chttp2_list_add_waiting_for_concurrency(transport_global,
                                                     stream_global);
        maybe_start_some_streams(exec_ctx, transport_global);
      } else if (stream_global->outgoing_window > 0) {
        grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
      }
    } else {
      grpc_sopb_reset(op->send_ops);
      grpc_exec_ctx_enqueue(exec_ctx, stream_global->send_done_closure, 0);
    }
  }

  if (op->recv_ops) {
    GPR_ASSERT(stream_global->publish_sopb == NULL);
    GPR_ASSERT(stream_global->published_state != GRPC_STREAM_CLOSED);
    stream_global->recv_done_closure = op->on_done_recv;
    stream_global->publish_sopb = op->recv_ops;
    stream_global->publish_sopb->nops = 0;
    stream_global->publish_state = op->recv_state;
    /* clamp max recv bytes */
    op->max_recv_bytes = GPR_MIN(op->max_recv_bytes, GPR_UINT32_MAX);
    if (stream_global->max_recv_bytes < op->max_recv_bytes) {
      GRPC_CHTTP2_FLOWCTL_TRACE_STREAM(
          "op", transport_global, stream_global, max_recv_bytes,
          op->max_recv_bytes - stream_global->max_recv_bytes);
      GRPC_CHTTP2_FLOWCTL_TRACE_STREAM(
          "op", transport_global, stream_global, unannounced_incoming_window,
          op->max_recv_bytes - stream_global->max_recv_bytes);
      stream_global->unannounced_incoming_window +=
          (gpr_uint32)op->max_recv_bytes - stream_global->max_recv_bytes;
      stream_global->max_recv_bytes = (gpr_uint32)op->max_recv_bytes;
    }
    grpc_chttp2_incoming_metadata_live_op_buffer_end(
        &stream_global->outstanding_metadata);
    grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                  stream_global);
    if (stream_global->id != 0) {
      grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
    }
  }

  if (op->bind_pollset) {
    add_to_pollset_locked(exec_ctx, TRANSPORT_FROM_GLOBAL(transport_global),
                          op->bind_pollset);
  }

  grpc_exec_ctx_enqueue(exec_ctx, op->on_consumed, 1);
  GRPC_TIMER_END("perform_stream_op_locked", 0);
}

static void perform_stream_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                              grpc_stream *gs, grpc_transport_stream_op *op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  lock(t);
  perform_stream_op_locked(exec_ctx, &t->global, &s->global, op);
  unlock(exec_ctx, t);
}

static void send_ping_locked(grpc_chttp2_transport *t, grpc_closure *on_recv) {
  grpc_chttp2_outstanding_ping *p = gpr_malloc(sizeof(*p));
  p->next = &t->global.pings;
  p->prev = p->next->prev;
  p->prev->next = p->next->prev = p;
  p->id[0] = (gpr_uint8)((t->global.ping_counter >> 56) & 0xff);
  p->id[1] = (gpr_uint8)((t->global.ping_counter >> 48) & 0xff);
  p->id[2] = (gpr_uint8)((t->global.ping_counter >> 40) & 0xff);
  p->id[3] = (gpr_uint8)((t->global.ping_counter >> 32) & 0xff);
  p->id[4] = (gpr_uint8)((t->global.ping_counter >> 24) & 0xff);
  p->id[5] = (gpr_uint8)((t->global.ping_counter >> 16) & 0xff);
  p->id[6] = (gpr_uint8)((t->global.ping_counter >> 8) & 0xff);
  p->id[7] = (gpr_uint8)(t->global.ping_counter & 0xff);
  p->on_recv = on_recv;
  gpr_slice_buffer_add(&t->global.qbuf, grpc_chttp2_ping_create(0, p->id));
}

static void perform_transport_op(grpc_exec_ctx *exec_ctx, grpc_transport *gt,
                                 grpc_transport_op *op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  int close_transport = 0;

  lock(t);

  grpc_exec_ctx_enqueue(exec_ctx, op->on_consumed, 1);

  if (op->on_connectivity_state_change) {
    grpc_connectivity_state_notify_on_state_change(
        exec_ctx, &t->channel_callback.state_tracker, op->connectivity_state,
        op->on_connectivity_state_change);
  }

  if (op->send_goaway) {
    t->global.sent_goaway = 1;
    grpc_chttp2_goaway_append(
        t->global.last_incoming_stream_id,
        (gpr_uint32)grpc_chttp2_grpc_status_to_http2_error(op->goaway_status),
        gpr_slice_ref(*op->goaway_message), &t->global.qbuf);
    close_transport = !grpc_chttp2_has_streams(t);
  }

  if (op->set_accept_stream != NULL) {
    t->channel_callback.accept_stream = op->set_accept_stream;
    t->channel_callback.accept_stream_user_data =
        op->set_accept_stream_user_data;
  }

  if (op->bind_pollset) {
    add_to_pollset_locked(exec_ctx, t, op->bind_pollset);
  }

  if (op->bind_pollset_set) {
    add_to_pollset_set_locked(exec_ctx, t, op->bind_pollset_set);
  }

  if (op->send_ping) {
    send_ping_locked(t, op->send_ping);
  }

  if (op->disconnect) {
    close_transport_locked(exec_ctx, t);
  }

  unlock(exec_ctx, t);

  if (close_transport) {
    lock(t);
    close_transport_locked(exec_ctx, t);
    unlock(exec_ctx, t);
  }
}

/*
 * INPUT PROCESSING
 */

static grpc_stream_state compute_state(gpr_uint8 write_closed,
                                       gpr_uint8 read_closed) {
  if (write_closed && read_closed) return GRPC_STREAM_CLOSED;
  if (write_closed) return GRPC_STREAM_SEND_CLOSED;
  if (read_closed) return GRPC_STREAM_RECV_CLOSED;
  return GRPC_STREAM_OPEN;
}

static void remove_stream(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                          gpr_uint32 id) {
  size_t new_stream_count;
  grpc_chttp2_stream *s =
      grpc_chttp2_stream_map_delete(&t->parsing_stream_map, id);
  if (!s) {
    s = grpc_chttp2_stream_map_delete(&t->new_stream_map, id);
  }
  grpc_chttp2_list_remove_writable_stream(&t->global, &s->global);
  GPR_ASSERT(s);
  s->global.in_stream_map = 0;
  if (t->parsing.incoming_stream == &s->parsing) {
    t->parsing.incoming_stream = NULL;
    grpc_chttp2_parsing_become_skip_parser(&t->parsing);
  }
  if (grpc_chttp2_unregister_stream(t, s) && t->global.sent_goaway) {
    close_transport_locked(exec_ctx, t);
  }

  new_stream_count = grpc_chttp2_stream_map_size(&t->parsing_stream_map) +
                     grpc_chttp2_stream_map_size(&t->new_stream_map);
  GPR_ASSERT(new_stream_count <= GPR_UINT32_MAX);
  if (new_stream_count != t->global.concurrent_stream_count) {
    t->global.concurrent_stream_count = (gpr_uint32)new_stream_count;
    maybe_start_some_streams(exec_ctx, &t->global);
  }
}

static void unlock_check_read_write_state(grpc_exec_ctx *exec_ctx,
                                          grpc_chttp2_transport *t) {
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global;
  grpc_stream_state state;

  if (!t->parsing_active) {
    /* if a stream is in the stream map, and gets cancelled, we need to ensure
       we are not parsing before continuing the cancellation to keep things in
       a sane state */
    while (grpc_chttp2_list_pop_closed_waiting_for_parsing(transport_global,
                                                           &stream_global)) {
      GPR_ASSERT(stream_global->in_stream_map);
      GPR_ASSERT(stream_global->write_state != GRPC_WRITE_STATE_OPEN);
      GPR_ASSERT(stream_global->read_closed);
      remove_stream(exec_ctx, t, stream_global->id);
      grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                    stream_global);
    }
  }

  if (!t->writing_active) {
    while (grpc_chttp2_list_pop_cancelled_waiting_for_writing(transport_global,
                                                              &stream_global)) {
      grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                    stream_global);
    }
  }

  while (grpc_chttp2_list_pop_read_write_state_changed(transport_global,
                                                       &stream_global)) {
    if (stream_global->cancelled) {
      if (t->writing_active &&
          stream_global->write_state != GRPC_WRITE_STATE_SENT_CLOSE) {
        grpc_chttp2_list_add_cancelled_waiting_for_writing(transport_global,
                                                           stream_global);
      } else {
        stream_global->write_state = GRPC_WRITE_STATE_SENT_CLOSE;
        if (stream_global->outgoing_sopb != NULL) {
          grpc_sopb_reset(stream_global->outgoing_sopb);
          stream_global->outgoing_sopb = NULL;
          grpc_exec_ctx_enqueue(exec_ctx, stream_global->send_done_closure, 1);
        }
        stream_global->read_closed = 1;
        if (!stream_global->published_cancelled) {
          char buffer[GPR_LTOA_MIN_BUFSIZE];
          gpr_ltoa(stream_global->cancelled_status, buffer);
          grpc_chttp2_incoming_metadata_buffer_add(
              &stream_global->incoming_metadata,
              grpc_mdelem_from_strings(t->metadata_context, "grpc-status",
                                       buffer));
          grpc_chttp2_incoming_metadata_buffer_place_metadata_batch_into(
              &stream_global->incoming_metadata, &stream_global->incoming_sopb);
          stream_global->published_cancelled = 1;
        }
      }
    }
    if (stream_global->write_state == GRPC_WRITE_STATE_SENT_CLOSE &&
        stream_global->read_closed && stream_global->in_stream_map) {
      if (t->parsing_active) {
        grpc_chttp2_list_add_closed_waiting_for_parsing(transport_global,
                                                        stream_global);
      } else {
        remove_stream(exec_ctx, t, stream_global->id);
      }
    }
    if (!stream_global->publish_sopb) {
      continue;
    }
    if (stream_global->writing_now != 0) {
      continue;
    }
    /* FIXME(ctiller): we include in_stream_map in our computation of
       whether the stream is write-closed. This is completely bogus,
       but has the effect of delaying stream-closed until the stream
       is indeed evicted from the stream map, making it safe to delete.
       To fix this will require having an edge after stream-closed
       indicating that the stream is closed AND safe to delete. */
    state = compute_state(
        stream_global->write_state == GRPC_WRITE_STATE_SENT_CLOSE &&
            !stream_global->in_stream_map,
        stream_global->read_closed);
    if (stream_global->incoming_sopb.nops == 0 &&
        state == stream_global->published_state) {
      continue;
    }
    grpc_chttp2_incoming_metadata_buffer_postprocess_sopb_and_begin_live_op(
        &stream_global->incoming_metadata, &stream_global->incoming_sopb,
        &stream_global->outstanding_metadata);
    grpc_sopb_swap(stream_global->publish_sopb, &stream_global->incoming_sopb);
    stream_global->published_state = *stream_global->publish_state = state;
    grpc_exec_ctx_enqueue(exec_ctx, stream_global->recv_done_closure, 1);
    stream_global->recv_done_closure = NULL;
    stream_global->publish_sopb = NULL;
    stream_global->publish_state = NULL;
  }
}

static void cancel_from_api(grpc_chttp2_transport_global *transport_global,
                            grpc_chttp2_stream_global *stream_global,
                            grpc_status_code status) {
  stream_global->cancelled = 1;
  stream_global->cancelled_status = status;
  if (stream_global->id != 0) {
    gpr_slice_buffer_add(
        &transport_global->qbuf,
        grpc_chttp2_rst_stream_create(
            stream_global->id,
            (gpr_uint32)grpc_chttp2_grpc_status_to_http2_error(status)));
  }
  grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                stream_global);
}

static void close_from_api(grpc_chttp2_transport_global *transport_global,
                           grpc_chttp2_stream_global *stream_global,
                           grpc_status_code status,
                           gpr_slice *optional_message) {
  gpr_slice hdr;
  gpr_slice status_hdr;
  gpr_slice message_pfx;
  gpr_uint8 *p;
  gpr_uint32 len = 0;

  GPR_ASSERT(status >= 0 && (int)status < 100);

  stream_global->cancelled = 1;
  stream_global->cancelled_status = status;
  GPR_ASSERT(stream_global->id != 0);
  GPR_ASSERT(!stream_global->written_anything);

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
    *p++ = (gpr_uint8)('0' + status);
  } else {
    *p++ = 2;
    *p++ = (gpr_uint8)('0' + (status / 10));
    *p++ = (gpr_uint8)('0' + (status % 10));
  }
  GPR_ASSERT(p == GPR_SLICE_END_PTR(status_hdr));
  len += (gpr_uint32)GPR_SLICE_LENGTH(status_hdr);

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
    *p++ = (gpr_uint8)GPR_SLICE_LENGTH(*optional_message);
    GPR_ASSERT(p == GPR_SLICE_END_PTR(message_pfx));
    len += (gpr_uint32)GPR_SLICE_LENGTH(message_pfx);
    len += (gpr_uint32)GPR_SLICE_LENGTH(*optional_message);
  }

  hdr = gpr_slice_malloc(9);
  p = GPR_SLICE_START_PTR(hdr);
  *p++ = (gpr_uint8)(len >> 16);
  *p++ = (gpr_uint8)(len >> 8);
  *p++ = (gpr_uint8)(len);
  *p++ = GRPC_CHTTP2_FRAME_HEADER;
  *p++ = GRPC_CHTTP2_DATA_FLAG_END_STREAM | GRPC_CHTTP2_DATA_FLAG_END_HEADERS;
  *p++ = (gpr_uint8)(stream_global->id >> 24);
  *p++ = (gpr_uint8)(stream_global->id >> 16);
  *p++ = (gpr_uint8)(stream_global->id >> 8);
  *p++ = (gpr_uint8)(stream_global->id);
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
      grpc_chttp2_rst_stream_create(stream_global->id, GRPC_CHTTP2_NO_ERROR));

  grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                stream_global);
}

static void cancel_stream_cb(grpc_chttp2_transport_global *transport_global,
                             void *user_data,
                             grpc_chttp2_stream_global *stream_global) {
  cancel_from_api(transport_global, stream_global, GRPC_STATUS_UNAVAILABLE);
}

static void end_all_the_calls(grpc_chttp2_transport *t) {
  grpc_chttp2_for_all_streams(&t->global, NULL, cancel_stream_cb);
}

static void drop_connection(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t) {
  close_transport_locked(exec_ctx, t);
  end_all_the_calls(t);
}

/** update window from a settings change */
static void update_global_window(void *args, gpr_uint32 id, void *stream) {
  grpc_chttp2_transport *t = args;
  grpc_chttp2_stream *s = stream;
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global = &s->global;
  int was_zero;
  int is_zero;

  GRPC_CHTTP2_FLOWCTL_TRACE_STREAM("settings", transport_global, stream_global,
                                   outgoing_window,
                                   t->parsing.initial_window_update);
  was_zero = stream_global->outgoing_window <= 0;
  stream_global->outgoing_window += t->parsing.initial_window_update;
  is_zero = stream_global->outgoing_window <= 0;

  if (was_zero && !is_zero) {
    grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
  }
}

static void read_error_locked(grpc_exec_ctx *exec_ctx,
                              grpc_chttp2_transport *t) {
  t->endpoint_reading = 0;
  if (!t->writing_active && t->ep) {
    destroy_endpoint(exec_ctx, t);
  }
}

/* tcp read callback */
static void recv_data(grpc_exec_ctx *exec_ctx, void *tp, int success) {
  size_t i;
  int keep_reading = 0;
  grpc_chttp2_transport *t = tp;

  GRPC_TIMER_BEGIN("recv_data", 0);

  lock(t);
  i = 0;
  GPR_ASSERT(!t->parsing_active);
  if (!t->closed) {
    t->parsing_active = 1;
    /* merge stream lists */
    grpc_chttp2_stream_map_move_into(&t->new_stream_map,
                                     &t->parsing_stream_map);
    grpc_chttp2_prepare_to_read(&t->global, &t->parsing);
    gpr_mu_unlock(&t->mu);
    GRPC_TIMER_BEGIN("recv_data.parse", 0);
    for (; i < t->read_buffer.count &&
               grpc_chttp2_perform_read(exec_ctx, &t->parsing,
                                        t->read_buffer.slices[i]);
         i++)
      ;
    GRPC_TIMER_END("recv_data.parse", 0);
    gpr_mu_lock(&t->mu);
    if (i != t->read_buffer.count) {
      drop_connection(exec_ctx, t);
    }
    /* merge stream lists */
    grpc_chttp2_stream_map_move_into(&t->new_stream_map,
                                     &t->parsing_stream_map);
    t->global.concurrent_stream_count =
        (gpr_uint32)grpc_chttp2_stream_map_size(&t->parsing_stream_map);
    if (t->parsing.initial_window_update != 0) {
      grpc_chttp2_stream_map_for_each(&t->parsing_stream_map,
                                      update_global_window, t);
      t->parsing.initial_window_update = 0;
    }
    /* handle higher level things */
    grpc_chttp2_publish_reads(exec_ctx, &t->global, &t->parsing);
    t->parsing_active = 0;
  }
  if (!success || i != t->read_buffer.count || t->closed) {
    drop_connection(exec_ctx, t);
    read_error_locked(exec_ctx, t);
  } else if (!t->closed) {
    keep_reading = 1;
    REF_TRANSPORT(t, "keep_reading");
    prevent_endpoint_shutdown(t);
  }
  gpr_slice_buffer_reset_and_unref(&t->read_buffer);
  unlock(exec_ctx, t);

  if (keep_reading) {
    grpc_endpoint_read(exec_ctx, t->ep, &t->read_buffer, &t->recv_data);
    allow_endpoint_shutdown_unlocked(exec_ctx, t);
    UNREF_TRANSPORT(exec_ctx, t, "keep_reading");
  } else {
    UNREF_TRANSPORT(exec_ctx, t, "recv_data");
  }

  GRPC_TIMER_END("recv_data", 0);
}

/*
 * CALLBACK LOOP
 */

static void connectivity_state_set(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_connectivity_state state, const char *reason) {
  GRPC_CHTTP2_IF_TRACING(
      gpr_log(GPR_DEBUG, "set connectivity_state=%d", state));
  grpc_connectivity_state_set(exec_ctx, &TRANSPORT_FROM_GLOBAL(transport_global)
                                             ->channel_callback.state_tracker,
                              state, reason);
}

/*
 * POLLSET STUFF
 */

static void add_to_pollset_locked(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_transport *t,
                                  grpc_pollset *pollset) {
  if (t->ep) {
    grpc_endpoint_add_to_pollset(exec_ctx, t->ep, pollset);
  }
}

static void add_to_pollset_set_locked(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *t,
                                      grpc_pollset_set *pollset_set) {
  if (t->ep) {
    grpc_endpoint_add_to_pollset_set(exec_ctx, t->ep, pollset_set);
  }
}

/*
 * TRACING
 */

void grpc_chttp2_flowctl_trace(const char *file, int line, const char *reason,
                               const char *context, const char *var,
                               int is_client, gpr_uint32 stream_id,
                               gpr_int64 current_value, gpr_int64 delta) {
  char *identifier;
  char *context_scope;
  char *context_thread;
  char *underscore_pos = strchr(context, '_');
  GPR_ASSERT(underscore_pos);
  context_thread = gpr_strdup(underscore_pos + 1);
  context_scope = gpr_strdup(context);
  context_scope[underscore_pos - context] = 0;
  if (stream_id) {
    gpr_asprintf(&identifier, "%s[%d]", context_scope, stream_id);
  } else {
    identifier = gpr_strdup(context_scope);
  }
  gpr_log(GPR_INFO,
          "FLOWCTL: %s %-10s %8s %-27s %8lld %c %8lld = %8lld %-10s [%s:%d]",
          is_client ? "client" : "server", identifier, context_thread, var,
          current_value, delta < 0 ? '-' : '+', delta < 0 ? -delta : delta,
          current_value + delta, reason, file, line);
  gpr_free(identifier);
  gpr_free(context_thread);
  gpr_free(context_scope);
}

/*
 * INTEGRATION GLUE
 */

static char *chttp2_get_peer(grpc_exec_ctx *exec_ctx, grpc_transport *t) {
  return gpr_strdup(((grpc_chttp2_transport *)t)->peer_string);
}

static const grpc_transport_vtable vtable = {
    sizeof(grpc_chttp2_stream), init_stream, perform_stream_op,
    perform_transport_op, destroy_stream, destroy_transport, chttp2_get_peer};

grpc_transport *grpc_create_chttp2_transport(
    grpc_exec_ctx *exec_ctx, const grpc_channel_args *channel_args,
    grpc_endpoint *ep, grpc_mdctx *mdctx, int is_client) {
  grpc_chttp2_transport *t = gpr_malloc(sizeof(grpc_chttp2_transport));
  init_transport(exec_ctx, t, channel_args, ep, mdctx, is_client != 0);
  return &t->base;
}

void grpc_chttp2_transport_start_reading(grpc_exec_ctx *exec_ctx,
                                         grpc_transport *transport,
                                         gpr_slice *slices, size_t nslices) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)transport;
  REF_TRANSPORT(t, "recv_data"); /* matches unref inside recv_data */
  gpr_slice_buffer_addn(&t->read_buffer, slices, nslices);
  recv_data(exec_ctx, t, 1);
}
