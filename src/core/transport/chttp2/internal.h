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

#ifndef GRPC_INTERNAL_CORE_CHTTP2_INTERNAL_H
#define GRPC_INTERNAL_CORE_CHTTP2_INTERNAL_H

#include "src/core/iomgr/endpoint.h"
#include "src/core/transport/chttp2/frame.h"
#include "src/core/transport/chttp2/frame_data.h"
#include "src/core/transport/chttp2/frame_goaway.h"
#include "src/core/transport/chttp2/frame_ping.h"
#include "src/core/transport/chttp2/frame_rst_stream.h"
#include "src/core/transport/chttp2/frame_settings.h"
#include "src/core/transport/chttp2/frame_window_update.h"
#include "src/core/transport/chttp2/hpack_parser.h"
#include "src/core/transport/chttp2/incoming_metadata.h"
#include "src/core/transport/chttp2/stream_encoder.h"
#include "src/core/transport/chttp2/stream_map.h"
#include "src/core/transport/connectivity_state.h"
#include "src/core/transport/transport_impl.h"

typedef struct grpc_chttp2_transport grpc_chttp2_transport;
typedef struct grpc_chttp2_stream grpc_chttp2_stream;

/* streams are kept in various linked lists depending on what things need to
   happen to them... this enum labels each list */
typedef enum {
  GRPC_CHTTP2_LIST_ALL_STREAMS,
  GRPC_CHTTP2_LIST_READ_WRITE_STATE_CHANGED,
  GRPC_CHTTP2_LIST_WRITABLE,
  GRPC_CHTTP2_LIST_WRITING,
  GRPC_CHTTP2_LIST_WRITTEN,
  GRPC_CHTTP2_LIST_PARSING_SEEN,
  GRPC_CHTTP2_LIST_CLOSED_WAITING_FOR_PARSING,
  GRPC_CHTTP2_LIST_CANCELLED_WAITING_FOR_WRITING,
  GRPC_CHTTP2_LIST_INCOMING_WINDOW_UPDATED,
  /** streams that are waiting to start because there are too many concurrent
      streams on the connection */
  GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY,
  STREAM_LIST_COUNT /* must be last */
} grpc_chttp2_stream_list_id;

/* deframer state for the overall http2 stream of bytes */
typedef enum {
  /* prefix: one entry per http2 connection prefix byte */
  GRPC_DTS_CLIENT_PREFIX_0 = 0,
  GRPC_DTS_CLIENT_PREFIX_1,
  GRPC_DTS_CLIENT_PREFIX_2,
  GRPC_DTS_CLIENT_PREFIX_3,
  GRPC_DTS_CLIENT_PREFIX_4,
  GRPC_DTS_CLIENT_PREFIX_5,
  GRPC_DTS_CLIENT_PREFIX_6,
  GRPC_DTS_CLIENT_PREFIX_7,
  GRPC_DTS_CLIENT_PREFIX_8,
  GRPC_DTS_CLIENT_PREFIX_9,
  GRPC_DTS_CLIENT_PREFIX_10,
  GRPC_DTS_CLIENT_PREFIX_11,
  GRPC_DTS_CLIENT_PREFIX_12,
  GRPC_DTS_CLIENT_PREFIX_13,
  GRPC_DTS_CLIENT_PREFIX_14,
  GRPC_DTS_CLIENT_PREFIX_15,
  GRPC_DTS_CLIENT_PREFIX_16,
  GRPC_DTS_CLIENT_PREFIX_17,
  GRPC_DTS_CLIENT_PREFIX_18,
  GRPC_DTS_CLIENT_PREFIX_19,
  GRPC_DTS_CLIENT_PREFIX_20,
  GRPC_DTS_CLIENT_PREFIX_21,
  GRPC_DTS_CLIENT_PREFIX_22,
  GRPC_DTS_CLIENT_PREFIX_23,
  /* frame header byte 0... */
  /* must follow from the prefix states */
  GRPC_DTS_FH_0,
  GRPC_DTS_FH_1,
  GRPC_DTS_FH_2,
  GRPC_DTS_FH_3,
  GRPC_DTS_FH_4,
  GRPC_DTS_FH_5,
  GRPC_DTS_FH_6,
  GRPC_DTS_FH_7,
  /* ... frame header byte 8 */
  GRPC_DTS_FH_8,
  /* inside a http2 frame */
  GRPC_DTS_FRAME
} grpc_chttp2_deframe_transport_state;

typedef enum {
  GRPC_WRITE_STATE_OPEN,
  GRPC_WRITE_STATE_QUEUED_CLOSE,
  GRPC_WRITE_STATE_SENT_CLOSE
} grpc_chttp2_write_state;

/* flags that can be or'd into stream_global::writing_now */
#define GRPC_CHTTP2_WRITING_DATA 1
#define GRPC_CHTTP2_WRITING_WINDOW 2

typedef enum {
  GRPC_DONT_SEND_CLOSED = 0,
  GRPC_SEND_CLOSED,
  GRPC_SEND_CLOSED_WITH_RST_STREAM
} grpc_chttp2_send_closed;

typedef struct {
  grpc_chttp2_stream *head;
  grpc_chttp2_stream *tail;
} grpc_chttp2_stream_list;

typedef struct {
  grpc_chttp2_stream *next;
  grpc_chttp2_stream *prev;
} grpc_chttp2_stream_link;

/* We keep several sets of connection wide parameters */
typedef enum {
  /* The settings our peer has asked for (and we have acked) */
  GRPC_PEER_SETTINGS = 0,
  /* The settings we'd like to have */
  GRPC_LOCAL_SETTINGS,
  /* The settings we've published to our peer */
  GRPC_SENT_SETTINGS,
  /* The settings the peer has acked */
  GRPC_ACKED_SETTINGS,
  GRPC_NUM_SETTING_SETS
} grpc_chttp2_setting_set;

/* Outstanding ping request data */
typedef struct grpc_chttp2_outstanding_ping {
  gpr_uint8 id[8];
  grpc_closure *on_recv;
  struct grpc_chttp2_outstanding_ping *next;
  struct grpc_chttp2_outstanding_ping *prev;
} grpc_chttp2_outstanding_ping;

typedef struct {
  /** data to write next write */
  gpr_slice_buffer qbuf;

  /** window available for us to send to peer */
  gpr_int64 outgoing_window;
  /** window available for peer to send to us - updated after parse */
  gpr_uint32 incoming_window;
  /** how much window would we like to have for incoming_window */
  gpr_uint32 connection_window_target;

  /** have we seen a goaway */
  gpr_uint8 seen_goaway;
  /** have we sent a goaway */
  gpr_uint8 sent_goaway;

  /** is this transport a client? */
  gpr_uint8 is_client;
  /** are the local settings dirty and need to be sent? */
  gpr_uint8 dirtied_local_settings;
  /** have local settings been sent? */
  gpr_uint8 sent_local_settings;
  /** bitmask of setting indexes to send out */
  gpr_uint32 force_send_settings;
  /** settings values */
  gpr_uint32 settings[GRPC_NUM_SETTING_SETS][GRPC_CHTTP2_NUM_SETTINGS];

  /** what is the next stream id to be allocated by this peer?
      copied to next_stream_id in parsing when parsing commences */
  gpr_uint32 next_stream_id;

  /** last received stream id */
  gpr_uint32 last_incoming_stream_id;

  /** pings awaiting responses */
  grpc_chttp2_outstanding_ping pings;
  /** next payload for an outgoing ping */
  gpr_uint64 ping_counter;

  /** concurrent stream count: updated when not parsing,
      so this is a strict over-estimation on the client */
  gpr_uint32 concurrent_stream_count;
} grpc_chttp2_transport_global;

typedef struct {
  /** data to write now */
  gpr_slice_buffer outbuf;
  /** hpack encoding */
  grpc_chttp2_hpack_compressor hpack_compressor;
  /** is this a client? */
  gpr_uint8 is_client;
  /** callback for when writing is done */
  grpc_closure done_cb;
} grpc_chttp2_transport_writing;

struct grpc_chttp2_transport_parsing {
  /** is this transport a client? (boolean) */
  gpr_uint8 is_client;

  /** were settings updated? */
  gpr_uint8 settings_updated;
  /** was a settings ack received? */
  gpr_uint8 settings_ack_received;
  /** was a goaway frame received? */
  gpr_uint8 goaway_received;

  /** initial window change */
  gpr_int64 initial_window_update;

  /** data to write later - after parsing */
  gpr_slice_buffer qbuf;
  /* metadata object cache */
  grpc_mdstr *str_grpc_timeout;
  /** parser for headers */
  grpc_chttp2_hpack_parser hpack_parser;
  /** simple one shot parsers */
  union {
    grpc_chttp2_window_update_parser window_update;
    grpc_chttp2_settings_parser settings;
    grpc_chttp2_ping_parser ping;
    grpc_chttp2_rst_stream_parser rst_stream;
  } simple;
  /** parser for goaway frames */
  grpc_chttp2_goaway_parser goaway_parser;

  /** window available for peer to send to us */
  gpr_uint32 incoming_window;
  gpr_uint32 incoming_window_delta;

  /** next stream id available at the time of beginning parsing */
  gpr_uint32 next_stream_id;
  gpr_uint32 last_incoming_stream_id;

  /* deframing */
  grpc_chttp2_deframe_transport_state deframe_state;
  gpr_uint8 incoming_frame_type;
  gpr_uint8 incoming_frame_flags;
  gpr_uint8 header_eof;
  gpr_uint32 expect_continuation_stream_id;
  gpr_uint32 incoming_frame_size;
  gpr_uint32 incoming_stream_id;

  /* active parser */
  void *parser_data;
  grpc_chttp2_stream_parsing *incoming_stream;
  grpc_chttp2_parse_error (*parser)(
      grpc_exec_ctx *exec_ctx, void *parser_user_data,
      grpc_chttp2_transport_parsing *transport_parsing,
      grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last);

  /* received settings */
  gpr_uint32 settings[GRPC_CHTTP2_NUM_SETTINGS];

  /* goaway data */
  grpc_status_code goaway_error;
  gpr_uint32 goaway_last_stream_index;
  gpr_slice goaway_text;

  gpr_int64 outgoing_window_update;

  /** pings awaiting responses */
  grpc_chttp2_outstanding_ping pings;
};

struct grpc_chttp2_transport {
  grpc_transport base; /* must be first */
  grpc_endpoint *ep;
  grpc_mdctx *metadata_context;
  gpr_refcount refs;
  char *peer_string;

  /** when this drops to zero it's safe to shutdown the endpoint */
  gpr_refcount shutdown_ep_refs;

  gpr_mu mu;

  /** is the transport destroying itself? */
  gpr_uint8 destroying;
  /** has the upper layer closed the transport? */
  gpr_uint8 closed;

  /** is a thread currently writing */
  gpr_uint8 writing_active;
  /** is a thread currently parsing */
  gpr_uint8 parsing_active;

  /** is there a read request to the endpoint outstanding? */
  gpr_uint8 endpoint_reading;

  /** various lists of streams */
  grpc_chttp2_stream_list lists[STREAM_LIST_COUNT];

  /** global state for reading/writing */
  grpc_chttp2_transport_global global;
  /** state only accessible by the chain of execution that
      set writing_active=1 */
  grpc_chttp2_transport_writing writing;
  /** state only accessible by the chain of execution that
      set parsing_active=1 */
  grpc_chttp2_transport_parsing parsing;

  /** maps stream id to grpc_chttp2_stream objects;
      owned by the parsing thread when parsing */
  grpc_chttp2_stream_map parsing_stream_map;

  /** streams created by the client (possibly during parsing);
      merged with parsing_stream_map during unlock when no
      parsing is occurring */
  grpc_chttp2_stream_map new_stream_map;

  /** closure to execute writing */
  grpc_closure writing_action;
  /** closure to finish reading from the endpoint */
  grpc_closure recv_data;

  /** incoming read bytes */
  gpr_slice_buffer read_buffer;

  /** address to place a newly accepted stream - set and unset by
      grpc_chttp2_parsing_accept_stream; used by init_stream to
      publish the accepted server stream */
  grpc_chttp2_stream **accepting_stream;

  struct {
    /* accept stream callback */
    void (*accept_stream)(void *user_data, grpc_transport *transport,
                          const void *server_data);
    void *accept_stream_user_data;

    /** connectivity tracking */
    grpc_connectivity_state_tracker state_tracker;
  } channel_callback;
};

typedef struct {
  /** HTTP2 stream id for this stream, or zero if one has not been assigned */
  gpr_uint32 id;

  grpc_closure *send_done_closure;
  grpc_closure *recv_done_closure;

  /** window available for us to send to peer */
  gpr_int64 outgoing_window;
  /** The number of bytes the upper layers have offered to receive.
      As the upper layer offers more bytes, this value increases.
      As bytes are read, this value decreases. */
  gpr_uint32 max_recv_bytes;
  /** The number of bytes the upper layer has offered to read but we have
      not yet announced to HTTP2 flow control.
      As the upper layers offer to read more bytes, this value increases.
      As we advertise incoming flow control window, this value decreases. */
  gpr_uint32 unannounced_incoming_window;
  /** The number of bytes of HTTP2 flow control we have advertised.
      As we advertise incoming flow control window, this value increases.
      As bytes are read, this value decreases.
      Updated after parse. */
  gpr_uint32 incoming_window;
  /** stream ops the transport user would like to send */
  grpc_stream_op_buffer *outgoing_sopb;
  /** when the application requests writes be closed, the write_closed is
      'queued'; when the close is flow controlled into the send path, we are
      'sending' it; when the write has been performed it is 'sent' */
  grpc_chttp2_write_state write_state;
  /** is this stream closed (boolean) */
  gpr_uint8 read_closed;
  /** has this stream been cancelled? (boolean) */
  gpr_uint8 cancelled;
  grpc_status_code cancelled_status;
  /** have we told the upper layer that this stream is cancelled? */
  gpr_uint8 published_cancelled;
  /** is this stream in the stream map? (boolean) */
  gpr_uint8 in_stream_map;
  /** bitmask of GRPC_CHTTP2_WRITING_xxx above */
  gpr_uint8 writing_now;
  /** has anything been written to this stream? */
  gpr_uint8 written_anything;

  /** stream state already published to the upper layer */
  grpc_stream_state published_state;
  /** address to publish next stream state to */
  grpc_stream_state *publish_state;
  /** pointer to sop buffer to fill in with new stream ops */
  grpc_stream_op_buffer *publish_sopb;
  grpc_stream_op_buffer incoming_sopb;

  /** incoming metadata */
  grpc_chttp2_incoming_metadata_buffer incoming_metadata;
  grpc_chttp2_incoming_metadata_live_op_buffer outstanding_metadata;
} grpc_chttp2_stream_global;

typedef struct {
  /** HTTP2 stream id for this stream, or zero if one has not been assigned */
  gpr_uint32 id;
  /** sops that have passed flow control to be written */
  grpc_stream_op_buffer sopb;
  /** how strongly should we indicate closure with the next write */
  grpc_chttp2_send_closed send_closed;
  /** how much window should we announce? */
  gpr_uint32 announce_window;
} grpc_chttp2_stream_writing;

struct grpc_chttp2_stream_parsing {
  /** HTTP2 stream id for this stream, or zero if one has not been assigned */
  gpr_uint32 id;
  /** has this stream received a close */
  gpr_uint8 received_close;
  /** saw a rst_stream */
  gpr_uint8 saw_rst_stream;
  /** incoming_window has been reduced by this much during parsing */
  gpr_uint32 incoming_window_delta;
  /** window available for peer to send to us */
  gpr_uint32 incoming_window;
  /** parsing state for data frames */
  grpc_chttp2_data_parser data_parser;
  /** reason give to rst_stream */
  gpr_uint32 rst_stream_reason;
  /* amount of window given */
  gpr_uint64 outgoing_window_update;

  /** incoming metadata */
  grpc_chttp2_incoming_metadata_buffer incoming_metadata;
};

struct grpc_chttp2_stream {
  grpc_chttp2_stream_global global;
  grpc_chttp2_stream_writing writing;
  grpc_chttp2_stream_parsing parsing;

  grpc_chttp2_stream_link links[STREAM_LIST_COUNT];
  gpr_uint8 included[STREAM_LIST_COUNT];
};

/** Transport writing call flow:
    chttp2_transport.c calls grpc_chttp2_unlocking_check_writes to see if writes
   are required;
    if they are, chttp2_transport.c calls grpc_chttp2_perform_writes to do the
   writes.
    Once writes have been completed (meaning another write could potentially be
   started),
    grpc_chttp2_terminate_writing is called. This will call
   grpc_chttp2_cleanup_writing, at which
    point the write phase is complete. */

/** Someone is unlocking the transport mutex: check to see if writes
    are required, and schedule them if so */
int grpc_chttp2_unlocking_check_writes(grpc_chttp2_transport_global *global,
                                       grpc_chttp2_transport_writing *writing);
void grpc_chttp2_perform_writes(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_writing *transport_writing,
    grpc_endpoint *endpoint);
void grpc_chttp2_terminate_writing(grpc_exec_ctx *exec_ctx,
                                   void *transport_writing, int success);
void grpc_chttp2_cleanup_writing(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport_global *global,
                                 grpc_chttp2_transport_writing *writing);

void grpc_chttp2_prepare_to_read(grpc_chttp2_transport_global *global,
                                 grpc_chttp2_transport_parsing *parsing);
/** Process one slice of incoming data; return 1 if the connection is still
    viable after reading, or 0 if the connection should be torn down */
int grpc_chttp2_perform_read(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_parsing *transport_parsing,
                             gpr_slice slice);
void grpc_chttp2_publish_reads(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport_global *global,
                               grpc_chttp2_transport_parsing *parsing);

/** Get a writable stream
    returns non-zero if there was a stream available */
void grpc_chttp2_list_add_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
void grpc_chttp2_list_add_first_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_writing **stream_writing);
void grpc_chttp2_list_remove_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);

void grpc_chttp2_list_add_incoming_window_updated(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_incoming_window_updated(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_parsing **stream_parsing);
void grpc_chttp2_list_remove_incoming_window_updated(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);

void grpc_chttp2_list_add_writing_stream(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing *stream_writing);
int grpc_chttp2_list_have_writing_streams(
    grpc_chttp2_transport_writing *transport_writing);
int grpc_chttp2_list_pop_writing_stream(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing **stream_writing);

void grpc_chttp2_list_add_written_stream(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing *stream_writing);
int grpc_chttp2_list_pop_written_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_writing **stream_writing);

void grpc_chttp2_list_add_parsing_seen_stream(
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing);
int grpc_chttp2_list_pop_parsing_seen_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_parsing **stream_parsing);

void grpc_chttp2_list_add_waiting_for_concurrency(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_waiting_for_concurrency(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);

void grpc_chttp2_list_add_closed_waiting_for_parsing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_closed_waiting_for_parsing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);

void grpc_chttp2_list_add_cancelled_waiting_for_writing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_cancelled_waiting_for_writing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);

void grpc_chttp2_list_add_read_write_state_changed(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_read_write_state_changed(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);

grpc_chttp2_stream_parsing *grpc_chttp2_parsing_lookup_stream(
    grpc_chttp2_transport_parsing *transport_parsing, gpr_uint32 id);
grpc_chttp2_stream_parsing *grpc_chttp2_parsing_accept_stream(
    grpc_chttp2_transport_parsing *transport_parsing, gpr_uint32 id);

void grpc_chttp2_add_incoming_goaway(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    gpr_uint32 goaway_error, gpr_slice goaway_text);

void grpc_chttp2_register_stream(grpc_chttp2_transport *t,
                                 grpc_chttp2_stream *s);
/* returns 1 if this is the last stream, 0 otherwise */
int grpc_chttp2_unregister_stream(grpc_chttp2_transport *t,
                                  grpc_chttp2_stream *s) GRPC_MUST_USE_RESULT;
int grpc_chttp2_has_streams(grpc_chttp2_transport *t);
void grpc_chttp2_for_all_streams(
    grpc_chttp2_transport_global *transport_global, void *user_data,
    void (*cb)(grpc_chttp2_transport_global *transport_global, void *user_data,
               grpc_chttp2_stream_global *stream_global));

void grpc_chttp2_parsing_become_skip_parser(
    grpc_chttp2_transport_parsing *transport_parsing);

#define GRPC_CHTTP2_CLIENT_CONNECT_STRING "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define GRPC_CHTTP2_CLIENT_CONNECT_STRLEN \
  (sizeof(GRPC_CHTTP2_CLIENT_CONNECT_STRING) - 1)

extern int grpc_http_trace;
extern int grpc_flowctl_trace;

#define GRPC_CHTTP2_IF_TRACING(stmt) \
  if (!(grpc_http_trace))            \
    ;                                \
  else                               \
  stmt

#define GRPC_CHTTP2_FLOWCTL_TRACE_STREAM(reason, transport, context, var,     \
                                         delta)                               \
  if (!(grpc_flowctl_trace)) {                                                \
  } else {                                                                    \
    grpc_chttp2_flowctl_trace(__FILE__, __LINE__, reason, #context, #var,     \
                              transport->is_client, context->id,              \
                              (gpr_int64)(context->var), (gpr_int64)(delta)); \
  }

#define GRPC_CHTTP2_FLOWCTL_TRACE_TRANSPORT(reason, context, var, delta)      \
  if (!(grpc_flowctl_trace)) {                                                \
  } else {                                                                    \
    grpc_chttp2_flowctl_trace(__FILE__, __LINE__, reason, #context, #var,     \
                              context->is_client, 0,                          \
                              (gpr_int64)(context->var), (gpr_int64)(delta)); \
  }

void grpc_chttp2_flowctl_trace(const char *file, int line, const char *reason,
                               const char *context, const char *var,
                               int is_client, gpr_uint32 stream_id,
                               gpr_int64 current_value, gpr_int64 delta);

#endif
