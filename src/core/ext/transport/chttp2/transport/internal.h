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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INTERNAL_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INTERNAL_H

#include <assert.h>
#include <stdbool.h>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/frame_data.h"
#include "src/core/ext/transport/chttp2/transport/frame_goaway.h"
#include "src/core/ext/transport/chttp2/transport/frame_ping.h"
#include "src/core/ext/transport/chttp2/transport/frame_rst_stream.h"
#include "src/core/ext/transport/chttp2/transport/frame_settings.h"
#include "src/core/ext/transport/chttp2/transport/frame_window_update.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/incoming_metadata.h"
#include "src/core/ext/transport/chttp2/transport/stream_map.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport_impl.h"

typedef struct grpc_chttp2_transport grpc_chttp2_transport;
typedef struct grpc_chttp2_stream grpc_chttp2_stream;

/* streams are kept in various linked lists depending on what things need to
   happen to them... this enum labels each list */
typedef enum {
  GRPC_CHTTP2_LIST_ALL_STREAMS,
  GRPC_CHTTP2_LIST_CHECK_READ_OPS,
  GRPC_CHTTP2_LIST_UNANNOUNCED_INCOMING_WINDOW_AVAILABLE,
  GRPC_CHTTP2_LIST_WRITABLE,
  GRPC_CHTTP2_LIST_WRITING,
  GRPC_CHTTP2_LIST_WRITTEN,
  GRPC_CHTTP2_LIST_PARSING_SEEN,
  GRPC_CHTTP2_LIST_CLOSED_WAITING_FOR_PARSING,
  GRPC_CHTTP2_LIST_CLOSED_WAITING_FOR_WRITING,
  GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT,
  /* streams waiting for the outgoing window in the writing path, they will be
   * merged to the stalled list or writable list under transport lock. */
  GRPC_CHTTP2_LIST_WRITING_STALLED_BY_TRANSPORT,
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
  uint8_t id[8];
  grpc_closure *on_recv;
  struct grpc_chttp2_outstanding_ping *next;
  struct grpc_chttp2_outstanding_ping *prev;
} grpc_chttp2_outstanding_ping;

/* forward declared in frame_data.h */
struct grpc_chttp2_incoming_byte_stream {
  grpc_byte_stream base;
  gpr_refcount refs;
  struct grpc_chttp2_incoming_byte_stream *next_message;
  int failed;

  grpc_chttp2_transport *transport;
  grpc_chttp2_stream *stream;
  int is_tail;
  gpr_slice_buffer slices;
  grpc_closure *on_next;
  gpr_slice *next;
};

typedef struct {
  /** data to write next write */
  gpr_slice_buffer qbuf;

  /** window available for us to send to peer */
  int64_t outgoing_window;
  /** window available to announce to peer */
  int64_t announce_incoming_window;
  /** how much window would we like to have for incoming_window */
  uint32_t connection_window_target;

  /** have we seen a goaway */
  uint8_t seen_goaway;
  /** have we sent a goaway */
  uint8_t sent_goaway;

  /** is this transport a client? */
  uint8_t is_client;
  /** are the local settings dirty and need to be sent? */
  uint8_t dirtied_local_settings;
  /** have local settings been sent? */
  uint8_t sent_local_settings;
  /** bitmask of setting indexes to send out */
  uint32_t force_send_settings;
  /** settings values */
  uint32_t settings[GRPC_NUM_SETTING_SETS][GRPC_CHTTP2_NUM_SETTINGS];

  /** what is the next stream id to be allocated by this peer?
      copied to next_stream_id in parsing when parsing commences */
  uint32_t next_stream_id;

  /** how far to lookahead in a stream? */
  uint32_t stream_lookahead;

  /** last received stream id */
  uint32_t last_incoming_stream_id;

  /** pings awaiting responses */
  grpc_chttp2_outstanding_ping pings;
  /** next payload for an outgoing ping */
  uint64_t ping_counter;

  /** concurrent stream count: updated when not parsing,
      so this is a strict over-estimation on the client */
  uint32_t concurrent_stream_count;
} grpc_chttp2_transport_global;

typedef struct {
  /** data to write now */
  gpr_slice_buffer outbuf;
  /** hpack encoding */
  grpc_chttp2_hpack_compressor hpack_compressor;
  int64_t outgoing_window;
  /** is this a client? */
  uint8_t is_client;
  /** callback for when writing is done */
  grpc_closure done_cb;
} grpc_chttp2_transport_writing;

struct grpc_chttp2_transport_parsing {
  /** is this transport a client? (boolean) */
  uint8_t is_client;

  /** were settings updated? */
  uint8_t settings_updated;
  /** was a settings ack received? */
  uint8_t settings_ack_received;
  /** was a goaway frame received? */
  uint8_t goaway_received;

  /** initial window change */
  int64_t initial_window_update;

  /** data to write later - after parsing */
  gpr_slice_buffer qbuf;
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
  int64_t incoming_window;

  /** next stream id available at the time of beginning parsing */
  uint32_t next_stream_id;
  uint32_t last_incoming_stream_id;

  /* deframing */
  grpc_chttp2_deframe_transport_state deframe_state;
  uint8_t incoming_frame_type;
  uint8_t incoming_frame_flags;
  uint8_t header_eof;
  uint32_t expect_continuation_stream_id;
  uint32_t incoming_frame_size;
  uint32_t incoming_stream_id;

  /* current max frame size */
  uint32_t max_frame_size;

  /* active parser */
  void *parser_data;
  grpc_chttp2_stream_parsing *incoming_stream;
  grpc_chttp2_parse_error (*parser)(
      grpc_exec_ctx *exec_ctx, void *parser_user_data,
      grpc_chttp2_transport_parsing *transport_parsing,
      grpc_chttp2_stream_parsing *stream_parsing, gpr_slice slice, int is_last);

  /* received settings */
  uint32_t settings[GRPC_CHTTP2_NUM_SETTINGS];
  /* last settings that were sent */
  uint32_t last_sent_settings[GRPC_CHTTP2_NUM_SETTINGS];

  /* goaway data */
  grpc_status_code goaway_error;
  uint32_t goaway_last_stream_index;
  gpr_slice goaway_text;

  int64_t outgoing_window;
};

typedef void (*grpc_chttp2_locked_action)(grpc_exec_ctx *ctx,
                                          grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s, void *arg);

typedef struct grpc_chttp2_executor_action_header {
  grpc_chttp2_stream *stream;
  grpc_chttp2_locked_action action;
  struct grpc_chttp2_executor_action_header *next;
  void *arg;
} grpc_chttp2_executor_action_header;

struct grpc_chttp2_transport {
  grpc_transport base; /* must be first */
  gpr_refcount refs;
  grpc_endpoint *ep;
  char *peer_string;

  /** when this drops to zero it's safe to shutdown the endpoint */
  gpr_refcount shutdown_ep_refs;

  struct {
    gpr_mu mu;

    /** is a thread currently in the global lock */
    bool global_active;
    /** is a thread currently writing */
    bool writing_active;
    /** is a thread currently parsing */
    bool parsing_active;

    grpc_chttp2_executor_action_header *pending_actions_head;
    grpc_chttp2_executor_action_header *pending_actions_tail;
  } executor;

  /** is the transport destroying itself? */
  uint8_t destroying;
  /** has the upper layer closed the transport? */
  uint8_t closed;

  /** is there a read request to the endpoint outstanding? */
  uint8_t endpoint_reading;

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
  /** closure to start reading from the endpoint */
  grpc_closure reading_action;
  /** closure to actually do parsing */
  grpc_closure parsing_action;

  /** incoming read bytes */
  gpr_slice_buffer read_buffer;

  /** address to place a newly accepted stream - set and unset by
      grpc_chttp2_parsing_accept_stream; used by init_stream to
      publish the accepted server stream */
  grpc_chttp2_stream **accepting_stream;

  struct {
    /* accept stream callback */
    void (*accept_stream)(grpc_exec_ctx *exec_ctx, void *user_data,
                          grpc_transport *transport, const void *server_data);
    void *accept_stream_user_data;

    /** connectivity tracking */
    grpc_connectivity_state_tracker state_tracker;
  } channel_callback;

  /** Transport op to be applied post-parsing */
  grpc_transport_op *post_parsing_op;
};

typedef struct {
  /** HTTP2 stream id for this stream, or zero if one has not been assigned */
  uint32_t id;

  /** window available for us to send to peer */
  int64_t outgoing_window;
  /** The number of bytes the upper layers have offered to receive.
      As the upper layer offers more bytes, this value increases.
      As bytes are read, this value decreases. */
  uint32_t max_recv_bytes;
  /** The number of bytes the upper layer has offered to read but we have
      not yet announced to HTTP2 flow control.
      As the upper layers offer to read more bytes, this value increases.
      As we advertise incoming flow control window, this value decreases. */
  uint32_t unannounced_incoming_window_for_parse;
  uint32_t unannounced_incoming_window_for_writing;
  /** things the upper layers would like to send */
  grpc_metadata_batch *send_initial_metadata;
  grpc_closure *send_initial_metadata_finished;
  grpc_byte_stream *send_message;
  grpc_closure *send_message_finished;
  grpc_metadata_batch *send_trailing_metadata;
  grpc_closure *send_trailing_metadata_finished;

  grpc_metadata_batch *recv_initial_metadata;
  grpc_closure *recv_initial_metadata_ready;
  grpc_byte_stream **recv_message;
  grpc_closure *recv_message_ready;
  grpc_metadata_batch *recv_trailing_metadata;
  grpc_closure *recv_trailing_metadata_finished;

  grpc_transport_stream_stats *collecting_stats;
  grpc_transport_stream_stats stats;

  /** number of streams that are currently being read */
  gpr_refcount active_streams;

  /** Is this stream closed for writing. */
  bool write_closed;
  /** Is this stream reading half-closed. */
  bool read_closed;
  /** Are all published incoming byte streams closed. */
  bool all_incoming_byte_streams_finished;
  /** Is this stream in the stream map. */
  bool in_stream_map;
  /** Has this stream seen an error.
      If true, then pending incoming frames can be thrown away. */
  bool seen_error;
  bool exceeded_metadata_size;

  bool published_initial_metadata;
  bool published_trailing_metadata;

  grpc_chttp2_incoming_metadata_buffer received_initial_metadata;
  grpc_chttp2_incoming_metadata_buffer received_trailing_metadata;

  grpc_chttp2_incoming_frame_queue incoming_frames;
} grpc_chttp2_stream_global;

typedef struct {
  /** HTTP2 stream id for this stream, or zero if one has not been assigned */
  uint32_t id;
  uint8_t fetching;
  bool sent_initial_metadata;
  uint8_t sent_message;
  uint8_t sent_trailing_metadata;
  uint8_t read_closed;
  /** send this initial metadata */
  grpc_metadata_batch *send_initial_metadata;
  grpc_byte_stream *send_message;
  grpc_metadata_batch *send_trailing_metadata;
  int64_t outgoing_window;
  /** how much window should we announce? */
  uint32_t announce_window;
  gpr_slice_buffer flow_controlled_buffer;
  gpr_slice fetching_slice;
  size_t stream_fetched;
  grpc_closure finished_fetch;
  /** stats gathered during the write */
  grpc_transport_one_way_stats stats;
} grpc_chttp2_stream_writing;

struct grpc_chttp2_stream_parsing {
  /** HTTP2 stream id for this stream, or zero if one has not been assigned */
  uint32_t id;
  /** has this stream received a close */
  uint8_t received_close;
  /** saw a rst_stream */
  uint8_t saw_rst_stream;
  /** how many header frames have we received? */
  uint8_t header_frames_received;
  /** which metadata did we get (on this parse) */
  uint8_t got_metadata_on_parse[2];
  /** should we raise the seen_error flag in transport_global */
  bool seen_error;
  bool exceeded_metadata_size;
  /** window available for peer to send to us */
  int64_t incoming_window;
  /** parsing state for data frames */
  grpc_chttp2_data_parser data_parser;
  /** reason give to rst_stream */
  uint32_t rst_stream_reason;
  /** amount of window given */
  int64_t outgoing_window;
  /** number of bytes received - reset at end of parse thread execution */
  int64_t received_bytes;
  /** stats gathered during the parse */
  grpc_transport_stream_stats stats;

  /** incoming metadata */
  grpc_chttp2_incoming_metadata_buffer metadata_buffer[2];
};

struct grpc_chttp2_stream {
  grpc_stream_refcount *refcount;
  grpc_chttp2_stream_global global;
  grpc_chttp2_stream_writing writing;
  grpc_chttp2_stream_parsing parsing;

  grpc_chttp2_stream_link links[STREAM_LIST_COUNT];
  uint8_t included[STREAM_LIST_COUNT];
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
int grpc_chttp2_unlocking_check_writes(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_transport_global *global,
                                       grpc_chttp2_transport_writing *writing,
                                       int is_parsing);
void grpc_chttp2_perform_writes(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_writing *transport_writing,
    grpc_endpoint *endpoint);
void grpc_chttp2_terminate_writing(grpc_exec_ctx *exec_ctx,
                                   void *transport_writing, bool success);
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

bool grpc_chttp2_list_add_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
/** Get a writable stream
    returns non-zero if there was a stream available */
int grpc_chttp2_list_pop_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_writing **stream_writing);
bool grpc_chttp2_list_remove_writable_stream(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global) GRPC_MUST_USE_RESULT;

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

void grpc_chttp2_list_add_check_read_ops(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
bool grpc_chttp2_list_remove_check_read_ops(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_check_read_ops(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);

void grpc_chttp2_list_add_writing_stalled_by_transport(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing *stream_writing);
void grpc_chttp2_list_flush_writing_stalled_by_transport(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_writing *transport_writing,
    bool is_window_available);

void grpc_chttp2_list_add_stalled_by_transport(
    grpc_chttp2_transport_writing *transport_writing,
    grpc_chttp2_stream_writing *stream_writing);
int grpc_chttp2_list_pop_stalled_by_transport(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);
void grpc_chttp2_list_remove_stalled_by_transport(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);

void grpc_chttp2_list_add_unannounced_incoming_window_available(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
void grpc_chttp2_list_remove_unannounced_incoming_window_available(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_unannounced_incoming_window_available(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_global **stream_global,
    grpc_chttp2_stream_parsing **stream_parsing);

void grpc_chttp2_list_add_closed_waiting_for_parsing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_closed_waiting_for_parsing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);

void grpc_chttp2_list_add_closed_waiting_for_writing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global);
int grpc_chttp2_list_pop_closed_waiting_for_writing(
    grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global **stream_global);

grpc_chttp2_stream_parsing *grpc_chttp2_parsing_lookup_stream(
    grpc_chttp2_transport_parsing *transport_parsing, uint32_t id);
grpc_chttp2_stream_parsing *grpc_chttp2_parsing_accept_stream(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    uint32_t id);

void grpc_chttp2_add_incoming_goaway(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    uint32_t goaway_error, gpr_slice goaway_text);

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
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing);

void grpc_chttp2_complete_closure_step(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_stream_global *stream_global,
                                       grpc_closure **pclosure, int success);

void grpc_chttp2_run_with_global_lock(grpc_exec_ctx *exec_ctx,
                                      grpc_chttp2_transport *transport,
                                      grpc_chttp2_stream *optional_stream,
                                      grpc_chttp2_locked_action action,
                                      void *arg, size_t sizeof_arg);

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

typedef enum {
  GRPC_CHTTP2_FLOWCTL_MOVE,
  GRPC_CHTTP2_FLOWCTL_CREDIT,
  GRPC_CHTTP2_FLOWCTL_DEBIT
} grpc_chttp2_flowctl_op;

#define GRPC_CHTTP2_FLOW_MOVE_COMMON(phase, transport, id1, id2, dst_context, \
                                     dst_var, src_context, src_var)           \
  do {                                                                        \
    assert(id1 == id2);                                                       \
    if (grpc_flowctl_trace) {                                                 \
      grpc_chttp2_flowctl_trace(                                              \
          __FILE__, __LINE__, phase, GRPC_CHTTP2_FLOWCTL_MOVE, #dst_context,  \
          #dst_var, #src_context, #src_var, transport->is_client, id1,        \
          dst_context->dst_var, src_context->src_var);                        \
    }                                                                         \
    dst_context->dst_var += src_context->src_var;                             \
    src_context->src_var = 0;                                                 \
  } while (0)

#define GRPC_CHTTP2_FLOW_MOVE_STREAM(phase, transport, dst_context, dst_var, \
                                     src_context, src_var)                   \
  GRPC_CHTTP2_FLOW_MOVE_COMMON(phase, transport, dst_context->id,            \
                               src_context->id, dst_context, dst_var,        \
                               src_context, src_var)
#define GRPC_CHTTP2_FLOW_MOVE_TRANSPORT(phase, dst_context, dst_var,           \
                                        src_context, src_var)                  \
  GRPC_CHTTP2_FLOW_MOVE_COMMON(phase, dst_context, 0, 0, dst_context, dst_var, \
                               src_context, src_var)

#define GRPC_CHTTP2_FLOW_CREDIT_COMMON(phase, transport, id, dst_context,      \
                                       dst_var, amount)                        \
  do {                                                                         \
    if (grpc_flowctl_trace) {                                                  \
      grpc_chttp2_flowctl_trace(__FILE__, __LINE__, phase,                     \
                                GRPC_CHTTP2_FLOWCTL_CREDIT, #dst_context,      \
                                #dst_var, NULL, #amount, transport->is_client, \
                                id, dst_context->dst_var, amount);             \
    }                                                                          \
    dst_context->dst_var += amount;                                            \
  } while (0)

#define GRPC_CHTTP2_FLOW_CREDIT_STREAM(phase, transport, dst_context, dst_var, \
                                       amount)                                 \
  GRPC_CHTTP2_FLOW_CREDIT_COMMON(phase, transport, dst_context->id,            \
                                 dst_context, dst_var, amount)
#define GRPC_CHTTP2_FLOW_CREDIT_TRANSPORT(phase, dst_context, dst_var, amount) \
  GRPC_CHTTP2_FLOW_CREDIT_COMMON(phase, dst_context, 0, dst_context, dst_var,  \
                                 amount)

#define GRPC_CHTTP2_FLOW_DEBIT_COMMON(phase, transport, id, dst_context,       \
                                      dst_var, amount)                         \
  do {                                                                         \
    if (grpc_flowctl_trace) {                                                  \
      grpc_chttp2_flowctl_trace(__FILE__, __LINE__, phase,                     \
                                GRPC_CHTTP2_FLOWCTL_DEBIT, #dst_context,       \
                                #dst_var, NULL, #amount, transport->is_client, \
                                id, dst_context->dst_var, amount);             \
    }                                                                          \
    dst_context->dst_var -= amount;                                            \
  } while (0)

#define GRPC_CHTTP2_FLOW_DEBIT_STREAM(phase, transport, dst_context, dst_var, \
                                      amount)                                 \
  GRPC_CHTTP2_FLOW_DEBIT_COMMON(phase, transport, dst_context->id,            \
                                dst_context, dst_var, amount)
#define GRPC_CHTTP2_FLOW_DEBIT_TRANSPORT(phase, dst_context, dst_var, amount) \
  GRPC_CHTTP2_FLOW_DEBIT_COMMON(phase, dst_context, 0, dst_context, dst_var,  \
                                amount)

void grpc_chttp2_flowctl_trace(const char *file, int line, const char *phase,
                               grpc_chttp2_flowctl_op op, const char *context1,
                               const char *var1, const char *context2,
                               const char *var2, int is_client,
                               uint32_t stream_id, int64_t val1, int64_t val2);

void grpc_chttp2_fake_status(grpc_exec_ctx *exec_ctx,
                             grpc_chttp2_transport_global *transport_global,
                             grpc_chttp2_stream_global *stream,
                             grpc_status_code status, gpr_slice *details);
void grpc_chttp2_mark_stream_closed(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_global *transport_global,
    grpc_chttp2_stream_global *stream_global, int close_reads,
    int close_writes);
void grpc_chttp2_start_writing(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport_global *transport_global);

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define GRPC_CHTTP2_STREAM_REF(stream_global, reason) \
  grpc_chttp2_stream_ref(stream_global, reason)
#define GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, reason) \
  grpc_chttp2_stream_unref(exec_ctx, stream_global, reason)
void grpc_chttp2_stream_ref(grpc_chttp2_stream_global *stream_global,
                            const char *reason);
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx,
                              grpc_chttp2_stream_global *stream_global,
                              const char *reason);
#else
#define GRPC_CHTTP2_STREAM_REF(stream_global, reason) \
  grpc_chttp2_stream_ref(stream_global)
#define GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream_global, reason) \
  grpc_chttp2_stream_unref(exec_ctx, stream_global)
void grpc_chttp2_stream_ref(grpc_chttp2_stream_global *stream_global);
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx,
                              grpc_chttp2_stream_global *stream_global);
#endif

grpc_chttp2_incoming_byte_stream *grpc_chttp2_incoming_byte_stream_create(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport_parsing *transport_parsing,
    grpc_chttp2_stream_parsing *stream_parsing, uint32_t frame_size,
    uint32_t flags, grpc_chttp2_incoming_frame_queue *add_to_queue);
void grpc_chttp2_incoming_byte_stream_push(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_incoming_byte_stream *bs,
                                           gpr_slice slice);
void grpc_chttp2_incoming_byte_stream_finished(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_byte_stream *bs, int success,
    int from_parsing_thread);

void grpc_chttp2_ack_ping(grpc_exec_ctx *exec_ctx,
                          grpc_chttp2_transport_parsing *parsing,
                          const uint8_t *opaque_8bytes);

/** add a ref to the stream and add it to the writable list;
    ref will be dropped in writing.c */
void grpc_chttp2_become_writable(grpc_chttp2_transport_global *transport_global,
                                 grpc_chttp2_stream_global *stream_global);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INTERNAL_H */
