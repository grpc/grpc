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

#include "src/core/profiling/timers.h"
#include "src/core/support/string.h"
#include "src/core/transport/chttp2/http2_errors.h"
#include "src/core/transport/chttp2/status_conversion.h"
#include "src/core/transport/chttp2/timeout_encoding.h"
#include "src/core/transport/chttp2/internal.h"
#include "src/core/transport/transport_impl.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

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

#if 0
static void lock(grpc_chttp2_transport *t);
static void unlock(grpc_chttp2_transport *t);
#endif

static void unlock_check_channel_callbacks(grpc_chttp2_transport *t);
static void unlock_check_read_write_state(grpc_chttp2_transport *t);

/* forward declarations of various callbacks that we'll build closures around */
static void writing_action(void *t, int iomgr_success_ignored);
static void reading_action(void *t, int iomgr_success_ignored);
static void parsing_action(void *t, int iomgr_success_ignored);
static void notify_closed(void *t, int iomgr_success_ignored);

/** Set a transport level setting, and push it to our peer */
static void push_setting(grpc_chttp2_transport *t, grpc_chttp2_setting_id id,
                         gpr_uint32 value);

/** Endpoint callback to process incoming data */
static void recv_data(void *tp, gpr_slice *slices, size_t nslices,
                      grpc_endpoint_cb_status error);

/** Start disconnection chain */
static void drop_connection(grpc_chttp2_transport *t);

/** Perform a transport_op */
static void perform_op_locked(grpc_chttp2_transport *t,
                              grpc_chttp2_stream *s,
                              void *transport_op);

/** Cancel a stream: coming from the transport API */
static void cancel_from_api(grpc_chttp2_transport_global *transport_global,
                            grpc_chttp2_stream_global *stream_global,
                            grpc_status_code status);

/** Add endpoint from this transport to pollset */
static void add_to_pollset_locked(grpc_chttp2_transport *t,
  grpc_chttp2_stream *s_ignored,
                                  void *pollset);

/** Start new streams that have been created if we can */
static void maybe_start_some_streams(
    grpc_chttp2_transport_global *transport_global);

static void finish_global_actions(grpc_chttp2_transport *t);

/*
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

static void destruct_transport(grpc_chttp2_transport *t) {
  size_t i;

  gpr_mu_lock(&t->executor.mu);

  GPR_ASSERT(t->ep == NULL);

  gpr_slice_buffer_destroy(&t->global.qbuf);

  gpr_slice_buffer_destroy(&t->writing.outbuf);
  grpc_chttp2_hpack_compressor_destroy(&t->writing.hpack_compressor);

  gpr_slice_buffer_destroy(&t->parsing.qbuf);
  grpc_chttp2_hpack_parser_destroy(&t->parsing.hpack_parser);
  grpc_chttp2_goaway_parser_destroy(&t->parsing.goaway_parser);

  grpc_mdstr_unref(t->parsing.str_grpc_timeout);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(t->lists[i].head == NULL);
    GPR_ASSERT(t->lists[i].tail == NULL);
  }

  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->parsing_stream_map) == 0);
  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->new_stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&t->parsing_stream_map);
  grpc_chttp2_stream_map_destroy(&t->new_stream_map);

  gpr_mu_unlock(&t->executor.mu);
  gpr_mu_destroy(&t->executor.mu);

  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  while (t->global.pings.next != &t->global.pings) {
    grpc_chttp2_outstanding_ping *ping = t->global.pings.next;
    grpc_iomgr_add_delayed_callback(ping->on_recv, 0);
    ping->next->prev = ping->prev;
    ping->prev->next = ping->next;
    gpr_free(ping);
  }

  grpc_mdctx_unref(t->metadata_context);

  gpr_free(t);
}

#ifdef REFCOUNTING_DEBUG
#define REF_TRANSPORT(t, r) ref_transport(t, r, __FILE__, __LINE__)
#define UNREF_TRANSPORT(t, r) unref_transport(t, r, __FILE__, __LINE__)
static void unref_transport(grpc_chttp2_transport *t, const char *reason,
                            const char *file, int line) {
  gpr_log(GPR_DEBUG, "chttp2:unref:%p %d->%d %s [%s:%d]", t, t->refs.count,
          t->refs.count - 1, reason, file, line);
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(t);
}

static void ref_transport(grpc_chttp2_transport *t, const char *reason,
                          const char *file, int line) {
  gpr_log(GPR_DEBUG, "chttp2:  ref:%p %d->%d %s [%s:%d]", t, t->refs.count,
          t->refs.count + 1, reason, file, line);
  gpr_ref(&t->refs);
}
#else
#define REF_TRANSPORT(t, r) ref_transport(t)
#define UNREF_TRANSPORT(t, r) unref_transport(t)
static void unref_transport(grpc_chttp2_transport *t) {
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(t);
}

static void ref_transport(grpc_chttp2_transport *t) { gpr_ref(&t->refs); }
#endif

static void init_transport(grpc_chttp2_transport *t,
                           grpc_transport_setup_callback setup, void *arg,
                           const grpc_channel_args *channel_args,
                           grpc_endpoint *ep, gpr_slice *slices, size_t nslices,
                           grpc_mdctx *mdctx, int is_client) {
  size_t i;
  int j;
  grpc_transport_setup_result sr;

  GPR_ASSERT(strlen(GRPC_CHTTP2_CLIENT_CONNECT_STRING) ==
             GRPC_CHTTP2_CLIENT_CONNECT_STRLEN);

  memset(t, 0, sizeof(*t));

  t->base.vtable = &vtable;
  t->ep = ep;
  /* one ref is for destroy, the other for when ep becomes NULL */
  gpr_ref_init(&t->refs, 2);
  gpr_mu_init(&t->executor.mu);
  grpc_mdctx_ref(mdctx);
  t->metadata_context = mdctx;
  t->endpoint_reading = 1;
  t->global.error_state = GRPC_CHTTP2_ERROR_STATE_NONE;
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

  gpr_slice_buffer_init(&t->global.qbuf);

  gpr_slice_buffer_init(&t->writing.outbuf);
  grpc_chttp2_hpack_compressor_init(&t->writing.hpack_compressor, mdctx);
  grpc_iomgr_closure_init(&t->writing_action, writing_action, t);
  grpc_iomgr_closure_init(&t->reading_action, reading_action, t);
  grpc_iomgr_closure_init(&t->parsing_action, parsing_action, t);

  gpr_slice_buffer_init(&t->parsing.qbuf);
  grpc_chttp2_goaway_parser_init(&t->parsing.goaway_parser);
  grpc_chttp2_hpack_parser_init(&t->parsing.hpack_parser, t->metadata_context);

  grpc_iomgr_closure_init(&t->channel_callback.notify_closed, notify_closed, t);
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
                       channel_args->args[i].value.integer);
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
          t->global.next_stream_id = channel_args->args[i].value.integer;
        }
      }
    }
  }

  gpr_mu_lock(&t->executor.mu);
  t->executor.channel_callback_active = 1;
  t->executor.global_active = 1;
  REF_TRANSPORT(t, "init"); /* matches unref at end of this function */
  gpr_mu_unlock(&t->executor.mu);

  sr = setup(arg, &t->base, t->metadata_context);

  t->channel_callback.cb = sr.callbacks;
  t->channel_callback.cb_user_data = sr.user_data;
  t->executor.channel_callback_active = 0;

  finish_global_actions(t);

  REF_TRANSPORT(t, "recv_data"); /* matches unref inside recv_data */
  recv_data(t, slices, nslices, GRPC_ENDPOINT_CB_OK);

  UNREF_TRANSPORT(t, "init");
}

static void destroy_transport_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s_ignored, void *arg_ignored) {
  t->destroying = 1;
  drop_connection(t);
}

static void destroy_transport(grpc_transport *gt) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_run_with_global_lock(t, NULL, destroy_transport_locked, NULL, 0);
  UNREF_TRANSPORT(t, "destroy");
}

static void close_transport_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s_ignored, void *arg_ignored) {
  if (!t->closed) {
    t->closed = 1;
    if (t->ep) {
      grpc_endpoint_shutdown(t->ep);
    }
  }
}

static void close_transport(grpc_transport *gt) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_run_with_global_lock(t, NULL, close_transport_locked, NULL, 0);
}

typedef struct {
  grpc_status_code status;
  gpr_slice debug_data;
} goaway_arg;

static void goaway_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s_ignored, void *a) {
  goaway_arg *arg = a;
  grpc_chttp2_goaway_append(t->global.last_incoming_stream_id,
                            grpc_chttp2_grpc_status_to_http2_error(arg->status),
                            arg->debug_data, &t->global.qbuf);
}

static void goaway(grpc_transport *gt, grpc_status_code status,
                   gpr_slice debug_data) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  goaway_arg arg;
  arg.status = status;
  arg.debug_data = debug_data;
  grpc_chttp2_run_with_global_lock(t, NULL, goaway_locked, &arg, sizeof(arg));
}

static void finish_init_stream_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s, void *arg_ignored) {
  grpc_chttp2_register_stream(t, s);
}

static int init_stream(grpc_transport *gt, grpc_stream *gs,
                       const void *server_data, grpc_transport_op *initial_op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  memset(s, 0, sizeof(*s));

  grpc_chttp2_incoming_metadata_buffer_init(&s->parsing.incoming_metadata);
  grpc_chttp2_incoming_metadata_buffer_init(&s->global.incoming_metadata);
  grpc_sopb_init(&s->writing.sopb);
  grpc_sopb_init(&s->global.incoming_sopb);
  grpc_chttp2_data_parser_init(&s->parsing.data_parser);

  REF_TRANSPORT(t, "stream");

  if (server_data) {
    GPR_ASSERT(t->executor.parsing_active);
    s->global.id = (gpr_uint32)(gpr_uintptr)server_data;
    s->global.outgoing_window =
        t->global.settings[GRPC_PEER_SETTINGS]
                          [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    s->parsing.incoming_window = s->global.incoming_window =
        t->global.settings[GRPC_SENT_SETTINGS]
                          [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    *t->accepting_stream = s;
    grpc_chttp2_stream_map_add(&t->parsing_stream_map, s->global.id, s);
    s->global.in_stream_map = 1;
  }

  grpc_chttp2_run_with_global_lock(t, s, finish_init_stream_locked, NULL, 0);
  if (initial_op) grpc_chttp2_run_with_global_lock(t, s, perform_op_locked, initial_op, sizeof(*initial_op));

  return 0;
}

static void destroy_stream(grpc_transport *gt, grpc_stream *gs) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;

  int i;

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(!s->included[i]);
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

  UNREF_TRANSPORT(t, "stream");
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
  t->channel_callback.cb->accept_stream(t->channel_callback.cb_user_data,
                                        &t->base, (void *)(gpr_uintptr)id);
  t->accepting_stream = NULL;
  return &accepting->parsing;
}

/*
 * LOCK MANAGEMENT
 */

static void finish_global_actions(grpc_chttp2_transport *t) {
  grpc_chttp2_executor_action_header *hdr;
  grpc_chttp2_executor_action_header *next;
  grpc_iomgr_closure *run_closures;

  for (;;) {
    unlock_check_read_write_state(t);
    if (!t->executor.writing_active && t->global.error_state == GRPC_CHTTP2_ERROR_STATE_NONE &&
        grpc_chttp2_unlocking_check_writes(&t->global, &t->writing)) {
      t->executor.writing_active = 1;
      REF_TRANSPORT(t, "writing");
      grpc_chttp2_schedule_closure(&t->global, &t->writing_action, 1);
    }
    unlock_check_channel_callbacks(t);

    run_closures = t->global.pending_closures;
    t->global.pending_closures = NULL;

    gpr_mu_lock(&t->executor.mu);
    t->executor.global_active = 0;
    gpr_mu_unlock(&t->executor.mu);

    while (run_closures) {
      grpc_iomgr_closure *next = run_closures->next;
      run_closures->cb(run_closures->cb_arg, run_closures->success);
      run_closures = next;
    }

    gpr_mu_lock(&t->executor.mu);
    if (!t->executor.global_active && t->executor.pending_actions) {
      t->executor.global_active = 1;
      hdr = t->executor.pending_actions;
      t->executor.pending_actions = NULL;
      gpr_mu_unlock(&t->executor.mu);
      while (hdr != NULL) {
        hdr->action(t, hdr->stream, hdr->arg);
        next = hdr->next;
        gpr_free(hdr);
        UNREF_TRANSPORT(t, "pending_action");
        hdr = next;
      }
      continue;
    }
    gpr_mu_unlock(&t->executor.mu);
    break;
  }
}

void grpc_chttp2_run_with_global_lock(grpc_chttp2_transport *t, grpc_chttp2_stream *optional_stream,
  void (*action)(grpc_chttp2_transport *t, grpc_chttp2_stream *s, void *arg),
  void *arg, size_t sizeof_arg) {
  grpc_chttp2_executor_action_header *hdr;

  REF_TRANSPORT(t, "run_global");
  gpr_mu_lock(&t->executor.mu);

  for (;;) {
    if (!t->executor.global_active) {
      t->executor.global_active = 1;
      gpr_mu_unlock(&t->executor.mu);

      action(t, optional_stream, arg);

      finish_global_actions(t);
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

  UNREF_TRANSPORT(t, "run_global");
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

static void terminate_writing_with_lock(grpc_chttp2_transport *t, grpc_chttp2_stream *s_ignored, void *a) {
  int success = *(int*)a;

  if (!success) {
    drop_connection(t);
  }

  /* cleanup writing related jazz */
  grpc_chttp2_cleanup_writing(&t->global, &t->writing);

  /* leave the writing flag up on shutdown to prevent further writes in unlock()
     from starting */
  t->executor.writing_active = 0;
  if (t->ep && !t->endpoint_reading) {
    grpc_endpoint_destroy(t->ep);
    t->ep = NULL;
    UNREF_TRANSPORT(
        t, "disconnect"); /* safe because we'll still have the ref for write */
  }

  UNREF_TRANSPORT(t, "writing");
}

void grpc_chttp2_terminate_writing(
    grpc_chttp2_transport_writing *transport_writing, int success) {
  grpc_chttp2_transport *t = TRANSPORT_FROM_WRITING(transport_writing);
  grpc_chttp2_run_with_global_lock(t, NULL, terminate_writing_with_lock, &success, sizeof(success));
}

static void writing_action(void *gt, int iomgr_success_ignored) {
  grpc_chttp2_transport *t = gt;
  grpc_chttp2_perform_writes(&t->writing, t->ep);
}

void grpc_chttp2_add_incoming_goaway(
    grpc_chttp2_transport_global *transport_global, gpr_uint32 goaway_error,
    gpr_slice goaway_text) {
  char *msg = gpr_hexdump((char*)GPR_SLICE_START_PTR(goaway_text), GPR_SLICE_LENGTH(goaway_text), GPR_HEXDUMP_PLAINTEXT);
  gpr_free(msg);
  if (transport_global->goaway_state == GRPC_CHTTP2_ERROR_STATE_NONE) {
    transport_global->goaway_state = GRPC_CHTTP2_ERROR_STATE_SEEN;
    transport_global->goaway_text = goaway_text;
    transport_global->goaway_error = goaway_error;
  } else {
    gpr_slice_unref(goaway_text);
  }
}

static void maybe_start_some_streams(
    grpc_chttp2_transport_global *transport_global) {
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
      grpc_chttp2_add_incoming_goaway(
          transport_global, GRPC_CHTTP2_NO_ERROR,
          gpr_slice_from_copied_string("Exceeded sequence number limit"));
    }

    stream_global->outgoing_window =
        transport_global->settings[GRPC_PEER_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    stream_global->incoming_window =
        transport_global->settings[GRPC_SENT_SETTINGS]
                                  [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
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

static void perform_op_locked(grpc_chttp2_transport *t,
                              grpc_chttp2_stream *s,
                              void *transport_op) {
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global = &s->global;
  grpc_transport_op *op = transport_op;

  if (op->cancel_with_status != GRPC_STATUS_OK) {
    cancel_from_api(transport_global, stream_global, op->cancel_with_status);
  }

  if (op->send_ops) {
    GPR_ASSERT(stream_global->outgoing_sopb == NULL);
    stream_global->send_done_closure = op->on_done_send;
    if (!stream_global->cancelled) {
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
        maybe_start_some_streams(transport_global);
      } else if (stream_global->outgoing_window > 0) {
        grpc_chttp2_list_add_writable_stream(transport_global, stream_global);
      }
    } else {
      grpc_sopb_reset(op->send_ops);
      grpc_chttp2_schedule_closure(transport_global,
                                   stream_global->send_done_closure, 0);
    }
  }

  if (op->recv_ops) {
    GPR_ASSERT(stream_global->publish_sopb == NULL);
    GPR_ASSERT(stream_global->published_state != GRPC_STREAM_CLOSED);
    stream_global->recv_done_closure = op->on_done_recv;
    stream_global->publish_sopb = op->recv_ops;
    stream_global->publish_sopb->nops = 0;
    stream_global->publish_state = op->recv_state;
    grpc_chttp2_incoming_metadata_live_op_buffer_end(
        &stream_global->outstanding_metadata);
    grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                  stream_global);
    grpc_chttp2_list_add_writable_window_update_stream(transport_global,
                                                       stream_global);
  }

  if (op->bind_pollset) {
    add_to_pollset_locked(TRANSPORT_FROM_GLOBAL(transport_global), NULL,
                          op->bind_pollset);
  }

  if (op->on_consumed) {
    grpc_chttp2_schedule_closure(transport_global, op->on_consumed, 1);
  }
}

static void perform_op(grpc_transport *gt, grpc_stream *gs,
                       grpc_transport_op *op) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_stream *s = (grpc_chttp2_stream *)gs;
  grpc_chttp2_run_with_global_lock(t, s, perform_op_locked, op, sizeof(*op));
}

static void send_ping_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s_ignored, void *a) {
  grpc_chttp2_outstanding_ping *p = gpr_malloc(sizeof(*p));
  p->next = &t->global.pings;
  p->prev = p->next->prev;
  p->prev->next = p->next->prev = p;
  p->id[0] = (t->global.ping_counter >> 56) & 0xff;
  p->id[1] = (t->global.ping_counter >> 48) & 0xff;
  p->id[2] = (t->global.ping_counter >> 40) & 0xff;
  p->id[3] = (t->global.ping_counter >> 32) & 0xff;
  p->id[4] = (t->global.ping_counter >> 24) & 0xff;
  p->id[5] = (t->global.ping_counter >> 16) & 0xff;
  p->id[6] = (t->global.ping_counter >> 8) & 0xff;
  p->id[7] = t->global.ping_counter & 0xff;
  p->on_recv = *(grpc_iomgr_closure**)a;
  gpr_slice_buffer_add(&t->global.qbuf, grpc_chttp2_ping_create(0, p->id));
}

static void send_ping(grpc_transport *gt, grpc_iomgr_closure *on_recv) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_run_with_global_lock(t, NULL, send_ping_locked, &on_recv, sizeof(on_recv));
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

static void remove_stream(grpc_chttp2_transport *t, gpr_uint32 id) {
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
    grpc_chttp2_parsing_become_skip_parser(&t->parsing);
  }

  new_stream_count =
      grpc_chttp2_stream_map_size(&t->parsing_stream_map) +
      grpc_chttp2_stream_map_size(&t->new_stream_map);
  if (new_stream_count != t->global.concurrent_stream_count) {
    t->global.concurrent_stream_count = new_stream_count;
    maybe_start_some_streams(&t->global);
  }
}

static void unlock_check_read_write_state(grpc_chttp2_transport *t) {
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global;
  grpc_stream_state state;

  if (!t->executor.parsing_active) {
    /* if a stream is in the stream map, and gets cancelled, we need to ensure
       we are not parsing before continuing the cancellation to keep things in
       a sane state */
    while (grpc_chttp2_list_pop_closed_waiting_for_parsing(transport_global,
                                                           &stream_global)) {
      GPR_ASSERT(stream_global->in_stream_map);
      GPR_ASSERT(stream_global->write_state != GRPC_WRITE_STATE_OPEN);
      GPR_ASSERT(stream_global->read_closed);
      remove_stream(t, stream_global->id);
      grpc_chttp2_list_add_read_write_state_changed(transport_global,
                                                    stream_global);
    }
  }

  while (grpc_chttp2_list_pop_read_write_state_changed(transport_global,
                                                       &stream_global)) {
    if (stream_global->cancelled) {
      stream_global->write_state = GRPC_WRITE_STATE_SENT_CLOSE;
      stream_global->read_closed = 1;
      if (!stream_global->published_cancelled) {
        char buffer[GPR_LTOA_MIN_BUFSIZE];
        gpr_ltoa(stream_global->cancelled_status, buffer);
        grpc_chttp2_incoming_metadata_buffer_add(&stream_global->incoming_metadata,
          grpc_mdelem_from_strings(t->metadata_context, "grpc-status", buffer));
        grpc_chttp2_incoming_metadata_buffer_place_metadata_batch_into(
          &stream_global->incoming_metadata,
          &stream_global->incoming_sopb);
        stream_global->published_cancelled = 1;
      }
    }
    if (stream_global->write_state == GRPC_WRITE_STATE_SENT_CLOSE &&
        stream_global->read_closed && stream_global->in_stream_map) {
      if (t->executor.parsing_active) {
        grpc_chttp2_list_add_closed_waiting_for_parsing(transport_global,
                                                        stream_global);
      } else {
        remove_stream(t, stream_global->id);
      }
    }
    if (!stream_global->publish_sopb) {
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
    if (state == GRPC_STREAM_CLOSED) {
      GPR_ASSERT(!stream_global->in_stream_map);
      grpc_chttp2_unregister_stream(TRANSPORT_FROM_GLOBAL(transport_global), STREAM_FROM_GLOBAL(stream_global));
      grpc_chttp2_list_remove_incoming_window_updated(transport_global, stream_global);
      grpc_chttp2_list_remove_writable_window_update_stream(transport_global, stream_global);
    }
    grpc_sopb_swap(stream_global->publish_sopb, &stream_global->incoming_sopb);
    stream_global->published_state = *stream_global->publish_state = state;
    grpc_chttp2_schedule_closure(transport_global,
                                 stream_global->recv_done_closure, 1);
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
    gpr_slice_buffer_add(&transport_global->qbuf,
                         grpc_chttp2_rst_stream_create(
                             stream_global->id,
                             grpc_chttp2_grpc_status_to_http2_error(status)));
  }
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

static void drop_connection(grpc_chttp2_transport *t) {
  if (t->global.error_state == GRPC_CHTTP2_ERROR_STATE_NONE) {
    t->global.error_state = GRPC_CHTTP2_ERROR_STATE_SEEN;
  }
  close_transport_locked(t, NULL, NULL);
  end_all_the_calls(t);
}

static void read_error_locked(grpc_chttp2_transport *t) {
  t->endpoint_reading = 0;
  if (!t->executor.writing_active && t->ep) {
    grpc_endpoint_destroy(t->ep);
    t->ep = NULL;
    /* safe as we still have a ref for read */
    UNREF_TRANSPORT(t, "disconnect");
  }
}

static void recv_data_error_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s, void *a) {
  size_t i;

  drop_connection(t);
  read_error_locked(t);
  for (i = 0; i < t->executor_parsing.nslices; i++) gpr_slice_unref(t->executor_parsing.slices[i]);
  memset(&t->executor_parsing, 0, sizeof(t->executor_parsing));
  UNREF_TRANSPORT(t, "recv_data");
}

static void finish_parsing_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s_ignored, void *a) {
  size_t i = *(size_t *)a;

  if (i != t->executor_parsing.nslices) {
    drop_connection(t);
  }
  /* merge stream lists */
  grpc_chttp2_stream_map_move_into(&t->new_stream_map,
                                   &t->parsing_stream_map);
  t->global.concurrent_stream_count = grpc_chttp2_stream_map_size(&t->parsing_stream_map);
  /* handle higher level things */
  grpc_chttp2_publish_reads(&t->global, &t->parsing);
  t->executor.parsing_active = 0;

  for (; i < t->executor_parsing.nslices; i++) gpr_slice_unref(t->executor_parsing.slices[i]);

  memset(&t->executor_parsing, 0, sizeof(t->executor_parsing));

  if (i == t->executor_parsing.nslices) {
    grpc_chttp2_schedule_closure(&t->global, &t->reading_action, 1);
  } else {
    read_error_locked(t);
    UNREF_TRANSPORT(t, "recv_data");
  }
}

static void parsing_action(void *pt, int iomgr_success_ignored) {
  size_t i;
  grpc_chttp2_transport *t = pt;
  for (i = 0; i < t->executor_parsing.nslices && grpc_chttp2_perform_read(&t->parsing, t->executor_parsing.slices[i]);
       i++) {
    gpr_slice_unref(t->executor_parsing.slices[i]);
  }
  grpc_chttp2_run_with_global_lock(t, NULL, finish_parsing_locked, &i, sizeof(i));
}

static void recv_data_ok_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s, void *a) {
  size_t i;
  GPR_ASSERT(!t->executor.parsing_active);
  if (t->global.error_state == GRPC_CHTTP2_ERROR_STATE_NONE) {
    t->executor.parsing_active = 1;
    /* merge stream lists */
    grpc_chttp2_stream_map_move_into(&t->new_stream_map,
                                     &t->parsing_stream_map);
    grpc_chttp2_prepare_to_read(&t->global, &t->parsing);
    /* schedule more work to do unlocked */
    grpc_chttp2_schedule_closure(&t->global, &t->parsing_action, 1);
  } else {
    for (i = 0; i < t->executor_parsing.nslices; i++) gpr_slice_unref(t->executor_parsing.slices[i]);
    memset(&t->executor_parsing, 0, sizeof(t->executor_parsing));
  }
}

/** update window from a settings change */
static void update_global_window(void *args, gpr_uint32 id, void *stream) {
  grpc_chttp2_transport *t = args;
  grpc_chttp2_stream *s = stream;
  grpc_chttp2_transport_global *transport_global = &t->global;
  grpc_chttp2_stream_global *stream_global = &s->global;

  GRPC_CHTTP2_FLOWCTL_TRACE_STREAM("settings", transport_global, stream_global,
                                   outgoing_window,
                                   t->parsing.initial_window_update);
  stream_global->outgoing_window += t->parsing.initial_window_update;
}

/* tcp read callback */
static void recv_data(void *tp, gpr_slice *slices, size_t nslices,
                      grpc_endpoint_cb_status error) {
  grpc_chttp2_transport *t = tp;

  t->executor_parsing.slices = slices;
  t->executor_parsing.nslices = nslices;

  switch (error) {
    case GRPC_ENDPOINT_CB_SHUTDOWN:
    case GRPC_ENDPOINT_CB_EOF:
    case GRPC_ENDPOINT_CB_ERROR:
      grpc_chttp2_run_with_global_lock(t, NULL, recv_data_error_locked, NULL, 0);
      break;
    case GRPC_ENDPOINT_CB_OK:
      grpc_chttp2_run_with_global_lock(t, NULL, recv_data_ok_locked, NULL, 0);
      break;
  }
  if (unref) {
    UNREF_TRANSPORT(t, "recv_data");
  }
}

static void reading_action(void *pt, int iomgr_success_ignored) {
  grpc_chttp2_transport *t = pt;
  grpc_endpoint_notify_on_read(t->ep, recv_data, t);
}

/*
 * CALLBACK LOOP
 */

typedef struct {
  grpc_chttp2_transport *t;
  gpr_uint32 error;
  gpr_slice text;
  grpc_iomgr_closure closure;
} notify_goaways_args;

static void finished_channel_callbacks_locked(grpc_chttp2_transport *t, grpc_chttp2_stream *s_ignored, void *arg_ignored) {
  t->executor.channel_callback_active = 0;
}

static void notify_goaways(void *p, int iomgr_success_ignored) {
  notify_goaways_args *a = p;
  grpc_chttp2_transport *t = a->t;

  t->channel_callback.cb->goaway(t->channel_callback.cb_user_data, &t->base,
                                 a->error, a->text);

  gpr_free(a);

  grpc_chttp2_run_with_global_lock(t, NULL, finished_channel_callbacks_locked, NULL, 0);
  UNREF_TRANSPORT(t, "notify_goaways");
}

static void notify_closed(void *gt, int iomgr_success_ignored) {
  grpc_chttp2_transport *t = gt;
  t->channel_callback.cb->closed(t->channel_callback.cb_user_data, &t->base);

  grpc_chttp2_run_with_global_lock(t, NULL, finished_channel_callbacks_locked, NULL, 0);
  UNREF_TRANSPORT(t, "notify_closed");
}

static void unlock_check_channel_callbacks(grpc_chttp2_transport *t) {
  if (t->executor.channel_callback_active) {
    return;
  }
  if (t->global.goaway_state != GRPC_CHTTP2_ERROR_STATE_NONE) {
    if (t->global.goaway_state == GRPC_CHTTP2_ERROR_STATE_SEEN &&
        t->global.error_state != GRPC_CHTTP2_ERROR_STATE_NOTIFIED) {
      notify_goaways_args *a = gpr_malloc(sizeof(*a));
      a->t = t;
      a->error = t->global.goaway_error;
      a->text = t->global.goaway_text;
      t->global.goaway_state = GRPC_CHTTP2_ERROR_STATE_NOTIFIED;
      t->executor.channel_callback_active = 1;
      grpc_iomgr_closure_init(&a->closure, notify_goaways, a);
      REF_TRANSPORT(t, "notify_goaways");
      grpc_chttp2_schedule_closure(&t->global, &a->closure, 1);
      return;
    } else if (t->global.goaway_state != GRPC_CHTTP2_ERROR_STATE_NOTIFIED) {
      return;
    }
  }
  if (t->global.error_state == GRPC_CHTTP2_ERROR_STATE_SEEN) {
    t->global.error_state = GRPC_CHTTP2_ERROR_STATE_NOTIFIED;
    t->executor.channel_callback_active = 1;
    REF_TRANSPORT(t, "notify_closed");
    grpc_chttp2_schedule_closure(&t->global, &t->channel_callback.notify_closed,
                                 1);
  }
}

void grpc_chttp2_schedule_closure(
    grpc_chttp2_transport_global *transport_global, grpc_iomgr_closure *closure,
    int success) {
  closure->success = success;
  closure->next = transport_global->pending_closures;
  transport_global->pending_closures = closure;
}

/*
 * POLLSET STUFF
 */

static void add_to_pollset_locked(grpc_chttp2_transport *t,
                                  grpc_chttp2_stream *s,
                                  void *pollset) {
  if (t->ep) {
    grpc_endpoint_add_to_pollset(t->ep, pollset);
  }
}

static void add_to_pollset(grpc_transport *gt, grpc_pollset *pollset) {
  grpc_chttp2_transport *t = (grpc_chttp2_transport *)gt;
  grpc_chttp2_run_with_global_lock(t, NULL, add_to_pollset_locked, pollset, 0);
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
          "FLOWCTL: %s %-10s %8s %-23s %8lld %c %8lld = %8lld %-10s [%s:%d]",
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

static const grpc_transport_vtable vtable = {sizeof(grpc_chttp2_stream),
                                             init_stream,
                                             perform_op,
                                             add_to_pollset,
                                             destroy_stream,
                                             goaway,
                                             close_transport,
                                             send_ping,
                                             destroy_transport};

void grpc_create_chttp2_transport(grpc_transport_setup_callback setup,
                                  void *arg,
                                  const grpc_channel_args *channel_args,
                                  grpc_endpoint *ep, gpr_slice *slices,
                                  size_t nslices, grpc_mdctx *mdctx,
                                  int is_client) {
  grpc_chttp2_transport *t = gpr_malloc(sizeof(grpc_chttp2_transport));
  init_transport(t, setup, arg, channel_args, ep, slices, nslices, mdctx,
                 is_client);
}
