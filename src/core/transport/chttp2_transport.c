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
#include "src/core/transport/chttp2/frame_data.h"
#include "src/core/transport/chttp2/frame_goaway.h"
#include "src/core/transport/chttp2/frame_ping.h"
#include "src/core/transport/chttp2/frame_rst_stream.h"
#include "src/core/transport/chttp2/frame_settings.h"
#include "src/core/transport/chttp2/frame_window_update.h"
#include "src/core/transport/chttp2/hpack_parser.h"
#include "src/core/transport/chttp2/http2_errors.h"
#include "src/core/transport/chttp2/status_conversion.h"
#include "src/core/transport/chttp2/stream_encoder.h"
#include "src/core/transport/chttp2/stream_map.h"
#include "src/core/transport/chttp2/timeout_encoding.h"
#include "src/core/transport/transport_impl.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/useful.h>

#define DEFAULT_WINDOW 65535
#define DEFAULT_CONNECTION_WINDOW_TARGET (1024 * 1024)
#define MAX_WINDOW 0x7fffffffu

#define MAX_CLIENT_STREAM_ID 0x7fffffffu

#define CLIENT_CONNECT_STRING "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define CLIENT_CONNECT_STRLEN 24

int grpc_http_trace = 0;
int grpc_flowctl_trace = 0;

typedef struct transport transport;
typedef struct stream stream;

#define IF_TRACING(stmt)  \
  if (!(grpc_http_trace)) \
    ;                     \
  else                    \
  stmt

#define FLOWCTL_TRACE(t, obj, dir, id, delta) \
  if (!grpc_flowctl_trace)                    \
    ;                                         \
  else                                        \
  flowctl_trace(t, #dir, obj->dir##_window, id, delta)

/* streams are kept in various linked lists depending on what things need to
   happen to them... this enum labels each list */
typedef enum {
  /* streams that have pending writes */
  WRITABLE = 0,
  /* streams that have been selected to be written */
  WRITING,
  /* streams that have just been written, and included a close */
  WRITTEN_CLOSED,
  /* streams that have been cancelled and have some pending state updates
     to perform */
  CANCELLED,
  /* streams that want to send window updates */
  WINDOW_UPDATE,
  /* streams that are waiting to start because there are too many concurrent
     streams on the connection */
  WAITING_FOR_CONCURRENCY,
  /* streams that have finished reading: we wait until unlock to coalesce
     all changes into one callback */
  FINISHED_READ_OP,
  STREAM_LIST_COUNT /* must be last */
} stream_list_id;

/* deframer state for the overall http2 stream of bytes */
typedef enum {
  /* prefix: one entry per http2 connection prefix byte */
  DTS_CLIENT_PREFIX_0 = 0,
  DTS_CLIENT_PREFIX_1,
  DTS_CLIENT_PREFIX_2,
  DTS_CLIENT_PREFIX_3,
  DTS_CLIENT_PREFIX_4,
  DTS_CLIENT_PREFIX_5,
  DTS_CLIENT_PREFIX_6,
  DTS_CLIENT_PREFIX_7,
  DTS_CLIENT_PREFIX_8,
  DTS_CLIENT_PREFIX_9,
  DTS_CLIENT_PREFIX_10,
  DTS_CLIENT_PREFIX_11,
  DTS_CLIENT_PREFIX_12,
  DTS_CLIENT_PREFIX_13,
  DTS_CLIENT_PREFIX_14,
  DTS_CLIENT_PREFIX_15,
  DTS_CLIENT_PREFIX_16,
  DTS_CLIENT_PREFIX_17,
  DTS_CLIENT_PREFIX_18,
  DTS_CLIENT_PREFIX_19,
  DTS_CLIENT_PREFIX_20,
  DTS_CLIENT_PREFIX_21,
  DTS_CLIENT_PREFIX_22,
  DTS_CLIENT_PREFIX_23,
  /* frame header byte 0... */
  /* must follow from the prefix states */
  DTS_FH_0,
  DTS_FH_1,
  DTS_FH_2,
  DTS_FH_3,
  DTS_FH_4,
  DTS_FH_5,
  DTS_FH_6,
  DTS_FH_7,
  /* ... frame header byte 8 */
  DTS_FH_8,
  /* inside a http2 frame */
  DTS_FRAME
} deframe_transport_state;

typedef enum {
  WRITE_STATE_OPEN,
  WRITE_STATE_QUEUED_CLOSE,
  WRITE_STATE_SENT_CLOSE
} WRITE_STATE;

typedef struct {
  stream *head;
  stream *tail;
} stream_list;

typedef struct {
  stream *next;
  stream *prev;
} stream_link;

typedef enum {
  ERROR_STATE_NONE,
  ERROR_STATE_SEEN,
  ERROR_STATE_NOTIFIED
} error_state;

/* We keep several sets of connection wide parameters */
typedef enum {
  /* The settings our peer has asked for (and we have acked) */
  PEER_SETTINGS = 0,
  /* The settings we'd like to have */
  LOCAL_SETTINGS,
  /* The settings we've published to our peer */
  SENT_SETTINGS,
  /* The settings the peer has acked */
  ACKED_SETTINGS,
  NUM_SETTING_SETS
} setting_set;

/* Outstanding ping request data */
typedef struct {
  gpr_uint8 id[8];
  void (*cb)(void *user_data);
  void *user_data;
} outstanding_ping;

typedef struct {
  grpc_status_code status;
  gpr_slice debug;
} pending_goaway;

typedef struct {
  void (*cb)(void *user_data, int success);
  void *user_data;
  int success;
} op_closure;

typedef struct {
  op_closure *callbacks;
  size_t count;
  size_t capacity;
} op_closure_array;

struct transport {
  grpc_transport base; /* must be first */
  const grpc_transport_callbacks *cb;
  void *cb_user_data;
  grpc_endpoint *ep;
  grpc_mdctx *metadata_context;
  gpr_refcount refs;
  gpr_uint8 is_client;

  gpr_mu mu;
  gpr_cv cv;

  /* basic state management - what are we doing at the moment? */
  gpr_uint8 reading;
  gpr_uint8 writing;
  gpr_uint8 calling_back;
  gpr_uint8 destroying;
  gpr_uint8 closed;
  error_state error_state;

  /* queued callbacks */
  op_closure_array pending_callbacks;
  op_closure_array executing_callbacks;

  /* stream indexing */
  gpr_uint32 next_stream_id;
  gpr_uint32 last_incoming_stream_id;

  /* settings */
  gpr_uint32 settings[NUM_SETTING_SETS][GRPC_CHTTP2_NUM_SETTINGS];
  gpr_uint32 force_send_settings;   /* bitmask of setting indexes to send out */
  gpr_uint8 sent_local_settings;    /* have local settings been sent? */
  gpr_uint8 dirtied_local_settings; /* are the local settings dirty? */

  /* window management */
  gpr_uint32 outgoing_window;
  gpr_uint32 incoming_window;
  gpr_uint32 connection_window_target;

  /* deframing */
  deframe_transport_state deframe_state;
  gpr_uint8 incoming_frame_type;
  gpr_uint8 incoming_frame_flags;
  gpr_uint8 header_eof;
  gpr_uint32 expect_continuation_stream_id;
  gpr_uint32 incoming_frame_size;
  gpr_uint32 incoming_stream_id;

  /* hpack encoding */
  grpc_chttp2_hpack_compressor hpack_compressor;

  /* various parsers */
  grpc_chttp2_hpack_parser hpack_parser;
  /* simple one shot parsers */
  union {
    grpc_chttp2_window_update_parser window_update;
    grpc_chttp2_settings_parser settings;
    grpc_chttp2_ping_parser ping;
  } simple_parsers;

  /* goaway */
  grpc_chttp2_goaway_parser goaway_parser;
  pending_goaway *pending_goaways;
  size_t num_pending_goaways;
  size_t cap_pending_goaways;

  /* state for a stream that's not yet been created */
  grpc_stream_op_buffer new_stream_sopb;

  /* stream ops that need to be destroyed, but outside of the lock */
  grpc_stream_op_buffer nuke_later_sopb;

  /* active parser */
  void *parser_data;
  stream *incoming_stream;
  grpc_chttp2_parse_error (*parser)(void *parser_user_data,
                                    grpc_chttp2_parse_state *state,
                                    gpr_slice slice, int is_last);

  gpr_slice_buffer outbuf;
  gpr_slice_buffer qbuf;

  stream_list lists[STREAM_LIST_COUNT];
  grpc_chttp2_stream_map stream_map;

  /* metadata object cache */
  grpc_mdstr *str_grpc_timeout;

  /* pings */
  outstanding_ping *pings;
  size_t ping_count;
  size_t ping_capacity;
  gpr_int64 ping_counter;
};

struct stream {
  gpr_uint32 id;

  gpr_uint32 incoming_window;
  gpr_int64 outgoing_window;
  /* when the application requests writes be closed, the write_closed is
     'queued'; when the close is flow controlled into the send path, we are
     'sending' it; when the write has been performed it is 'sent' */
  WRITE_STATE write_state;
  gpr_uint8 send_closed;
  gpr_uint8 read_closed;
  gpr_uint8 cancelled;

  op_closure send_done_closure;
  op_closure recv_done_closure;

  stream_link links[STREAM_LIST_COUNT];
  gpr_uint8 included[STREAM_LIST_COUNT];

  /* incoming metadata */
  grpc_linked_mdelem *incoming_metadata;
  size_t incoming_metadata_count;
  size_t incoming_metadata_capacity;
  grpc_linked_mdelem *old_incoming_metadata;
  gpr_timespec incoming_deadline;

  /* sops from application */
  grpc_stream_op_buffer *outgoing_sopb;
  grpc_stream_op_buffer *incoming_sopb;
  grpc_stream_state *publish_state;
  grpc_stream_state published_state;
  /* sops that have passed flow control to be written */
  grpc_stream_op_buffer writing_sopb;

  grpc_chttp2_data_parser parser;

  grpc_stream_state callback_state;
  grpc_stream_op_buffer callback_sopb;
};

static const grpc_transport_vtable vtable;

static void push_setting(transport *t, grpc_chttp2_setting_id id,
                         gpr_uint32 value);

static int prepare_callbacks(transport *t);
static void run_callbacks(transport *t, const grpc_transport_callbacks *cb);
static void call_cb_closed(transport *t, const grpc_transport_callbacks *cb);

static int prepare_write(transport *t);
static void perform_write(transport *t, grpc_endpoint *ep);

static void lock(transport *t);
static void unlock(transport *t);

static void drop_connection(transport *t);
static void end_all_the_calls(transport *t);

static stream *stream_list_remove_head(transport *t, stream_list_id id);
static void stream_list_remove(transport *t, stream *s, stream_list_id id);
static void stream_list_add_tail(transport *t, stream *s, stream_list_id id);
static void stream_list_join(transport *t, stream *s, stream_list_id id);

static void cancel_stream_id(transport *t, gpr_uint32 id,
                             grpc_status_code local_status,
                             grpc_chttp2_error_code error_code, int send_rst);
static void cancel_stream(transport *t, stream *s,
                          grpc_status_code local_status,
                          grpc_chttp2_error_code error_code,
                          grpc_mdstr *optional_message, int send_rst);
static void finalize_cancellations(transport *t);
static stream *lookup_stream(transport *t, gpr_uint32 id);
static void remove_from_stream_map(transport *t, stream *s);
static void maybe_start_some_streams(transport *t);

static void become_skip_parser(transport *t);

static void recv_data(void *tp, gpr_slice *slices, size_t nslices,
                      grpc_endpoint_cb_status error);

static void schedule_cb(transport *t, op_closure closure, int success);
static void maybe_finish_read(transport *t, stream *s);
static void maybe_join_window_updates(transport *t, stream *s);
static void finish_reads(transport *t);
static void add_to_pollset_locked(transport *t, grpc_pollset *pollset);
static void perform_op_locked(transport *t, stream *s, grpc_transport_op *op);
static void add_metadata_batch(transport *t, stream *s);

static void flowctl_trace(transport *t, const char *flow, gpr_int32 window,
                          gpr_uint32 id, gpr_int32 delta) {
  gpr_log(GPR_DEBUG, "HTTP:FLOW:%p:%d:%s: %d + %d = %d", t, id, flow, window,
          delta, window + delta);
}

/*
 * CONSTRUCTION/DESTRUCTION/REFCOUNTING
 */

static void destruct_transport(transport *t) {
  size_t i;

  gpr_mu_lock(&t->mu);

  GPR_ASSERT(t->ep == NULL);

  gpr_slice_buffer_destroy(&t->outbuf);
  gpr_slice_buffer_destroy(&t->qbuf);
  grpc_chttp2_hpack_parser_destroy(&t->hpack_parser);
  grpc_chttp2_hpack_compressor_destroy(&t->hpack_compressor);
  grpc_chttp2_goaway_parser_destroy(&t->goaway_parser);

  grpc_mdstr_unref(t->str_grpc_timeout);

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    GPR_ASSERT(t->lists[i].head == NULL);
    GPR_ASSERT(t->lists[i].tail == NULL);
  }

  GPR_ASSERT(grpc_chttp2_stream_map_size(&t->stream_map) == 0);

  grpc_chttp2_stream_map_destroy(&t->stream_map);

  gpr_mu_unlock(&t->mu);
  gpr_mu_destroy(&t->mu);
  gpr_cv_destroy(&t->cv);

  /* callback remaining pings: they're not allowed to call into the transpot,
     and maybe they hold resources that need to be freed */
  for (i = 0; i < t->ping_count; i++) {
    t->pings[i].cb(t->pings[i].user_data);
  }
  gpr_free(t->pings);

  gpr_free(t->pending_callbacks.callbacks);
  gpr_free(t->executing_callbacks.callbacks);

  for (i = 0; i < t->num_pending_goaways; i++) {
    gpr_slice_unref(t->pending_goaways[i].debug);
  }
  gpr_free(t->pending_goaways);

  grpc_sopb_destroy(&t->nuke_later_sopb);

  grpc_mdctx_unref(t->metadata_context);

  gpr_free(t);
}

static void unref_transport(transport *t) {
  if (!gpr_unref(&t->refs)) return;
  destruct_transport(t);
}

static void ref_transport(transport *t) { gpr_ref(&t->refs); }

static void init_transport(transport *t, grpc_transport_setup_callback setup,
                           void *arg, const grpc_channel_args *channel_args,
                           grpc_endpoint *ep, gpr_slice *slices, size_t nslices,
                           grpc_mdctx *mdctx, int is_client) {
  size_t i;
  int j;
  grpc_transport_setup_result sr;

  GPR_ASSERT(strlen(CLIENT_CONNECT_STRING) == CLIENT_CONNECT_STRLEN);

  memset(t, 0, sizeof(*t));

  t->base.vtable = &vtable;
  t->ep = ep;
  /* one ref is for destroy, the other for when ep becomes NULL */
  gpr_ref_init(&t->refs, 2);
  gpr_mu_init(&t->mu);
  gpr_cv_init(&t->cv);
  grpc_mdctx_ref(mdctx);
  t->metadata_context = mdctx;
  t->str_grpc_timeout =
      grpc_mdstr_from_string(t->metadata_context, "grpc-timeout");
  t->reading = 1;
  t->error_state = ERROR_STATE_NONE;
  t->next_stream_id = is_client ? 1 : 2;
  t->is_client = is_client;
  t->outgoing_window = DEFAULT_WINDOW;
  t->incoming_window = DEFAULT_WINDOW;
  t->connection_window_target = DEFAULT_CONNECTION_WINDOW_TARGET;
  t->deframe_state = is_client ? DTS_FH_0 : DTS_CLIENT_PREFIX_0;
  t->ping_counter = gpr_now().tv_nsec;
  grpc_chttp2_hpack_compressor_init(&t->hpack_compressor, mdctx);
  grpc_chttp2_goaway_parser_init(&t->goaway_parser);
  gpr_slice_buffer_init(&t->outbuf);
  gpr_slice_buffer_init(&t->qbuf);
  grpc_sopb_init(&t->nuke_later_sopb);
  grpc_chttp2_hpack_parser_init(&t->hpack_parser, t->metadata_context);
  if (is_client) {
    gpr_slice_buffer_add(&t->qbuf,
                         gpr_slice_from_copied_string(CLIENT_CONNECT_STRING));
  }
  /* 8 is a random stab in the dark as to a good initial size: it's small enough
     that it shouldn't waste memory for infrequently used connections, yet
     large enough that the exponential growth should happen nicely when it's
     needed.
     TODO(ctiller): tune this */
  grpc_chttp2_stream_map_init(&t->stream_map, 8);

  /* copy in initial settings to all setting sets */
  for (i = 0; i < NUM_SETTING_SETS; i++) {
    for (j = 0; j < GRPC_CHTTP2_NUM_SETTINGS; j++) {
      t->settings[i][j] = grpc_chttp2_settings_parameters[j].default_value;
    }
  }
  t->dirtied_local_settings = 1;
  /* Hack: it's common for implementations to assume 65536 bytes initial send
     window -- this should by rights be 0 */
  t->force_send_settings = 1 << GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
  t->sent_local_settings = 0;

  /* configure http2 the way we like it */
  if (t->is_client) {
    push_setting(t, GRPC_CHTTP2_SETTINGS_ENABLE_PUSH, 0);
    push_setting(t, GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 0);
  }
  push_setting(t, GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, DEFAULT_WINDOW);

  if (channel_args) {
    for (i = 0; i < channel_args->num_args; i++) {
      if (0 ==
          strcmp(channel_args->args[i].key, GRPC_ARG_MAX_CONCURRENT_STREAMS)) {
        if (t->is_client) {
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
        } else if ((t->next_stream_id & 1) !=
                   (channel_args->args[i].value.integer & 1)) {
          gpr_log(GPR_ERROR, "%s: low bit must be %d on %s",
                  GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER, t->next_stream_id & 1,
                  t->is_client ? "client" : "server");
        } else {
          t->next_stream_id = channel_args->args[i].value.integer;
        }
      }
    }
  }

  gpr_mu_lock(&t->mu);
  t->calling_back = 1;
  ref_transport(t); /* matches unref at end of this function */
  gpr_mu_unlock(&t->mu);

  sr = setup(arg, &t->base, t->metadata_context);

  lock(t);
  t->cb = sr.callbacks;
  t->cb_user_data = sr.user_data;
  t->calling_back = 0;
  if (t->destroying) gpr_cv_signal(&t->cv);
  unlock(t);

  ref_transport(t); /* matches unref inside recv_data */
  recv_data(t, slices, nslices, GRPC_ENDPOINT_CB_OK);

  unref_transport(t);
}

static void destroy_transport(grpc_transport *gt) {
  transport *t = (transport *)gt;

  lock(t);
  t->destroying = 1;
  /* Wait for pending stuff to finish.
     We need to be not calling back to ensure that closed() gets a chance to
     trigger if needed during unlock() before we die.
     We need to be not writing as cancellation finalization may produce some
     callbacks that NEED to be made to close out some streams when t->writing
     becomes 0. */
  while (t->calling_back || t->writing) {
    gpr_cv_wait(&t->cv, &t->mu, gpr_inf_future);
  }
  drop_connection(t);
  unlock(t);

  /* The drop_connection() above puts the transport into an error state, and
     the follow-up unlock should then (as part of the cleanup work it does)
     ensure that cb is NULL, and therefore not call back anything further.
     This check validates this very subtle behavior.
     It's shutdown path, so I don't believe an extra lock pair is going to be
     problematic for performance. */
  lock(t);
  GPR_ASSERT(!t->cb);
  unlock(t);

  unref_transport(t);
}

static void close_transport(grpc_transport *gt) {
  transport *t = (transport *)gt;
  gpr_mu_lock(&t->mu);
  GPR_ASSERT(!t->closed);
  t->closed = 1;
  if (t->ep) {
    grpc_endpoint_shutdown(t->ep);
  }
  gpr_mu_unlock(&t->mu);
}

static void goaway(grpc_transport *gt, grpc_status_code status,
                   gpr_slice debug_data) {
  transport *t = (transport *)gt;
  lock(t);
  grpc_chttp2_goaway_append(t->last_incoming_stream_id,
                            grpc_chttp2_grpc_status_to_http2_error(status),
                            debug_data, &t->qbuf);
  unlock(t);
}

static int init_stream(grpc_transport *gt, grpc_stream *gs,
                       const void *server_data, grpc_transport_op *initial_op) {
  transport *t = (transport *)gt;
  stream *s = (stream *)gs;

  memset(s, 0, sizeof(*s));

  ref_transport(t);

  if (!server_data) {
    lock(t);
    s->id = 0;
    s->outgoing_window = 0;
    s->incoming_window = 0;
  } else {
    /* already locked */
    s->id = (gpr_uint32)(gpr_uintptr)server_data;
    s->outgoing_window =
        t->settings[PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    s->incoming_window =
        t->settings[SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    t->incoming_stream = s;
    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
  }

  s->incoming_deadline = gpr_inf_future;
  grpc_sopb_init(&s->writing_sopb);
  grpc_sopb_init(&s->callback_sopb);
  grpc_chttp2_data_parser_init(&s->parser);

  if (initial_op) perform_op_locked(t, s, initial_op);

  if (!server_data) {
    unlock(t);
  }

  return 0;
}

static void schedule_nuke_sopb(transport *t, grpc_stream_op_buffer *sopb) {
  grpc_sopb_append(&t->nuke_later_sopb, sopb->ops, sopb->nops);
  sopb->nops = 0;
}

static void destroy_stream(grpc_transport *gt, grpc_stream *gs) {
  transport *t = (transport *)gt;
  stream *s = (stream *)gs;
  size_t i;

  gpr_mu_lock(&t->mu);

  /* stop parsing if we're currently parsing this stream */
  if (t->deframe_state == DTS_FRAME && t->incoming_stream_id == s->id &&
      s->id != 0) {
    become_skip_parser(t);
  }

  for (i = 0; i < STREAM_LIST_COUNT; i++) {
    stream_list_remove(t, s, i);
  }
  remove_from_stream_map(t, s);

  gpr_mu_unlock(&t->mu);

  GPR_ASSERT(s->outgoing_sopb == NULL);
  GPR_ASSERT(s->incoming_sopb == NULL);
  grpc_sopb_destroy(&s->writing_sopb);
  grpc_sopb_destroy(&s->callback_sopb);
  grpc_chttp2_data_parser_destroy(&s->parser);
  for (i = 0; i < s->incoming_metadata_count; i++) {
    grpc_mdelem_unref(s->incoming_metadata[i].md);
  }
  gpr_free(s->incoming_metadata);
  gpr_free(s->old_incoming_metadata);

  unref_transport(t);
}

/*
 * LIST MANAGEMENT
 */

static int stream_list_empty(transport *t, stream_list_id id) {
  return t->lists[id].head == NULL;
}

static stream *stream_list_remove_head(transport *t, stream_list_id id) {
  stream *s = t->lists[id].head;
  if (s) {
    stream *new_head = s->links[id].next;
    GPR_ASSERT(s->included[id]);
    if (new_head) {
      t->lists[id].head = new_head;
      new_head->links[id].prev = NULL;
    } else {
      t->lists[id].head = NULL;
      t->lists[id].tail = NULL;
    }
    s->included[id] = 0;
  }
  return s;
}

static void stream_list_remove(transport *t, stream *s, stream_list_id id) {
  if (!s->included[id]) return;
  s->included[id] = 0;
  if (s->links[id].prev) {
    s->links[id].prev->links[id].next = s->links[id].next;
  } else {
    GPR_ASSERT(t->lists[id].head == s);
    t->lists[id].head = s->links[id].next;
  }
  if (s->links[id].next) {
    s->links[id].next->links[id].prev = s->links[id].prev;
  } else {
    t->lists[id].tail = s->links[id].prev;
  }
}

static void stream_list_add_tail(transport *t, stream *s, stream_list_id id) {
  stream *old_tail;
  GPR_ASSERT(!s->included[id]);
  old_tail = t->lists[id].tail;
  s->links[id].next = NULL;
  s->links[id].prev = old_tail;
  if (old_tail) {
    old_tail->links[id].next = s;
  } else {
    s->links[id].prev = NULL;
    t->lists[id].head = s;
  }
  t->lists[id].tail = s;
  s->included[id] = 1;
}

static void stream_list_join(transport *t, stream *s, stream_list_id id) {
  if (s->included[id]) {
    return;
  }
  stream_list_add_tail(t, s, id);
}

static void remove_from_stream_map(transport *t, stream *s) {
  if (s->id == 0) return;
  IF_TRACING(gpr_log(GPR_DEBUG, "HTTP:%s: Removing stream %d",
                     t->is_client ? "CLI" : "SVR", s->id));
  if (grpc_chttp2_stream_map_delete(&t->stream_map, s->id)) {
    maybe_start_some_streams(t);
  }
}

/*
 * LOCK MANAGEMENT
 */

/* We take a transport-global lock in response to calls coming in from above,
   and in response to data being received from below. New data to be written
   is always queued, as are callbacks to process data. During unlock() we
   check our todo lists and initiate callbacks and flush writes. */

static void lock(transport *t) { gpr_mu_lock(&t->mu); }

static void unlock(transport *t) {
  int start_write = 0;
  int perform_callbacks = 0;
  int call_closed = 0;
  int num_goaways = 0;
  int i;
  pending_goaway *goaways = NULL;
  grpc_endpoint *ep = t->ep;
  grpc_stream_op_buffer nuke_now;
  const grpc_transport_callbacks *cb = t->cb;

  GRPC_TIMER_BEGIN(GRPC_PTAG_HTTP2_UNLOCK, 0);

  grpc_sopb_init(&nuke_now);
  if (t->nuke_later_sopb.nops) {
    grpc_sopb_swap(&nuke_now, &t->nuke_later_sopb);
  }

  /* see if we need to trigger a write - and if so, get the data ready */
  if (ep && !t->writing) {
    t->writing = start_write = prepare_write(t);
    if (start_write) {
      ref_transport(t);
    }
  }

  if (!t->writing) {
    finalize_cancellations(t);
  }

  finish_reads(t);

  /* gather any callbacks that need to be made */
  if (!t->calling_back) {
    t->calling_back = perform_callbacks = prepare_callbacks(t);
    if (cb) {
      if (t->error_state == ERROR_STATE_SEEN && !t->writing) {
        call_closed = 1;
        t->calling_back = 1;
        t->cb = NULL; /* no more callbacks */
        t->error_state = ERROR_STATE_NOTIFIED;
      }
      if (t->num_pending_goaways) {
        goaways = t->pending_goaways;
        num_goaways = t->num_pending_goaways;
        t->pending_goaways = NULL;
        t->num_pending_goaways = 0;
        t->cap_pending_goaways = 0;
        t->calling_back = 1;
      }
    }
  }

  if (perform_callbacks || call_closed || num_goaways) {
    ref_transport(t);
  }

  /* finally unlock */
  gpr_mu_unlock(&t->mu);

  GRPC_TIMER_MARK(GRPC_PTAG_HTTP2_UNLOCK_CLEANUP, 0);

  /* perform some callbacks if necessary */
  for (i = 0; i < num_goaways; i++) {
    cb->goaway(t->cb_user_data, &t->base, goaways[i].status, goaways[i].debug);
  }

  if (perform_callbacks) {
    run_callbacks(t, cb);
  }

  if (call_closed) {
    call_cb_closed(t, cb);
  }

  /* write some bytes if necessary */
  if (start_write) {
    /* ultimately calls unref_transport(t); and clears t->writing */
    perform_write(t, ep);
  }

  if (perform_callbacks || call_closed || num_goaways) {
    lock(t);
    t->calling_back = 0;
    if (t->destroying) gpr_cv_signal(&t->cv);
    unlock(t);
    unref_transport(t);
  }

  grpc_sopb_destroy(&nuke_now);

  gpr_free(goaways);

  GRPC_TIMER_END(GRPC_PTAG_HTTP2_UNLOCK, 0);
}

/*
 * OUTPUT PROCESSING
 */

static void push_setting(transport *t, grpc_chttp2_setting_id id,
                         gpr_uint32 value) {
  const grpc_chttp2_setting_parameters *sp =
      &grpc_chttp2_settings_parameters[id];
  gpr_uint32 use_value = GPR_CLAMP(value, sp->min_value, sp->max_value);
  if (use_value != value) {
    gpr_log(GPR_INFO, "Requested parameter %s clamped from %d to %d", sp->name,
            value, use_value);
  }
  if (use_value != t->settings[LOCAL_SETTINGS][id]) {
    t->settings[LOCAL_SETTINGS][id] = use_value;
    t->dirtied_local_settings = 1;
  }
}

static int prepare_write(transport *t) {
  stream *s;
  gpr_uint32 window_delta;

  /* simple writes are queued to qbuf, and flushed here */
  gpr_slice_buffer_swap(&t->qbuf, &t->outbuf);
  GPR_ASSERT(t->qbuf.count == 0);

  if (t->dirtied_local_settings && !t->sent_local_settings) {
    gpr_slice_buffer_add(
        &t->outbuf, grpc_chttp2_settings_create(
                        t->settings[SENT_SETTINGS], t->settings[LOCAL_SETTINGS],
                        t->force_send_settings, GRPC_CHTTP2_NUM_SETTINGS));
    t->force_send_settings = 0;
    t->dirtied_local_settings = 0;
    t->sent_local_settings = 1;
  }

  /* for each stream that's become writable, frame it's data (according to
     available window sizes) and add to the output buffer */
  while (t->outgoing_window && (s = stream_list_remove_head(t, WRITABLE)) &&
         s->outgoing_window > 0) {
    window_delta = grpc_chttp2_preencode(
        s->outgoing_sopb->ops, &s->outgoing_sopb->nops,
        GPR_MIN(t->outgoing_window, s->outgoing_window), &s->writing_sopb);
    FLOWCTL_TRACE(t, t, outgoing, 0, -(gpr_int64)window_delta);
    FLOWCTL_TRACE(t, s, outgoing, s->id, -(gpr_int64)window_delta);
    t->outgoing_window -= window_delta;
    s->outgoing_window -= window_delta;

    if (s->write_state == WRITE_STATE_QUEUED_CLOSE &&
        s->outgoing_sopb->nops == 0) {
      s->send_closed = 1;
    }
    if (s->writing_sopb.nops > 0 || s->send_closed) {
      stream_list_join(t, s, WRITING);
    }

    /* we should either exhaust window or have no ops left, but not both */
    if (s->outgoing_sopb->nops == 0) {
      s->outgoing_sopb = NULL;
      schedule_cb(t, s->send_done_closure, 1);
    } else if (s->outgoing_window) {
      stream_list_add_tail(t, s, WRITABLE);
    }
  }

  /* for each stream that wants to update its window, add that window here */
  while ((s = stream_list_remove_head(t, WINDOW_UPDATE))) {
    window_delta =
        t->settings[LOCAL_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] -
        s->incoming_window;
    if (!s->read_closed && window_delta) {
      gpr_slice_buffer_add(
          &t->outbuf, grpc_chttp2_window_update_create(s->id, window_delta));
      FLOWCTL_TRACE(t, s, incoming, s->id, window_delta);
      s->incoming_window += window_delta;
    }
  }

  /* if the transport is ready to send a window update, do so here also */
  if (t->incoming_window < t->connection_window_target * 3 / 4) {
    window_delta = t->connection_window_target - t->incoming_window;
    gpr_slice_buffer_add(&t->outbuf,
                         grpc_chttp2_window_update_create(0, window_delta));
    FLOWCTL_TRACE(t, t, incoming, 0, window_delta);
    t->incoming_window += window_delta;
  }

  return t->outbuf.length > 0 || !stream_list_empty(t, WRITING);
}

static void finalize_outbuf(transport *t) {
  stream *s;

  while ((s = stream_list_remove_head(t, WRITING))) {
    grpc_chttp2_encode(s->writing_sopb.ops, s->writing_sopb.nops,
                       s->send_closed, s->id, &t->hpack_compressor, &t->outbuf);
    s->writing_sopb.nops = 0;
    if (s->send_closed) {
      stream_list_join(t, s, WRITTEN_CLOSED);
    }
  }
}

static void finish_write_common(transport *t, int success) {
  stream *s;

  lock(t);
  if (!success) {
    drop_connection(t);
  }
  while ((s = stream_list_remove_head(t, WRITTEN_CLOSED))) {
    s->write_state = WRITE_STATE_SENT_CLOSE;
    if (1||!s->cancelled) {
      maybe_finish_read(t, s);
    }
  }
  t->outbuf.count = 0;
  t->outbuf.length = 0;
  /* leave the writing flag up on shutdown to prevent further writes in unlock()
     from starting */
  t->writing = 0;
  if (t->destroying) {
    gpr_cv_signal(&t->cv);
  }
  if (!t->reading) {
    grpc_endpoint_destroy(t->ep);
    t->ep = NULL;
    unref_transport(t); /* safe because we'll still have the ref for write */
  }
  unlock(t);

  unref_transport(t);
}

static void finish_write(void *tp, grpc_endpoint_cb_status error) {
  transport *t = tp;
  finish_write_common(t, error == GRPC_ENDPOINT_CB_OK);
}

static void perform_write(transport *t, grpc_endpoint *ep) {
  finalize_outbuf(t);

  GPR_ASSERT(t->outbuf.count > 0);

  switch (grpc_endpoint_write(ep, t->outbuf.slices, t->outbuf.count,
                              finish_write, t)) {
    case GRPC_ENDPOINT_WRITE_DONE:
      finish_write_common(t, 1);
      break;
    case GRPC_ENDPOINT_WRITE_ERROR:
      finish_write_common(t, 0);
      break;
    case GRPC_ENDPOINT_WRITE_PENDING:
      break;
  }
}

static void add_goaway(transport *t, gpr_uint32 goaway_error, gpr_slice goaway_text) {
  if (t->num_pending_goaways == t->cap_pending_goaways) {
    t->cap_pending_goaways = GPR_MAX(1, t->cap_pending_goaways * 2);
    t->pending_goaways =
        gpr_realloc(t->pending_goaways,
                    sizeof(pending_goaway) * t->cap_pending_goaways);
  }
  t->pending_goaways[t->num_pending_goaways].status =
      grpc_chttp2_http2_error_to_grpc_status(goaway_error);
  t->pending_goaways[t->num_pending_goaways].debug = goaway_text;
  t->num_pending_goaways++;
}


static void maybe_start_some_streams(transport *t) {
  /* start streams where we have free stream ids and free concurrency */
  while (
      t->next_stream_id <= MAX_CLIENT_STREAM_ID &&
      grpc_chttp2_stream_map_size(&t->stream_map) <
      t->settings[PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS]) {
    stream *s = stream_list_remove_head(t, WAITING_FOR_CONCURRENCY);
    if (!s) return;

    IF_TRACING(gpr_log(GPR_DEBUG, "HTTP:%s: Allocating new stream %p to id %d",
                       t->is_client ? "CLI" : "SVR", s, t->next_stream_id));

    if (t->next_stream_id == MAX_CLIENT_STREAM_ID) {
      add_goaway(t, GRPC_CHTTP2_NO_ERROR, gpr_slice_from_copied_string("Exceeded sequence number limit"));
    }

    GPR_ASSERT(s->id == 0);
    s->id = t->next_stream_id;
    t->next_stream_id += 2;
    s->outgoing_window =
        t->settings[PEER_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    s->incoming_window =
        t->settings[SENT_SETTINGS][GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE];
    grpc_chttp2_stream_map_add(&t->stream_map, s->id, s);
    stream_list_join(t, s, WRITABLE);
  }
  /* cancel out streams that will never be started */
  while (t->next_stream_id > MAX_CLIENT_STREAM_ID) {
    stream *s = stream_list_remove_head(t, WAITING_FOR_CONCURRENCY);
    if (!s) return;

    cancel_stream(t, s, GRPC_STATUS_UNAVAILABLE, grpc_chttp2_grpc_status_to_http2_error(GRPC_STATUS_UNAVAILABLE), NULL, 0);
  }
}

static void perform_op_locked(transport *t, stream *s, grpc_transport_op *op) {
  if (op->cancel_with_status != GRPC_STATUS_OK) {
    cancel_stream(
        t, s, op->cancel_with_status,
        grpc_chttp2_grpc_status_to_http2_error(op->cancel_with_status),
        op->cancel_message, 1);
  }

  if (op->send_ops) {
    GPR_ASSERT(s->outgoing_sopb == NULL);
    s->send_done_closure.cb = op->on_done_send;
    s->send_done_closure.user_data = op->send_user_data;
    if (!s->cancelled) {
      s->outgoing_sopb = op->send_ops;
      if (op->is_last_send && s->write_state == WRITE_STATE_OPEN) {
        s->write_state = WRITE_STATE_QUEUED_CLOSE;
      }
      if (s->id == 0) {
        IF_TRACING(gpr_log(GPR_DEBUG,
                           "HTTP:%s: New stream %p waiting for concurrency",
                           t->is_client ? "CLI" : "SVR", s));
        stream_list_join(t, s, WAITING_FOR_CONCURRENCY);
        maybe_start_some_streams(t);
      } else if (s->outgoing_window > 0) {
        stream_list_join(t, s, WRITABLE);
      }
    } else {
      schedule_nuke_sopb(t, op->send_ops);
      schedule_cb(t, s->send_done_closure, 0);
    }
  }

  if (op->recv_ops) {
    GPR_ASSERT(s->incoming_sopb == NULL);
    s->recv_done_closure.cb = op->on_done_recv;
    s->recv_done_closure.user_data = op->recv_user_data;
    s->incoming_sopb = op->recv_ops;
    s->incoming_sopb->nops = 0;
    s->publish_state = op->recv_state;
    gpr_free(s->old_incoming_metadata);
    s->old_incoming_metadata = NULL;
    maybe_finish_read(t, s);
    maybe_join_window_updates(t, s);
  }

  if (op->bind_pollset) {
    add_to_pollset_locked(t, op->bind_pollset);
  }
}

static void perform_op(grpc_transport *gt, grpc_stream *gs,
                       grpc_transport_op *op) {
  transport *t = (transport *)gt;
  stream *s = (stream *)gs;

  lock(t);
  perform_op_locked(t, s, op);
  unlock(t);
}

static void send_ping(grpc_transport *gt, void (*cb)(void *user_data),
                      void *user_data) {
  transport *t = (transport *)gt;
  outstanding_ping *p;

  lock(t);
  if (t->ping_capacity == t->ping_count) {
    t->ping_capacity = GPR_MAX(1, t->ping_capacity * 3 / 2);
    t->pings =
        gpr_realloc(t->pings, sizeof(outstanding_ping) * t->ping_capacity);
  }
  p = &t->pings[t->ping_count++];
  p->id[0] = (t->ping_counter >> 56) & 0xff;
  p->id[1] = (t->ping_counter >> 48) & 0xff;
  p->id[2] = (t->ping_counter >> 40) & 0xff;
  p->id[3] = (t->ping_counter >> 32) & 0xff;
  p->id[4] = (t->ping_counter >> 24) & 0xff;
  p->id[5] = (t->ping_counter >> 16) & 0xff;
  p->id[6] = (t->ping_counter >> 8) & 0xff;
  p->id[7] = t->ping_counter & 0xff;
  p->cb = cb;
  p->user_data = user_data;
  gpr_slice_buffer_add(&t->qbuf, grpc_chttp2_ping_create(0, p->id));
  unlock(t);
}

/*
 * INPUT PROCESSING
 */

static void finalize_cancellations(transport *t) {
  stream *s;

  while ((s = stream_list_remove_head(t, CANCELLED))) {
    s->read_closed = 1;
    s->write_state = WRITE_STATE_SENT_CLOSE;
    maybe_finish_read(t, s);
  }
}

static void add_incoming_metadata(transport *t, stream *s, grpc_mdelem *elem) {
  if (s->incoming_metadata_capacity == s->incoming_metadata_count) {
    s->incoming_metadata_capacity =
        GPR_MAX(8, 2 * s->incoming_metadata_capacity);
    s->incoming_metadata =
        gpr_realloc(s->incoming_metadata, sizeof(*s->incoming_metadata) *
                                              s->incoming_metadata_capacity);
  }
  s->incoming_metadata[s->incoming_metadata_count++].md = elem;
}

static void cancel_stream_inner(transport *t, stream *s, gpr_uint32 id,
                                grpc_status_code local_status,
                                grpc_chttp2_error_code error_code,
                                grpc_mdstr *optional_message, int send_rst) {
  int had_outgoing;
  char buffer[GPR_LTOA_MIN_BUFSIZE];

  if (s) {
    /* clear out any unreported input & output: nobody cares anymore */
    had_outgoing = s->outgoing_sopb && s->outgoing_sopb->nops != 0;
    schedule_nuke_sopb(t, &s->parser.incoming_sopb);
    if (s->outgoing_sopb) {
      schedule_nuke_sopb(t, s->outgoing_sopb);
      s->outgoing_sopb = NULL;
      stream_list_remove(t, s, WRITABLE);
      schedule_cb(t, s->send_done_closure, 0);
    }
    if (s->cancelled) {
      send_rst = 0;
    } else if (!s->read_closed || s->write_state != WRITE_STATE_SENT_CLOSE ||
               had_outgoing) {
      s->cancelled = 1;
      stream_list_join(t, s, CANCELLED);

      gpr_ltoa(local_status, buffer);
      add_incoming_metadata(
          t, s,
          grpc_mdelem_from_strings(t->metadata_context, "grpc-status", buffer));
      if (!optional_message) {
        switch (local_status) {
          case GRPC_STATUS_CANCELLED:
            add_incoming_metadata(
                t, s, grpc_mdelem_from_strings(t->metadata_context,
                                               "grpc-message", "Cancelled"));
            break;
          default:
            break;
        }
      } else {
        add_incoming_metadata(
            t, s,
            grpc_mdelem_from_metadata_strings(
                t->metadata_context,
                grpc_mdstr_from_string(t->metadata_context, "grpc-message"),
                grpc_mdstr_ref(optional_message)));
      }
      add_metadata_batch(t, s);
      maybe_finish_read(t, s);
    }
  }
  if (!id) send_rst = 0;
  if (send_rst) {
    gpr_slice_buffer_add(&t->qbuf,
                         grpc_chttp2_rst_stream_create(id, error_code));
  }
  if (optional_message) {
    grpc_mdstr_unref(optional_message);
  }
}

static void cancel_stream_id(transport *t, gpr_uint32 id,
                             grpc_status_code local_status,
                             grpc_chttp2_error_code error_code, int send_rst) {
  cancel_stream_inner(t, lookup_stream(t, id), id, local_status, error_code,
                      NULL, send_rst);
}

static void cancel_stream(transport *t, stream *s,
                          grpc_status_code local_status,
                          grpc_chttp2_error_code error_code,
                          grpc_mdstr *optional_message, int send_rst) {
  cancel_stream_inner(t, s, s->id, local_status, error_code, optional_message,
                      send_rst);
}

static void cancel_stream_cb(void *user_data, gpr_uint32 id, void *stream) {
  cancel_stream(user_data, stream, GRPC_STATUS_UNAVAILABLE,
                GRPC_CHTTP2_INTERNAL_ERROR, NULL, 0);
}

static void end_all_the_calls(transport *t) {
  grpc_chttp2_stream_map_for_each(&t->stream_map, cancel_stream_cb, t);
}

static void drop_connection(transport *t) {
  if (t->error_state == ERROR_STATE_NONE) {
    t->error_state = ERROR_STATE_SEEN;
  }
  end_all_the_calls(t);
}

static void maybe_finish_read(transport *t, stream *s) {
  if (s->incoming_sopb) {
    stream_list_join(t, s, FINISHED_READ_OP);
  }
}

static void maybe_join_window_updates(transport *t, stream *s) {
  if (s->incoming_sopb != NULL &&
      s->incoming_window <
          t->settings[LOCAL_SETTINGS]
                     [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE] *
              3 / 4) {
    stream_list_join(t, s, WINDOW_UPDATE);
  }
}

static grpc_chttp2_parse_error update_incoming_window(transport *t, stream *s) {
  if (t->incoming_frame_size > t->incoming_window) {
    gpr_log(GPR_ERROR, "frame of size %d overflows incoming window of %d",
            t->incoming_frame_size, t->incoming_window);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }

  if (t->incoming_frame_size > s->incoming_window) {
    gpr_log(GPR_ERROR, "frame of size %d overflows incoming window of %d",
            t->incoming_frame_size, s->incoming_window);
    return GRPC_CHTTP2_CONNECTION_ERROR;
  }

  FLOWCTL_TRACE(t, t, incoming, 0, -(gpr_int64)t->incoming_frame_size);
  FLOWCTL_TRACE(t, s, incoming, s->id, -(gpr_int64)t->incoming_frame_size);
  t->incoming_window -= t->incoming_frame_size;
  s->incoming_window -= t->incoming_frame_size;

  /* if the stream incoming window is getting low, schedule an update */
  maybe_join_window_updates(t, s);

  return GRPC_CHTTP2_PARSE_OK;
}

static stream *lookup_stream(transport *t, gpr_uint32 id) {
  return grpc_chttp2_stream_map_find(&t->stream_map, id);
}

static grpc_chttp2_parse_error skip_parser(void *parser,
                                           grpc_chttp2_parse_state *st,
                                           gpr_slice slice, int is_last) {
  return GRPC_CHTTP2_PARSE_OK;
}

static void skip_header(void *tp, grpc_mdelem *md) { grpc_mdelem_unref(md); }

static int init_skip_frame(transport *t, int is_header) {
  if (is_header) {
    int is_eoh = t->expect_continuation_stream_id != 0;
    t->parser = grpc_chttp2_header_parser_parse;
    t->parser_data = &t->hpack_parser;
    t->hpack_parser.on_header = skip_header;
    t->hpack_parser.on_header_user_data = NULL;
    t->hpack_parser.is_boundary = is_eoh;
    t->hpack_parser.is_eof = is_eoh ? t->header_eof : 0;
  } else {
    t->parser = skip_parser;
  }
  return 1;
}

static void become_skip_parser(transport *t) {
  init_skip_frame(t, t->parser == grpc_chttp2_header_parser_parse);
}

static int init_data_frame_parser(transport *t) {
  stream *s = lookup_stream(t, t->incoming_stream_id);
  grpc_chttp2_parse_error err = GRPC_CHTTP2_PARSE_OK;
  if (!s || s->read_closed) return init_skip_frame(t, 0);
  if (err == GRPC_CHTTP2_PARSE_OK) {
    err = update_incoming_window(t, s);
  }
  if (err == GRPC_CHTTP2_PARSE_OK) {
    err = grpc_chttp2_data_parser_begin_frame(&s->parser,
                                              t->incoming_frame_flags);
  }
  switch (err) {
    case GRPC_CHTTP2_PARSE_OK:
      t->incoming_stream = s;
      t->parser = grpc_chttp2_data_parser_parse;
      t->parser_data = &s->parser;
      return 1;
    case GRPC_CHTTP2_STREAM_ERROR:
      cancel_stream(t, s, grpc_chttp2_http2_error_to_grpc_status(
                              GRPC_CHTTP2_INTERNAL_ERROR),
                    GRPC_CHTTP2_INTERNAL_ERROR, NULL, 1);
      return init_skip_frame(t, 0);
    case GRPC_CHTTP2_CONNECTION_ERROR:
      drop_connection(t);
      return 0;
  }
  gpr_log(GPR_ERROR, "should never reach here");
  abort();
  return 0;
}

static void free_timeout(void *p) { gpr_free(p); }

static void on_header(void *tp, grpc_mdelem *md) {
  transport *t = tp;
  stream *s = t->incoming_stream;

  GPR_ASSERT(s);

  IF_TRACING(gpr_log(
      GPR_INFO, "HTTP:%d:%s:HDR: %s: %s", s->id, t->is_client ? "CLI" : "SVR",
      grpc_mdstr_as_c_string(md->key), grpc_mdstr_as_c_string(md->value)));

  if (md->key == t->str_grpc_timeout) {
    gpr_timespec *cached_timeout = grpc_mdelem_get_user_data(md, free_timeout);
    if (!cached_timeout) {
      /* not already parsed: parse it now, and store the result away */
      cached_timeout = gpr_malloc(sizeof(gpr_timespec));
      if (!grpc_chttp2_decode_timeout(grpc_mdstr_as_c_string(md->value),
                                      cached_timeout)) {
        gpr_log(GPR_ERROR, "Ignoring bad timeout value '%s'",
                grpc_mdstr_as_c_string(md->value));
        *cached_timeout = gpr_inf_future;
      }
      grpc_mdelem_set_user_data(md, free_timeout, cached_timeout);
    }
    s->incoming_deadline = gpr_time_add(gpr_now(), *cached_timeout);
    grpc_mdelem_unref(md);
  } else {
    add_incoming_metadata(t, s, md);
  }
  maybe_finish_read(t, s);
}

static int init_header_frame_parser(transport *t, int is_continuation) {
  int is_eoh =
      (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_HEADERS) != 0;
  stream *s;

  if (is_eoh) {
    t->expect_continuation_stream_id = 0;
  } else {
    t->expect_continuation_stream_id = t->incoming_stream_id;
  }

  if (!is_continuation) {
    t->header_eof =
        (t->incoming_frame_flags & GRPC_CHTTP2_DATA_FLAG_END_STREAM) != 0;
  }

  /* could be a new stream or an existing stream */
  s = lookup_stream(t, t->incoming_stream_id);
  if (!s) {
    if (is_continuation) {
      gpr_log(GPR_ERROR, "stream disbanded before CONTINUATION received");
      return init_skip_frame(t, 1);
    }
    if (t->is_client) {
      if ((t->incoming_stream_id & 1) &&
          t->incoming_stream_id < t->next_stream_id) {
        /* this is an old (probably cancelled) stream */
      } else {
        gpr_log(GPR_ERROR, "ignoring new stream creation on client");
      }
      return init_skip_frame(t, 1);
    } else if (t->last_incoming_stream_id > t->incoming_stream_id) {
      gpr_log(GPR_ERROR,
              "ignoring out of order new stream request on server; last stream "
              "id=%d, new stream id=%d",
              t->last_incoming_stream_id, t->incoming_stream_id);
      return init_skip_frame(t, 1);
    } else if ((t->incoming_stream_id & 1) == 0) {
      gpr_log(GPR_ERROR, "ignoring stream with non-client generated index %d", t->incoming_stream_id);
      return init_skip_frame(t, 1);
    }
    t->incoming_stream = NULL;
    /* if stream is accepted, we set incoming_stream in init_stream */
    t->cb->accept_stream(t->cb_user_data, &t->base,
                         (void *)(gpr_uintptr)t->incoming_stream_id);
    s = t->incoming_stream;
    if (!s) {
      gpr_log(GPR_ERROR, "stream not accepted");
      return init_skip_frame(t, 1);
    }
  } else {
    t->incoming_stream = s;
  }
  if (t->incoming_stream->read_closed) {
    gpr_log(GPR_ERROR, "skipping already closed stream header");
    t->incoming_stream = NULL;
    return init_skip_frame(t, 1);
  }
  t->parser = grpc_chttp2_header_parser_parse;
  t->parser_data = &t->hpack_parser;
  t->hpack_parser.on_header = on_header;
  t->hpack_parser.on_header_user_data = t;
  t->hpack_parser.is_boundary = is_eoh;
  t->hpack_parser.is_eof = is_eoh ? t->header_eof : 0;
  if (!is_continuation &&
      (t->incoming_frame_flags & GRPC_CHTTP2_FLAG_HAS_PRIORITY)) {
    grpc_chttp2_hpack_parser_set_has_priority(&t->hpack_parser);
  }
  return 1;
}

static int init_window_update_frame_parser(transport *t) {
  int ok = GRPC_CHTTP2_PARSE_OK == grpc_chttp2_window_update_parser_begin_frame(
                                       &t->simple_parsers.window_update,
                                       t->incoming_frame_size,
                                       t->incoming_frame_flags);
  if (!ok) {
    drop_connection(t);
  }
  t->parser = grpc_chttp2_window_update_parser_parse;
  t->parser_data = &t->simple_parsers.window_update;
  return ok;
}

static int init_ping_parser(transport *t) {
  int ok = GRPC_CHTTP2_PARSE_OK ==
           grpc_chttp2_ping_parser_begin_frame(&t->simple_parsers.ping,
                                               t->incoming_frame_size,
                                               t->incoming_frame_flags);
  if (!ok) {
    drop_connection(t);
  }
  t->parser = grpc_chttp2_ping_parser_parse;
  t->parser_data = &t->simple_parsers.ping;
  return ok;
}

static int init_goaway_parser(transport *t) {
  int ok =
      GRPC_CHTTP2_PARSE_OK ==
      grpc_chttp2_goaway_parser_begin_frame(
          &t->goaway_parser, t->incoming_frame_size, t->incoming_frame_flags);
  if (!ok) {
    drop_connection(t);
  }
  t->parser = grpc_chttp2_goaway_parser_parse;
  t->parser_data = &t->goaway_parser;
  return ok;
}

static int init_settings_frame_parser(transport *t) {
  int ok = GRPC_CHTTP2_PARSE_OK ==
           grpc_chttp2_settings_parser_begin_frame(
               &t->simple_parsers.settings, t->incoming_frame_size,
               t->incoming_frame_flags, t->settings[PEER_SETTINGS]);
  if (!ok) {
    drop_connection(t);
  }
  if (t->incoming_frame_flags & GRPC_CHTTP2_FLAG_ACK) {
    memcpy(t->settings[ACKED_SETTINGS], t->settings[SENT_SETTINGS],
           GRPC_CHTTP2_NUM_SETTINGS * sizeof(gpr_uint32));
  }
  t->parser = grpc_chttp2_settings_parser_parse;
  t->parser_data = &t->simple_parsers.settings;
  return ok;
}

static int init_frame_parser(transport *t) {
  if (t->expect_continuation_stream_id != 0) {
    if (t->incoming_frame_type != GRPC_CHTTP2_FRAME_CONTINUATION) {
      gpr_log(GPR_ERROR, "Expected CONTINUATION frame, got frame type %02x",
              t->incoming_frame_type);
      return 0;
    }
    if (t->expect_continuation_stream_id != t->incoming_stream_id) {
      gpr_log(GPR_ERROR,
              "Expected CONTINUATION frame for stream %08x, got stream %08x",
              t->expect_continuation_stream_id, t->incoming_stream_id);
      return 0;
    }
    return init_header_frame_parser(t, 1);
  }
  switch (t->incoming_frame_type) {
    case GRPC_CHTTP2_FRAME_DATA:
      return init_data_frame_parser(t);
    case GRPC_CHTTP2_FRAME_HEADER:
      return init_header_frame_parser(t, 0);
    case GRPC_CHTTP2_FRAME_CONTINUATION:
      gpr_log(GPR_ERROR, "Unexpected CONTINUATION frame");
      return 0;
    case GRPC_CHTTP2_FRAME_RST_STREAM:
      /* TODO(ctiller): actually parse the reason */
      cancel_stream_id(
          t, t->incoming_stream_id,
          grpc_chttp2_http2_error_to_grpc_status(GRPC_CHTTP2_CANCEL),
          GRPC_CHTTP2_CANCEL, 0);
      return init_skip_frame(t, 0);
    case GRPC_CHTTP2_FRAME_SETTINGS:
      return init_settings_frame_parser(t);
    case GRPC_CHTTP2_FRAME_WINDOW_UPDATE:
      return init_window_update_frame_parser(t);
    case GRPC_CHTTP2_FRAME_PING:
      return init_ping_parser(t);
    case GRPC_CHTTP2_FRAME_GOAWAY:
      return init_goaway_parser(t);
    default:
      gpr_log(GPR_ERROR, "Unknown frame type %02x", t->incoming_frame_type);
      return init_skip_frame(t, 0);
  }
}

static int is_window_update_legal(gpr_int64 window_update, gpr_int64 window) {
  return window + window_update < MAX_WINDOW;
}

static void add_metadata_batch(transport *t, stream *s) {
  grpc_metadata_batch b;

  b.list.head = NULL;
  /* Store away the last element of the list, so that in patch_metadata_ops
     we can reconstitute the list.
     We can't do list building here as later incoming metadata may reallocate
     the underlying array. */
  b.list.tail = (void*)(gpr_intptr)s->incoming_metadata_count;
  b.garbage.head = b.garbage.tail = NULL;
  b.deadline = s->incoming_deadline;
  s->incoming_deadline = gpr_inf_future;

  grpc_sopb_add_metadata(&s->parser.incoming_sopb, b);
}

static int parse_frame_slice(transport *t, gpr_slice slice, int is_last) {
  grpc_chttp2_parse_state st;
  size_t i;
  memset(&st, 0, sizeof(st));
  switch (t->parser(t->parser_data, &st, slice, is_last)) {
    case GRPC_CHTTP2_PARSE_OK:
      if (st.end_of_stream) {
        t->incoming_stream->read_closed = 1;
        maybe_finish_read(t, t->incoming_stream);
      }
      if (st.need_flush_reads) {
        maybe_finish_read(t, t->incoming_stream);
      }
      if (st.metadata_boundary) {
        add_metadata_batch(t, t->incoming_stream);
        maybe_finish_read(t, t->incoming_stream);
      }
      if (st.ack_settings) {
        gpr_slice_buffer_add(&t->qbuf, grpc_chttp2_settings_ack_create());
        maybe_start_some_streams(t);
      }
      if (st.send_ping_ack) {
        gpr_slice_buffer_add(
            &t->qbuf,
            grpc_chttp2_ping_create(1, t->simple_parsers.ping.opaque_8bytes));
      }
      if (st.goaway) {
        add_goaway(t, st.goaway_error, st.goaway_text);
      }
      if (st.process_ping_reply) {
        for (i = 0; i < t->ping_count; i++) {
          if (0 ==
              memcmp(t->pings[i].id, t->simple_parsers.ping.opaque_8bytes, 8)) {
            t->pings[i].cb(t->pings[i].user_data);
            memmove(&t->pings[i], &t->pings[i + 1],
                    (t->ping_count - i - 1) * sizeof(outstanding_ping));
            t->ping_count--;
            break;
          }
        }
      }
      if (st.initial_window_update) {
        for (i = 0; i < t->stream_map.count; i++) {
          stream *s = (stream *)(t->stream_map.values[i]);
          int was_window_empty = s->outgoing_window <= 0;
          FLOWCTL_TRACE(t, s, outgoing, s->id, st.initial_window_update);
          s->outgoing_window += st.initial_window_update;
          if (was_window_empty && s->outgoing_window > 0 && s->outgoing_sopb &&
              s->outgoing_sopb->nops > 0) {
            stream_list_join(t, s, WRITABLE);
          }
        }
      }
      if (st.window_update) {
        if (t->incoming_stream_id) {
          /* if there was a stream id, this is for some stream */
          stream *s = lookup_stream(t, t->incoming_stream_id);
          if (s) {
            int was_window_empty = s->outgoing_window <= 0;
            if (!is_window_update_legal(st.window_update, s->outgoing_window)) {
              cancel_stream(t, s, grpc_chttp2_http2_error_to_grpc_status(
                                      GRPC_CHTTP2_FLOW_CONTROL_ERROR),
                            GRPC_CHTTP2_FLOW_CONTROL_ERROR, NULL, 1);
            } else {
              FLOWCTL_TRACE(t, s, outgoing, s->id, st.window_update);
              s->outgoing_window += st.window_update;
              /* if this window update makes outgoing ops writable again,
                 flag that */
              if (was_window_empty && s->outgoing_sopb &&
                  s->outgoing_sopb->nops > 0) {
                stream_list_join(t, s, WRITABLE);
              }
            }
          }
        } else {
          /* transport level window update */
          if (!is_window_update_legal(st.window_update, t->outgoing_window)) {
            drop_connection(t);
          } else {
            FLOWCTL_TRACE(t, t, outgoing, 0, st.window_update);
            t->outgoing_window += st.window_update;
          }
        }
      }
      return 1;
    case GRPC_CHTTP2_STREAM_ERROR:
      become_skip_parser(t);
      cancel_stream_id(
          t, t->incoming_stream_id,
          grpc_chttp2_http2_error_to_grpc_status(GRPC_CHTTP2_INTERNAL_ERROR),
          GRPC_CHTTP2_INTERNAL_ERROR, 1);
      return 1;
    case GRPC_CHTTP2_CONNECTION_ERROR:
      drop_connection(t);
      return 0;
  }
  gpr_log(GPR_ERROR, "should never reach here");
  abort();
  return 0;
}

static int process_read(transport *t, gpr_slice slice) {
  gpr_uint8 *beg = GPR_SLICE_START_PTR(slice);
  gpr_uint8 *end = GPR_SLICE_END_PTR(slice);
  gpr_uint8 *cur = beg;

  if (cur == end) return 1;

  switch (t->deframe_state) {
    case DTS_CLIENT_PREFIX_0:
    case DTS_CLIENT_PREFIX_1:
    case DTS_CLIENT_PREFIX_2:
    case DTS_CLIENT_PREFIX_3:
    case DTS_CLIENT_PREFIX_4:
    case DTS_CLIENT_PREFIX_5:
    case DTS_CLIENT_PREFIX_6:
    case DTS_CLIENT_PREFIX_7:
    case DTS_CLIENT_PREFIX_8:
    case DTS_CLIENT_PREFIX_9:
    case DTS_CLIENT_PREFIX_10:
    case DTS_CLIENT_PREFIX_11:
    case DTS_CLIENT_PREFIX_12:
    case DTS_CLIENT_PREFIX_13:
    case DTS_CLIENT_PREFIX_14:
    case DTS_CLIENT_PREFIX_15:
    case DTS_CLIENT_PREFIX_16:
    case DTS_CLIENT_PREFIX_17:
    case DTS_CLIENT_PREFIX_18:
    case DTS_CLIENT_PREFIX_19:
    case DTS_CLIENT_PREFIX_20:
    case DTS_CLIENT_PREFIX_21:
    case DTS_CLIENT_PREFIX_22:
    case DTS_CLIENT_PREFIX_23:
      while (cur != end && t->deframe_state != DTS_FH_0) {
        if (*cur != CLIENT_CONNECT_STRING[t->deframe_state]) {
          gpr_log(GPR_ERROR,
                  "Connect string mismatch: expected '%c' (%d) got '%c' (%d) "
                  "at byte %d",
                  CLIENT_CONNECT_STRING[t->deframe_state],
                  (int)(gpr_uint8)CLIENT_CONNECT_STRING[t->deframe_state], *cur,
                  (int)*cur, t->deframe_state);
          drop_connection(t);
          return 0;
        }
        ++cur;
        ++t->deframe_state;
      }
      if (cur == end) {
        return 1;
      }
    /* fallthrough */
    dts_fh_0:
    case DTS_FH_0:
      GPR_ASSERT(cur < end);
      t->incoming_frame_size = ((gpr_uint32)*cur) << 16;
      if (++cur == end) {
        t->deframe_state = DTS_FH_1;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_1:
      GPR_ASSERT(cur < end);
      t->incoming_frame_size |= ((gpr_uint32)*cur) << 8;
      if (++cur == end) {
        t->deframe_state = DTS_FH_2;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_2:
      GPR_ASSERT(cur < end);
      t->incoming_frame_size |= *cur;
      if (++cur == end) {
        t->deframe_state = DTS_FH_3;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_3:
      GPR_ASSERT(cur < end);
      t->incoming_frame_type = *cur;
      if (++cur == end) {
        t->deframe_state = DTS_FH_4;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_4:
      GPR_ASSERT(cur < end);
      t->incoming_frame_flags = *cur;
      if (++cur == end) {
        t->deframe_state = DTS_FH_5;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_5:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id = (((gpr_uint32)*cur) & 0x7f) << 24;
      if (++cur == end) {
        t->deframe_state = DTS_FH_6;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_6:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id |= ((gpr_uint32)*cur) << 16;
      if (++cur == end) {
        t->deframe_state = DTS_FH_7;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_7:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id |= ((gpr_uint32)*cur) << 8;
      if (++cur == end) {
        t->deframe_state = DTS_FH_8;
        return 1;
      }
    /* fallthrough */
    case DTS_FH_8:
      GPR_ASSERT(cur < end);
      t->incoming_stream_id |= ((gpr_uint32)*cur);
      t->deframe_state = DTS_FRAME;
      if (!init_frame_parser(t)) {
        return 0;
      }
      /* t->last_incoming_stream_id is used as last-stream-id when
         sending GOAWAY frame.
         https://tools.ietf.org/html/draft-ietf-httpbis-http2-17#section-6.8
         says that last-stream-id is peer-initiated stream ID.  So,
         since we don't have server pushed streams, client should send
         GOAWAY last-stream-id=0 in this case. */
      if (!t->is_client) {
        t->last_incoming_stream_id = t->incoming_stream_id;
      }
      if (t->incoming_frame_size == 0) {
        if (!parse_frame_slice(t, gpr_empty_slice(), 1)) {
          return 0;
        }
        if (++cur == end) {
          t->deframe_state = DTS_FH_0;
          return 1;
        }
        goto dts_fh_0; /* loop */
      }
      if (++cur == end) {
        return 1;
      }
    /* fallthrough */
    case DTS_FRAME:
      GPR_ASSERT(cur < end);
      if ((gpr_uint32)(end - cur) == t->incoming_frame_size) {
        if (!parse_frame_slice(
                t, gpr_slice_sub_no_ref(slice, cur - beg, end - beg), 1)) {
          return 0;
        }
        t->deframe_state = DTS_FH_0;
        return 1;
      } else if ((gpr_uint32)(end - cur) > t->incoming_frame_size) {
        if (!parse_frame_slice(
                t, gpr_slice_sub_no_ref(slice, cur - beg,
                                        cur + t->incoming_frame_size - beg),
                1)) {
          return 0;
        }
        cur += t->incoming_frame_size;
        goto dts_fh_0; /* loop */
      } else {
        if (!parse_frame_slice(
                t, gpr_slice_sub_no_ref(slice, cur - beg, end - beg), 0)) {
          return 0;
        }
        t->incoming_frame_size -= (end - cur);
        return 1;
      }
      gpr_log(GPR_ERROR, "should never reach here");
      abort();
  }

  gpr_log(GPR_ERROR, "should never reach here");
  abort();

  return 0;
}

/* tcp read callback */
static void recv_data(void *tp, gpr_slice *slices, size_t nslices,
                      grpc_endpoint_cb_status error) {
  transport *t = tp;
  size_t i;
  int keep_reading = 0;

  switch (error) {
    case GRPC_ENDPOINT_CB_SHUTDOWN:
    case GRPC_ENDPOINT_CB_EOF:
    case GRPC_ENDPOINT_CB_ERROR:
      lock(t);
      drop_connection(t);
      t->reading = 0;
      if (!t->writing && t->ep) {
        grpc_endpoint_destroy(t->ep);
        t->ep = NULL;
        unref_transport(t); /* safe as we still have a ref for read */
      }
      unlock(t);
      unref_transport(t);
      break;
    case GRPC_ENDPOINT_CB_OK:
      lock(t);
      if (t->cb) {
        for (i = 0; i < nslices && process_read(t, slices[i]); i++)
          ;
      }
      unlock(t);
      keep_reading = 1;
      break;
  }

  for (i = 0; i < nslices; i++) gpr_slice_unref(slices[i]);

  if (keep_reading) {
    grpc_endpoint_notify_on_read(t->ep, recv_data, t);
  }
}

/*
 * CALLBACK LOOP
 */

static grpc_stream_state compute_state(gpr_uint8 write_closed,
                                       gpr_uint8 read_closed) {
  if (write_closed && read_closed) return GRPC_STREAM_CLOSED;
  if (write_closed) return GRPC_STREAM_SEND_CLOSED;
  if (read_closed) return GRPC_STREAM_RECV_CLOSED;
  return GRPC_STREAM_OPEN;
}

static void patch_metadata_ops(stream *s) {
  grpc_stream_op *ops = s->incoming_sopb->ops;
  size_t nops = s->incoming_sopb->nops;
  size_t i;
  size_t j;
  size_t mdidx = 0;
  size_t last_mdidx;
  int found_metadata = 0;

  /* rework the array of metadata into a linked list, making use
     of the breadcrumbs we left in metadata batches during 
     add_metadata_batch */
  for (i = 0; i < nops; i++) {
    grpc_stream_op *op = &ops[i];
    if (op->type != GRPC_OP_METADATA) continue;
    found_metadata = 1;
    /* we left a breadcrumb indicating where the end of this list is,
       and since we add sequentially, we know from the end of the last
       segment where this segment begins */
    last_mdidx = (size_t)(gpr_intptr)(op->data.metadata.list.tail);
    GPR_ASSERT(last_mdidx > mdidx);
    GPR_ASSERT(last_mdidx <= s->incoming_metadata_count);
    /* turn the array into a doubly linked list */
    op->data.metadata.list.head = &s->incoming_metadata[mdidx];
    op->data.metadata.list.tail = &s->incoming_metadata[last_mdidx - 1];
    for (j = mdidx + 1; j < last_mdidx; j++) {
      s->incoming_metadata[j].prev = &s->incoming_metadata[j-1];
      s->incoming_metadata[j-1].next = &s->incoming_metadata[j];
    }
    s->incoming_metadata[mdidx].prev = NULL;
    s->incoming_metadata[last_mdidx-1].next = NULL;
    /* track where we're up to */
    mdidx = last_mdidx;
  }
  if (found_metadata) {
    s->old_incoming_metadata = s->incoming_metadata;
    if (mdidx != s->incoming_metadata_count) {
      /* we have a partially read metadata batch still in incoming_metadata */
      size_t new_count = s->incoming_metadata_count - mdidx;
      size_t copy_bytes = sizeof(*s->incoming_metadata) * new_count;
      GPR_ASSERT(mdidx < s->incoming_metadata_count);
      s->incoming_metadata = gpr_malloc(copy_bytes);
      memcpy(s->old_incoming_metadata + mdidx, s->incoming_metadata, copy_bytes);
      s->incoming_metadata_count = s->incoming_metadata_capacity = new_count;
    } else {
      s->incoming_metadata = NULL;
      s->incoming_metadata_count = 0;
      s->incoming_metadata_capacity = 0;
    }
  }
}

static void finish_reads(transport *t) {
  stream *s;

  while ((s = stream_list_remove_head(t, FINISHED_READ_OP)) != NULL) {
    int publish = 0;
    GPR_ASSERT(s->incoming_sopb);
    *s->publish_state =
        compute_state(s->write_state == WRITE_STATE_SENT_CLOSE, s->read_closed);
    if (*s->publish_state != s->published_state) {
      s->published_state = *s->publish_state;
      publish = 1;
      if (s->published_state == GRPC_STREAM_CLOSED) {
        remove_from_stream_map(t, s);
      }
    }
    if (s->parser.incoming_sopb.nops > 0) {
      grpc_sopb_swap(s->incoming_sopb, &s->parser.incoming_sopb);
      publish = 1;
    }
    if (publish) {
      if (s->incoming_metadata_count > 0) {
        patch_metadata_ops(s);
      }
      s->incoming_sopb = NULL;
      schedule_cb(t, s->recv_done_closure, 1);
    }
  }

}

static void schedule_cb(transport *t, op_closure closure, int success) {
  if (t->pending_callbacks.capacity == t->pending_callbacks.count) {
    t->pending_callbacks.capacity =
        GPR_MAX(t->pending_callbacks.capacity * 2, 8);
    t->pending_callbacks.callbacks =
        gpr_realloc(t->pending_callbacks.callbacks,
                    t->pending_callbacks.capacity *
                        sizeof(*t->pending_callbacks.callbacks));
  }
  closure.success = success;
  t->pending_callbacks.callbacks[t->pending_callbacks.count++] = closure;
}

static int prepare_callbacks(transport *t) {
  op_closure_array temp = t->pending_callbacks;
  t->pending_callbacks = t->executing_callbacks;
  t->executing_callbacks = temp;
  return t->executing_callbacks.count > 0;
}

static void run_callbacks(transport *t, const grpc_transport_callbacks *cb) {
  size_t i;
  for (i = 0; i < t->executing_callbacks.count; i++) {
    op_closure c = t->executing_callbacks.callbacks[i];
    c.cb(c.user_data, c.success);
  }
  t->executing_callbacks.count = 0;
}

static void call_cb_closed(transport *t, const grpc_transport_callbacks *cb) {
  cb->closed(t->cb_user_data, &t->base);
}

/*
 * POLLSET STUFF
 */

static void add_to_pollset_locked(transport *t, grpc_pollset *pollset) {
  if (t->ep) {
    grpc_endpoint_add_to_pollset(t->ep, pollset);
  }
}

static void add_to_pollset(grpc_transport *gt, grpc_pollset *pollset) {
  transport *t = (transport *)gt;
  lock(t);
  add_to_pollset_locked(t, pollset);
  unlock(t);
}

/*
 * INTEGRATION GLUE
 */

static const grpc_transport_vtable vtable = {
    sizeof(stream),  init_stream,    perform_op,
    add_to_pollset,  destroy_stream, goaway,
    close_transport, send_ping,      destroy_transport};

void grpc_create_chttp2_transport(grpc_transport_setup_callback setup,
                                  void *arg,
                                  const grpc_channel_args *channel_args,
                                  grpc_endpoint *ep, gpr_slice *slices,
                                  size_t nslices, grpc_mdctx *mdctx,
                                  int is_client) {
  transport *t = gpr_malloc(sizeof(transport));
  init_transport(t, setup, arg, channel_args, ep, slices, nslices, mdctx,
                 is_client);
}
