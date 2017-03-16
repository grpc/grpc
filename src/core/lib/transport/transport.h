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

#ifndef GRPC_CORE_LIB_TRANSPORT_TRANSPORT_H
#define GRPC_CORE_LIB_TRANSPORT_TRANSPORT_H

#include <stddef.h>

#include "src/core/lib/channel/context.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/core/lib/transport/metadata_batch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* forward declarations */
typedef struct grpc_transport grpc_transport;

/* grpc_stream doesn't actually exist. It's used as a typesafe
   opaque pointer for whatever data the transport wants to track
   for a stream. */
typedef struct grpc_stream grpc_stream;

//#define GRPC_STREAM_REFCOUNT_DEBUG

typedef struct grpc_stream_refcount {
  gpr_refcount refs;
  grpc_closure destroy;
#ifdef GRPC_STREAM_REFCOUNT_DEBUG
  const char *object_type;
#endif
  grpc_slice_refcount slice_refcount;
} grpc_stream_refcount;

#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_stream_ref_init(grpc_stream_refcount *refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void *cb_arg,
                          const char *object_type);
void grpc_stream_ref(grpc_stream_refcount *refcount, const char *reason);
void grpc_stream_unref(grpc_exec_ctx *exec_ctx, grpc_stream_refcount *refcount,
                       const char *reason);
#define GRPC_STREAM_REF_INIT(rc, ir, cb, cb_arg, objtype) \
  grpc_stream_ref_init(rc, ir, cb, cb_arg, objtype)
#else
void grpc_stream_ref_init(grpc_stream_refcount *refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void *cb_arg);
void grpc_stream_ref(grpc_stream_refcount *refcount);
void grpc_stream_unref(grpc_exec_ctx *exec_ctx, grpc_stream_refcount *refcount);
#define GRPC_STREAM_REF_INIT(rc, ir, cb, cb_arg, objtype) \
  grpc_stream_ref_init(rc, ir, cb, cb_arg)
#endif

/* Wrap a buffer that is owned by some stream object into a slice that shares
   the same refcount */
grpc_slice grpc_slice_from_stream_owned_buffer(grpc_stream_refcount *refcount,
                                               void *buffer, size_t length);

typedef struct {
  uint64_t framing_bytes;
  uint64_t data_bytes;
  uint64_t header_bytes;
} grpc_transport_one_way_stats;

typedef struct grpc_transport_stream_stats {
  grpc_transport_one_way_stats incoming;
  grpc_transport_one_way_stats outgoing;
} grpc_transport_stream_stats;

void grpc_transport_move_one_way_stats(grpc_transport_one_way_stats *from,
                                       grpc_transport_one_way_stats *to);

void grpc_transport_move_stats(grpc_transport_stream_stats *from,
                               grpc_transport_stream_stats *to);

typedef struct {
  grpc_closure closure;
  void *args[2];
} grpc_transport_private_op_data;

/* Transport stream op: a set of operations to perform on a transport
   against a single stream */
typedef struct grpc_transport_stream_op {
  /** Should be enqueued when all requested operations (excluding recv_message
      and recv_initial_metadata which have their own closures) in a given batch
      have been completed. */
  grpc_closure *on_complete;

  /** Is the completion of this op covered by a poller (if false: the op should
      complete independently of some pollset being polled) */
  bool covered_by_poller;

  /** Send initial metadata to the peer, from the provided metadata batch.
      idempotent_request MUST be set if this is non-null */
  grpc_metadata_batch *send_initial_metadata;
  /** Iff send_initial_metadata != NULL, flags associated with
      send_initial_metadata: a bitfield of GRPC_INITIAL_METADATA_xxx */
  uint32_t send_initial_metadata_flags;

  /** Send trailing metadata to the peer, from the provided metadata batch. */
  grpc_metadata_batch *send_trailing_metadata;

  /** Send message data to the peer, from the provided byte stream. */
  grpc_byte_stream *send_message;

  /** Receive initial metadata from the stream, into provided metadata batch. */
  grpc_metadata_batch *recv_initial_metadata;
  bool *recv_idempotent_request;
  bool *recv_cacheable_request;
  /** Should be enqueued when initial metadata is ready to be processed. */
  grpc_closure *recv_initial_metadata_ready;

  /** Receive message data from the stream, into provided byte stream. */
  grpc_byte_stream **recv_message;
  /** Should be enqueued when one message is ready to be processed. */
  grpc_closure *recv_message_ready;

  /** Receive trailing metadata from the stream, into provided metadata batch.
   */
  grpc_metadata_batch *recv_trailing_metadata;

  /** Collect any stats into provided buffer, zero internal stat counters */
  grpc_transport_stream_stats *collect_stats;

  /** If != GRPC_ERROR_NONE, forcefully close this stream.
      The HTTP2 semantics should be:
      - server side: if cancel_error has GRPC_ERROR_INT_GRPC_STATUS, and
        trailing metadata has not been sent, send trailing metadata with status
        and message from cancel_error (use grpc_error_get_status) followed by
        a RST_STREAM with error=GRPC_CHTTP2_NO_ERROR to force a full close
      - at all other times: use grpc_error_get_status to get a status code, and
        convert to a HTTP2 error code using
        grpc_chttp2_grpc_status_to_http2_error. Send a RST_STREAM with this
        error. */
  grpc_error *cancel_error;

  /* Indexes correspond to grpc_context_index enum values */
  grpc_call_context_element *context;

  /***************************************************************************
   * remaining fields are initialized and used at the discretion of the
   * current handler of the op */

  grpc_transport_private_op_data handler_private;
} grpc_transport_stream_op;

/** Transport op: a set of operations to perform on a transport as a whole */
typedef struct grpc_transport_op {
  /** Called when processing of this op is done. */
  grpc_closure *on_consumed;
  /** connectivity monitoring - set connectivity_state to NULL to unsubscribe */
  grpc_closure *on_connectivity_state_change;
  grpc_connectivity_state *connectivity_state;
  /** should the transport be disconnected */
  grpc_error *disconnect_with_error;
  /** what should the goaway contain? */
  grpc_error *goaway_error;
  /** set the callback for accepting new streams;
      this is a permanent callback, unlike the other one-shot closures.
      If true, the callback is set to set_accept_stream_fn, with its
      user_data argument set to set_accept_stream_user_data */
  bool set_accept_stream;
  void (*set_accept_stream_fn)(grpc_exec_ctx *exec_ctx, void *user_data,
                               grpc_transport *transport,
                               const void *server_data);
  void *set_accept_stream_user_data;
  /** add this transport to a pollset */
  grpc_pollset *bind_pollset;
  /** add this transport to a pollset_set */
  grpc_pollset_set *bind_pollset_set;
  /** send a ping, call this back if not NULL */
  grpc_closure *send_ping;

  /***************************************************************************
   * remaining fields are initialized and used at the discretion of the
   * transport implementation */

  grpc_transport_private_op_data transport_private;
} grpc_transport_op;

/* Returns the amount of memory required to store a grpc_stream for this
   transport */
size_t grpc_transport_stream_size(grpc_transport *transport);

/* Initialize transport data for a stream.

   Returns 0 on success, any other (transport-defined) value for failure.
   May assume that stream contains all-zeros.

   Arguments:
     transport   - the transport on which to create this stream
     stream      - a pointer to uninitialized memory to initialize
     server_data - either NULL for a client initiated stream, or a pointer
                   supplied from the accept_stream callback function */
int grpc_transport_init_stream(grpc_exec_ctx *exec_ctx,
                               grpc_transport *transport, grpc_stream *stream,
                               grpc_stream_refcount *refcount,
                               const void *server_data);

void grpc_transport_set_pops(grpc_exec_ctx *exec_ctx, grpc_transport *transport,
                             grpc_stream *stream, grpc_polling_entity *pollent);

/* Destroy transport data for a stream.

   Requires: a recv_batch with final_state == GRPC_STREAM_CLOSED has been
   received by the up-layer. Must not be called in the same call stack as
   recv_frame.

   Arguments:
     transport - the transport on which to create this stream
     stream    - the grpc_stream to destroy (memory is still owned by the
                 caller, but any child memory must be cleaned up) */
void grpc_transport_destroy_stream(grpc_exec_ctx *exec_ctx,
                                   grpc_transport *transport,
                                   grpc_stream *stream, void *and_free_memory);

void grpc_transport_stream_op_finish_with_failure(grpc_exec_ctx *exec_ctx,
                                                  grpc_transport_stream_op *op,
                                                  grpc_error *error);

char *grpc_transport_stream_op_string(grpc_transport_stream_op *op);
char *grpc_transport_op_string(grpc_transport_op *op);

/* Send a batch of operations on a transport

   Takes ownership of any objects contained in ops.

   Arguments:
     transport - the transport on which to initiate the stream
     stream    - the stream on which to send the operations. This must be
                 non-NULL and previously initialized by the same transport.
     op        - a grpc_transport_stream_op specifying the op to perform */
void grpc_transport_perform_stream_op(grpc_exec_ctx *exec_ctx,
                                      grpc_transport *transport,
                                      grpc_stream *stream,
                                      grpc_transport_stream_op *op);

void grpc_transport_perform_op(grpc_exec_ctx *exec_ctx,
                               grpc_transport *transport,
                               grpc_transport_op *op);

/* Send a ping on a transport

   Calls cb with user data when a response is received. */
void grpc_transport_ping(grpc_transport *transport, grpc_closure *cb);

/* Advise peer of pending connection termination. */
void grpc_transport_goaway(grpc_transport *transport, grpc_status_code status,
                           grpc_slice debug_data);

/* Close a transport. Aborts all open streams. */
void grpc_transport_close(grpc_transport *transport);

/* Destroy the transport */
void grpc_transport_destroy(grpc_exec_ctx *exec_ctx, grpc_transport *transport);

/* Get the transports peer */
char *grpc_transport_get_peer(grpc_exec_ctx *exec_ctx,
                              grpc_transport *transport);

/* Get the endpoint used by \a transport */
grpc_endpoint *grpc_transport_get_endpoint(grpc_exec_ctx *exec_ctx,
                                           grpc_transport *transport);

/* Allocate a grpc_transport_op, and preconfigure the on_consumed closure to
   \a on_consumed and then delete the returned transport op */
grpc_transport_op *grpc_make_transport_op(grpc_closure *on_consumed);
/* Allocate a grpc_transport_stream_op, and preconfigure the on_consumed closure
   to \a on_consumed and then delete the returned transport op */
grpc_transport_stream_op *grpc_make_transport_stream_op(
    grpc_closure *on_consumed);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_TRANSPORT_TRANSPORT_H */
