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
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport_impl.h"

/* streams are kept in various linked lists depending on what things need to
   happen to them... this enum labels each list */
typedef enum {
  GRPC_CHTTP2_LIST_WRITABLE,
  GRPC_CHTTP2_LIST_WRITING,
  GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT,
  /** streams that are waiting to start because there are too many concurrent
      streams on the connection */
  GRPC_CHTTP2_LIST_WAITING_FOR_CONCURRENCY,
  STREAM_LIST_COUNT /* must be last */
} grpc_chttp2_stream_list_id;

typedef enum {
  GRPC_CHTTP2_WRITE_STATE_IDLE,
  GRPC_CHTTP2_WRITE_STATE_WRITING,
  GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE,
  GRPC_CHTTP2_WRITE_STATE_WRITING_WITH_MORE_AND_COVERED_BY_POLLER,
} grpc_chttp2_write_state;

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

typedef enum {
  GRPC_CHTTP2_NO_GOAWAY_SEND,
  GRPC_CHTTP2_GOAWAY_SEND_SCHEDULED,
  GRPC_CHTTP2_GOAWAY_SENT,
} grpc_chttp2_sent_goaway_state;

/* Outstanding ping request data */
typedef struct grpc_chttp2_outstanding_ping {
  uint8_t id[8];
  grpc_closure *on_recv;
  struct grpc_chttp2_outstanding_ping *next;
  struct grpc_chttp2_outstanding_ping *prev;
} grpc_chttp2_outstanding_ping;

typedef struct grpc_chttp2_write_cb {
  int64_t call_at_byte;
  grpc_closure *closure;
  struct grpc_chttp2_write_cb *next;
} grpc_chttp2_write_cb;

/* forward declared in frame_data.h */
struct grpc_chttp2_incoming_byte_stream {
  grpc_byte_stream base;
  gpr_refcount refs;
  struct grpc_chttp2_incoming_byte_stream *next_message;
  grpc_error *error;

  grpc_chttp2_transport *transport;
  grpc_chttp2_stream *stream;
  bool is_tail;

  gpr_mu slice_mu;  // protects slices, on_next
  grpc_slice_buffer slices;
  grpc_closure *on_next;
  grpc_slice *next;
  uint32_t remaining_bytes;

  struct {
    grpc_closure closure;
    grpc_slice *slice;
    size_t max_size_hint;
    grpc_closure *on_complete;
  } next_action;
  grpc_closure destroy_action;
  grpc_closure finished_action;
};

struct grpc_chttp2_transport {
  grpc_transport base; /* must be first */
  gpr_refcount refs;
  grpc_endpoint *ep;
  char *peer_string;

  grpc_combiner *combiner;

  /** write execution state of the transport */
  grpc_chttp2_write_state write_state;

  /** is the transport destroying itself? */
  uint8_t destroying;
  /** has the upper layer closed the transport? */
  uint8_t closed;

  /** is there a read request to the endpoint outstanding? */
  uint8_t endpoint_reading;

  /** various lists of streams */
  grpc_chttp2_stream_list lists[STREAM_LIST_COUNT];

  /** maps stream id to grpc_chttp2_stream objects */
  grpc_chttp2_stream_map stream_map;

  grpc_closure write_action_begin_locked;
  grpc_closure write_action;
  grpc_closure write_action_end_locked;

  grpc_closure read_action_locked;

  /** incoming read bytes */
  grpc_slice_buffer read_buffer;

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

  /** data to write now */
  grpc_slice_buffer outbuf;
  /** hpack encoding */
  grpc_chttp2_hpack_compressor hpack_compressor;
  int64_t outgoing_window;
  /** is this a client? */
  uint8_t is_client;

  /** data to write next write */
  grpc_slice_buffer qbuf;

  /** window available to announce to peer */
  int64_t announce_incoming_window;
  /** how much window would we like to have for incoming_window */
  uint32_t connection_window_target;

  /** have we seen a goaway */
  uint8_t seen_goaway;
  /** have we sent a goaway */
  grpc_chttp2_sent_goaway_state sent_goaway_state;

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

  /** last new stream id */
  uint32_t last_new_stream_id;

  /** pings awaiting responses */
  grpc_chttp2_outstanding_ping pings;
  /** next payload for an outgoing ping */
  uint64_t ping_counter;

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

  /** initial window change */
  int64_t initial_window_update;

  /** window available for peer to send to us */
  int64_t incoming_window;

  /* deframing */
  grpc_chttp2_deframe_transport_state deframe_state;
  uint8_t incoming_frame_type;
  uint8_t incoming_frame_flags;
  uint8_t header_eof;
  bool is_first_frame;
  uint32_t expect_continuation_stream_id;
  uint32_t incoming_frame_size;
  uint32_t incoming_stream_id;

  /* active parser */
  void *parser_data;
  grpc_chttp2_stream *incoming_stream;
  grpc_error *(*parser)(grpc_exec_ctx *exec_ctx, void *parser_user_data,
                        grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                        grpc_slice slice, int is_last);

  /* goaway data */
  grpc_status_code goaway_error;
  uint32_t goaway_last_stream_index;
  grpc_slice goaway_text;

  grpc_chttp2_write_cb *write_cb_pool;

  /* if non-NULL, close the transport with this error when writes are finished
   */
  grpc_error *close_transport_on_writes_finished;

  /* a list of closures to run after writes are finished */
  grpc_closure_list run_after_write;

  /* buffer pool state */
  /** have we scheduled a benign cleanup? */
  bool benign_reclaimer_registered;
  /** have we scheduled a destructive cleanup? */
  bool destructive_reclaimer_registered;
  /** benign cleanup closure */
  grpc_closure benign_reclaimer_locked;
  /** destructive cleanup closure */
  grpc_closure destructive_reclaimer_locked;
};

typedef enum {
  GRPC_METADATA_NOT_PUBLISHED,
  GRPC_METADATA_SYNTHESIZED_FROM_FAKE,
  GRPC_METADATA_PUBLISHED_FROM_WIRE,
  GPRC_METADATA_PUBLISHED_AT_CLOSE
} grpc_published_metadata_method;

struct grpc_chttp2_stream {
  grpc_chttp2_transport *t;
  grpc_stream_refcount *refcount;

  grpc_closure destroy_stream;
  void *destroy_stream_arg;

  grpc_chttp2_stream_link links[STREAM_LIST_COUNT];
  uint8_t included[STREAM_LIST_COUNT];

  /** HTTP2 stream id for this stream, or zero if one has not been assigned */
  uint32_t id;

  /** window available for us to send to peer */
  int64_t outgoing_window;
  /** The number of bytes the upper layers have offered to receive.
      As the upper layer offers more bytes, this value increases.
      As bytes are read, this value decreases. */
  uint32_t max_recv_bytes;
  /** things the upper layers would like to send */
  grpc_metadata_batch *send_initial_metadata;
  grpc_closure *send_initial_metadata_finished;
  grpc_metadata_batch *send_trailing_metadata;
  grpc_closure *send_trailing_metadata_finished;

  grpc_byte_stream *fetching_send_message;
  uint32_t fetched_send_message_length;
  grpc_slice fetching_slice;
  int64_t next_message_end_offset;
  int64_t flow_controlled_bytes_written;
  bool complete_fetch_covered_by_poller;
  grpc_closure complete_fetch;
  grpc_closure complete_fetch_locked;
  grpc_closure *fetching_send_message_finished;

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
  /** Has this stream seen an error.
      If true, then pending incoming frames can be thrown away. */
  bool seen_error;

  /** the error that resulted in this stream being read-closed */
  grpc_error *read_closed_error;
  /** the error that resulted in this stream being write-closed */
  grpc_error *write_closed_error;

  grpc_published_metadata_method published_metadata[2];
  bool final_metadata_requested;

  grpc_chttp2_incoming_metadata_buffer metadata_buffer[2];

  grpc_chttp2_incoming_frame_queue incoming_frames;

  gpr_timespec deadline;

  /** saw some stream level error */
  grpc_error *forced_close_error;
  /** how many header frames have we received? */
  uint8_t header_frames_received;
  /** window available for peer to send to us */
  int64_t incoming_window;
  /** parsing state for data frames */
  grpc_chttp2_data_parser data_parser;
  /** number of bytes received - reset at end of parse thread execution */
  int64_t received_bytes;

  bool sent_initial_metadata;
  bool sent_trailing_metadata;
  /** how much window should we announce? */
  uint32_t announce_window;
  grpc_slice_buffer flow_controlled_buffer;

  grpc_chttp2_write_cb *on_write_finished_cbs;
  grpc_chttp2_write_cb *finish_after_write;
  size_t sending_bytes;
};

/** Transport writing call flow:
    grpc_chttp2_initiate_write() is called anywhere that we know bytes need to
    go out on the wire.
    If no other write has been started, a task is enqueued onto our workqueue.
    When that task executes, it obtains the global lock, and gathers the data
    to write.
    The global lock is dropped and we do the syscall to write.
    After writing, a follow-up check is made to see if another round of writing
    should be performed.

    The actual call chain is documented in the implementation of this function.
    */
void grpc_chttp2_initiate_write(grpc_exec_ctx *exec_ctx,
                                grpc_chttp2_transport *t,
                                bool covered_by_poller, const char *reason);

/** Someone is unlocking the transport mutex: check to see if writes
    are required, and frame them if so */
bool grpc_chttp2_begin_write(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t);
void grpc_chttp2_end_write(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                           grpc_error *error);

/** Process one slice of incoming data; return 1 if the connection is still
    viable after reading, or 0 if the connection should be torn down */
grpc_error *grpc_chttp2_perform_read(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_slice slice);

bool grpc_chttp2_list_add_writable_stream(grpc_chttp2_transport *t,
                                          grpc_chttp2_stream *s);
/** Get a writable stream
    returns non-zero if there was a stream available */
int grpc_chttp2_list_pop_writable_stream(grpc_chttp2_transport *t,
                                         grpc_chttp2_stream **s);
bool grpc_chttp2_list_remove_writable_stream(
    grpc_chttp2_transport *t, grpc_chttp2_stream *s) GRPC_MUST_USE_RESULT;

bool grpc_chttp2_list_add_writing_stream(grpc_chttp2_transport *t,
                                         grpc_chttp2_stream *s);
int grpc_chttp2_list_have_writing_streams(grpc_chttp2_transport *t);
int grpc_chttp2_list_pop_writing_stream(grpc_chttp2_transport *t,
                                        grpc_chttp2_stream **s);

void grpc_chttp2_list_add_written_stream(grpc_chttp2_transport *t,
                                         grpc_chttp2_stream *s);
int grpc_chttp2_list_pop_written_stream(grpc_chttp2_transport *t,
                                        grpc_chttp2_stream **s);

void grpc_chttp2_list_add_waiting_for_concurrency(grpc_chttp2_transport *t,
                                                  grpc_chttp2_stream *s);
int grpc_chttp2_list_pop_waiting_for_concurrency(grpc_chttp2_transport *t,
                                                 grpc_chttp2_stream **s);
void grpc_chttp2_list_remove_waiting_for_concurrency(grpc_chttp2_transport *t,
                                                     grpc_chttp2_stream *s);

void grpc_chttp2_list_add_stalled_by_transport(grpc_chttp2_transport *t,
                                               grpc_chttp2_stream *s);
int grpc_chttp2_list_pop_stalled_by_transport(grpc_chttp2_transport *t,
                                              grpc_chttp2_stream **s);
void grpc_chttp2_list_remove_stalled_by_transport(grpc_chttp2_transport *t,
                                                  grpc_chttp2_stream *s);

grpc_chttp2_stream *grpc_chttp2_parsing_lookup_stream(grpc_chttp2_transport *t,
                                                      uint32_t id);
grpc_chttp2_stream *grpc_chttp2_parsing_accept_stream(grpc_exec_ctx *exec_ctx,
                                                      grpc_chttp2_transport *t,
                                                      uint32_t id);

void grpc_chttp2_add_incoming_goaway(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     uint32_t goaway_error,
                                     grpc_slice goaway_text);

void grpc_chttp2_parsing_become_skip_parser(grpc_exec_ctx *exec_ctx,
                                            grpc_chttp2_transport *t);

void grpc_chttp2_complete_closure_step(grpc_exec_ctx *exec_ctx,
                                       grpc_chttp2_transport *t,
                                       grpc_chttp2_stream *s,
                                       grpc_closure **pclosure,
                                       grpc_error *error, const char *desc);

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

void grpc_chttp2_fake_status(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                             grpc_chttp2_stream *stream,
                             grpc_status_code status, grpc_slice *details);
void grpc_chttp2_mark_stream_closed(grpc_exec_ctx *exec_ctx,
                                    grpc_chttp2_transport *t,
                                    grpc_chttp2_stream *s, int close_reads,
                                    int close_writes, grpc_error *error);
void grpc_chttp2_start_writing(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport *t);

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
#define GRPC_CHTTP2_STREAM_REF(stream, reason) \
  grpc_chttp2_stream_ref(stream, reason)
#define GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream, reason) \
  grpc_chttp2_stream_unref(exec_ctx, stream, reason)
void grpc_chttp2_stream_ref(grpc_chttp2_stream *s, const char *reason);
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx, grpc_chttp2_stream *s,
                              const char *reason);
#else
#define GRPC_CHTTP2_STREAM_REF(stream, reason) grpc_chttp2_stream_ref(stream)
#define GRPC_CHTTP2_STREAM_UNREF(exec_ctx, stream, reason) \
  grpc_chttp2_stream_unref(exec_ctx, stream)
void grpc_chttp2_stream_ref(grpc_chttp2_stream *s);
void grpc_chttp2_stream_unref(grpc_exec_ctx *exec_ctx, grpc_chttp2_stream *s);
#endif

//#define GRPC_CHTTP2_REFCOUNTING_DEBUG 1
#ifdef GRPC_CHTTP2_REFCOUNTING_DEBUG
#define GRPC_CHTTP2_REF_TRANSPORT(t, r) \
  grpc_chttp2_ref_transport(t, r, __FILE__, __LINE__)
#define GRPC_CHTTP2_UNREF_TRANSPORT(cl, t, r) \
  grpc_chttp2_unref_transport(cl, t, r, __FILE__, __LINE__)
void grpc_chttp2_unref_transport(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport *t, const char *reason,
                                 const char *file, int line);
void grpc_chttp2_ref_transport(grpc_chttp2_transport *t, const char *reason,
                               const char *file, int line);
#else
#define GRPC_CHTTP2_REF_TRANSPORT(t, r) grpc_chttp2_ref_transport(t)
#define GRPC_CHTTP2_UNREF_TRANSPORT(cl, t, r) grpc_chttp2_unref_transport(cl, t)
void grpc_chttp2_unref_transport(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport *t);
void grpc_chttp2_ref_transport(grpc_chttp2_transport *t);
#endif

grpc_chttp2_incoming_byte_stream *grpc_chttp2_incoming_byte_stream_create(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t, grpc_chttp2_stream *s,
    uint32_t frame_size, uint32_t flags);
void grpc_chttp2_incoming_byte_stream_push(grpc_exec_ctx *exec_ctx,
                                           grpc_chttp2_incoming_byte_stream *bs,
                                           grpc_slice slice);
void grpc_chttp2_incoming_byte_stream_finished(
    grpc_exec_ctx *exec_ctx, grpc_chttp2_incoming_byte_stream *bs,
    grpc_error *error);

void grpc_chttp2_ack_ping(grpc_exec_ctx *exec_ctx, grpc_chttp2_transport *t,
                          const uint8_t *opaque_8bytes);

/** add a ref to the stream and add it to the writable list;
    ref will be dropped in writing.c */
void grpc_chttp2_become_writable(grpc_exec_ctx *exec_ctx,
                                 grpc_chttp2_transport *t,
                                 grpc_chttp2_stream *s, bool covered_by_poller,
                                 const char *reason);

void grpc_chttp2_cancel_stream(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_transport *t, grpc_chttp2_stream *s,
                               grpc_error *due_to_error);

void grpc_chttp2_maybe_complete_recv_initial_metadata(grpc_exec_ctx *exec_ctx,
                                                      grpc_chttp2_transport *t,
                                                      grpc_chttp2_stream *s);
void grpc_chttp2_maybe_complete_recv_message(grpc_exec_ctx *exec_ctx,
                                             grpc_chttp2_transport *t,
                                             grpc_chttp2_stream *s);
void grpc_chttp2_maybe_complete_recv_trailing_metadata(grpc_exec_ctx *exec_ctx,
                                                       grpc_chttp2_transport *t,
                                                       grpc_chttp2_stream *s);

void grpc_chttp2_fail_pending_writes(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_transport *t,
                                     grpc_chttp2_stream *s, grpc_error *error);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INTERNAL_H */
