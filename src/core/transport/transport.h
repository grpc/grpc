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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_TRANSPORT_H
#define GRPC_INTERNAL_CORE_TRANSPORT_TRANSPORT_H

#include <stddef.h>

#include "src/core/iomgr/pollset.h"
#include "src/core/iomgr/pollset_set.h"
#include "src/core/transport/metadata_batch.h"
#include "src/core/transport/byte_stream.h"
#include "src/core/channel/context.h"

/* forward declarations */
typedef struct grpc_transport grpc_transport;

/* grpc_stream doesn't actually exist. It's used as a typesafe
   opaque pointer for whatever data the transport wants to track
   for a stream. */
typedef struct grpc_stream grpc_stream;

typedef struct grpc_stream_refcount {
  gpr_refcount refs;
  grpc_closure destroy;
} grpc_stream_refcount;

/*#define GRPC_STREAM_REFCOUNT_DEBUG*/
#ifdef GRPC_STREAM_REFCOUNT_DEBUG
void grpc_stream_ref(grpc_stream_refcount *refcount, const char *reason);
void grpc_stream_unref(grpc_exec_ctx *exec_ctx, grpc_stream_refcount *refcount,
                       const char *reason);
#else
void grpc_stream_ref(grpc_stream_refcount *refcount);
void grpc_stream_unref(grpc_exec_ctx *exec_ctx, grpc_stream_refcount *refcount);
#endif

/* Transport stream op: a set of operations to perform on a transport
   against a single stream */
typedef struct grpc_transport_stream_op {
  grpc_metadata_batch *send_initial_metadata;
  grpc_metadata_batch *send_trailing_metadata;

  grpc_byte_stream *send_message;

  grpc_metadata_batch *recv_initial_metadata;
  grpc_byte_stream **recv_message;
  grpc_closure *recv_message_ready;
  grpc_metadata_batch *recv_trailing_metadata;

  grpc_closure *on_complete;

  /** If != GRPC_STATUS_OK, cancel this stream */
  grpc_status_code cancel_with_status;

  /** If != GRPC_STATUS_OK, send grpc-status, grpc-message, and close this
      stream for both reading and writing */
  grpc_status_code close_with_status;
  gpr_slice *optional_close_message;

  /* Indexes correspond to grpc_context_index enum values */
  grpc_call_context_element *context;
} grpc_transport_stream_op;

/** Transport op: a set of operations to perform on a transport as a whole */
typedef struct grpc_transport_op {
  /** called when processing of this op is done */
  grpc_closure *on_consumed;
  /** connectivity monitoring - set connectivity_state to NULL to unsubscribe */
  grpc_closure *on_connectivity_state_change;
  grpc_connectivity_state *connectivity_state;
  /** should the transport be disconnected */
  int disconnect;
  /** should we send a goaway?
      after a goaway is sent, once there are no more active calls on
      the transport, the transport should disconnect */
  int send_goaway;
  /** what should the goaway contain? */
  grpc_status_code goaway_status;
  gpr_slice *goaway_message;
  /** set the callback for accepting new streams;
      this is a permanent callback, unlike the other one-shot closures */
  void (*set_accept_stream)(grpc_exec_ctx *exec_ctx, void *user_data,
                            grpc_transport *transport, const void *server_data);
  void *set_accept_stream_user_data;
  /** add this transport to a pollset */
  grpc_pollset *bind_pollset;
  /** add this transport to a pollset_set */
  grpc_pollset_set *bind_pollset_set;
  /** send a ping, call this back if not NULL */
  grpc_closure *send_ping;
} grpc_transport_op;

/* Returns the amount of memory required to store a grpc_stream for this
   transport */
size_t grpc_transport_stream_size(grpc_transport *transport);

/* Initialize transport data for a stream.

   Returns 0 on success, any other (transport-defined) value for failure.

   Arguments:
     transport   - the transport on which to create this stream
     stream      - a pointer to uninitialized memory to initialize
     server_data - either NULL for a client initiated stream, or a pointer
                   supplied from the accept_stream callback function */
int grpc_transport_init_stream(grpc_exec_ctx *exec_ctx,
                               grpc_transport *transport, grpc_stream *stream,
                               grpc_stream_refcount *refcount,
                               const void *server_data);

void grpc_transport_set_pollset(grpc_exec_ctx *exec_ctx,
                                grpc_transport *transport, grpc_stream *stream,
                                grpc_pollset *pollset);

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
                                   grpc_stream *stream);

void grpc_transport_stream_op_finish_with_failure(grpc_exec_ctx *exec_ctx,
                                                  grpc_transport_stream_op *op);

void grpc_transport_stream_op_add_cancellation(grpc_transport_stream_op *op,
                                               grpc_status_code status);

void grpc_transport_stream_op_add_close(grpc_transport_stream_op *op,
                                        grpc_status_code status,
                                        gpr_slice *optional_message);

char *grpc_transport_stream_op_string(grpc_transport_stream_op *op);

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
                           gpr_slice debug_data);

/* Close a transport. Aborts all open streams. */
void grpc_transport_close(grpc_transport *transport);

/* Destroy the transport */
void grpc_transport_destroy(grpc_exec_ctx *exec_ctx, grpc_transport *transport);

/* Get the transports peer */
char *grpc_transport_get_peer(grpc_exec_ctx *exec_ctx,
                              grpc_transport *transport);

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_TRANSPORT_H */
