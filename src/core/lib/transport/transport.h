/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_TRANSPORT_TRANSPORT_H
#define GRPC_CORE_LIB_TRANSPORT_TRANSPORT_H

#include <stddef.h>

#include "src/core/lib/channel/context.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/support/arena.h"
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

extern grpc_core::DebugOnlyTraceFlag grpc_trace_stream_refcount;

typedef struct grpc_stream_refcount {
  gpr_refcount refs;
  grpc_closure destroy;
#ifndef NDEBUG
  const char* object_type;
#endif
  grpc_slice_refcount slice_refcount;
} grpc_stream_refcount;

#ifndef NDEBUG
void grpc_stream_ref_init(grpc_stream_refcount* refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void* cb_arg,
                          const char* object_type);
void grpc_stream_ref(grpc_stream_refcount* refcount, const char* reason);
void grpc_stream_unref(grpc_exec_ctx* exec_ctx, grpc_stream_refcount* refcount,
                       const char* reason);
#define GRPC_STREAM_REF_INIT(rc, ir, cb, cb_arg, objtype) \
  grpc_stream_ref_init(rc, ir, cb, cb_arg, objtype)
#else
void grpc_stream_ref_init(grpc_stream_refcount* refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void* cb_arg);
void grpc_stream_ref(grpc_stream_refcount* refcount);
void grpc_stream_unref(grpc_exec_ctx* exec_ctx, grpc_stream_refcount* refcount);
#define GRPC_STREAM_REF_INIT(rc, ir, cb, cb_arg, objtype) \
  grpc_stream_ref_init(rc, ir, cb, cb_arg)
#endif

/* Wrap a buffer that is owned by some stream object into a slice that shares
   the same refcount */
grpc_slice grpc_slice_from_stream_owned_buffer(grpc_stream_refcount* refcount,
                                               void* buffer, size_t length);

typedef struct {
  uint64_t framing_bytes;
  uint64_t data_bytes;
  uint64_t header_bytes;
} grpc_transport_one_way_stats;

typedef struct grpc_transport_stream_stats {
  grpc_transport_one_way_stats incoming;
  grpc_transport_one_way_stats outgoing;
} grpc_transport_stream_stats;

void grpc_transport_move_one_way_stats(grpc_transport_one_way_stats* from,
                                       grpc_transport_one_way_stats* to);

void grpc_transport_move_stats(grpc_transport_stream_stats* from,
                               grpc_transport_stream_stats* to);

typedef struct {
  void* extra_arg;
  grpc_closure closure;
} grpc_handler_private_op_data;

typedef struct grpc_transport_stream_op_batch_payload
    grpc_transport_stream_op_batch_payload;

/* Transport stream op: a set of operations to perform on a transport
   against a single stream */
typedef struct grpc_transport_stream_op_batch {
  /** Should be enqueued when all requested operations (excluding recv_message
      and recv_initial_metadata which have their own closures) in a given batch
      have been completed. */
  grpc_closure* on_complete;

  /** Values for the stream op (fields set are determined by flags above) */
  grpc_transport_stream_op_batch_payload* payload;

  /** Send initial metadata to the peer, from the provided metadata batch. */
  bool send_initial_metadata : 1;

  /** Send trailing metadata to the peer, from the provided metadata batch. */
  bool send_trailing_metadata : 1;

  /** Send message data to the peer, from the provided byte stream. */
  bool send_message : 1;

  /** Receive initial metadata from the stream, into provided metadata batch. */
  bool recv_initial_metadata : 1;

  /** Receive message data from the stream, into provided byte stream. */
  bool recv_message : 1;

  /** Receive trailing metadata from the stream, into provided metadata batch.
   */
  bool recv_trailing_metadata : 1;

  /** Collect any stats into provided buffer, zero internal stat counters */
  bool collect_stats : 1;

  /** Cancel this stream with the provided error */
  bool cancel_stream : 1;

  /***************************************************************************
   * remaining fields are initialized and used at the discretion of the
   * current handler of the op */

  grpc_handler_private_op_data handler_private;
} grpc_transport_stream_op_batch;

struct grpc_transport_stream_op_batch_payload {
  struct {
    grpc_metadata_batch* send_initial_metadata;
    /** Iff send_initial_metadata != NULL, flags associated with
        send_initial_metadata: a bitfield of GRPC_INITIAL_METADATA_xxx */
    uint32_t send_initial_metadata_flags;
    // If non-NULL, will be set by the transport to the peer string
    // (a char*, which the caller takes ownership of).
    gpr_atm* peer_string;
  } send_initial_metadata;

  struct {
    grpc_metadata_batch* send_trailing_metadata;
  } send_trailing_metadata;

  struct {
    // The transport (or a filter that decides to return a failure before
    // the op gets down to the transport) is responsible for calling
    // grpc_byte_stream_destroy() on this.
    // The batch's on_complete will not be called until after the byte
    // stream is destroyed.
    grpc_byte_stream* send_message;
  } send_message;

  struct {
    grpc_metadata_batch* recv_initial_metadata;
    uint32_t* recv_flags;
    /** Should be enqueued when initial metadata is ready to be processed. */
    grpc_closure* recv_initial_metadata_ready;
    // If not NULL, will be set to true if trailing metadata is
    // immediately available.  This may be a signal that we received a
    // Trailers-Only response.
    bool* trailing_metadata_available;
    // If non-NULL, will be set by the transport to the peer string
    // (a char*, which the caller takes ownership of).
    gpr_atm* peer_string;
  } recv_initial_metadata;

  struct {
    // Will be set by the transport to point to the byte stream
    // containing a received message.
    // The caller is responsible for calling grpc_byte_stream_destroy()
    // on this byte stream.
    grpc_byte_stream** recv_message;
    /** Should be enqueued when one message is ready to be processed. */
    grpc_closure* recv_message_ready;
  } recv_message;

  struct {
    grpc_metadata_batch* recv_trailing_metadata;
  } recv_trailing_metadata;

  struct {
    grpc_transport_stream_stats* collect_stats;
  } collect_stats;

  /** Forcefully close this stream.
      The HTTP2 semantics should be:
      - server side: if cancel_error has GRPC_ERROR_INT_GRPC_STATUS, and
        trailing metadata has not been sent, send trailing metadata with status
        and message from cancel_error (use grpc_error_get_status) followed by
        a RST_STREAM with error=GRPC_CHTTP2_NO_ERROR to force a full close
      - at all other times: use grpc_error_get_status to get a status code, and
        convert to a HTTP2 error code using
        grpc_chttp2_grpc_status_to_http2_error. Send a RST_STREAM with this
        error. */
  struct {
    // Error contract: the transport that gets this op must cause cancel_error
    //                 to be unref'ed after processing it
    grpc_error* cancel_error;
  } cancel_stream;

  /* Indexes correspond to grpc_context_index enum values */
  grpc_call_context_element* context;
};

/** Transport op: a set of operations to perform on a transport as a whole */
typedef struct grpc_transport_op {
  /** Called when processing of this op is done. */
  grpc_closure* on_consumed;
  /** connectivity monitoring - set connectivity_state to NULL to unsubscribe */
  grpc_closure* on_connectivity_state_change;
  grpc_connectivity_state* connectivity_state;
  /** should the transport be disconnected
   * Error contract: the transport that gets this op must cause
   *                 disconnect_with_error to be unref'ed after processing it */
  grpc_error* disconnect_with_error;
  /** what should the goaway contain?
   * Error contract: the transport that gets this op must cause
   *                 goaway_error to be unref'ed after processing it */
  grpc_error* goaway_error;
  /** set the callback for accepting new streams;
      this is a permanent callback, unlike the other one-shot closures.
      If true, the callback is set to set_accept_stream_fn, with its
      user_data argument set to set_accept_stream_user_data */
  bool set_accept_stream;
  void (*set_accept_stream_fn)(grpc_exec_ctx* exec_ctx, void* user_data,
                               grpc_transport* transport,
                               const void* server_data);
  void* set_accept_stream_user_data;
  /** add this transport to a pollset */
  grpc_pollset* bind_pollset;
  /** add this transport to a pollset_set */
  grpc_pollset_set* bind_pollset_set;
  /** send a ping, call this back if not NULL */
  grpc_closure* send_ping;

  /***************************************************************************
   * remaining fields are initialized and used at the discretion of the
   * transport implementation */

  grpc_handler_private_op_data handler_private;
} grpc_transport_op;

/* Returns the amount of memory required to store a grpc_stream for this
   transport */
size_t grpc_transport_stream_size(grpc_transport* transport);

/* Initialize transport data for a stream.

   Returns 0 on success, any other (transport-defined) value for failure.
   May assume that stream contains all-zeros.

   Arguments:
     transport   - the transport on which to create this stream
     stream      - a pointer to uninitialized memory to initialize
     server_data - either NULL for a client initiated stream, or a pointer
                   supplied from the accept_stream callback function */
int grpc_transport_init_stream(grpc_exec_ctx* exec_ctx,
                               grpc_transport* transport, grpc_stream* stream,
                               grpc_stream_refcount* refcount,
                               const void* server_data, gpr_arena* arena);

void grpc_transport_set_pops(grpc_exec_ctx* exec_ctx, grpc_transport* transport,
                             grpc_stream* stream, grpc_polling_entity* pollent);

/* Destroy transport data for a stream.

   Requires: a recv_batch with final_state == GRPC_STREAM_CLOSED has been
   received by the up-layer. Must not be called in the same call stack as
   recv_frame.

   Arguments:
     transport - the transport on which to create this stream
     stream    - the grpc_stream to destroy (memory is still owned by the
                 caller, but any child memory must be cleaned up) */
void grpc_transport_destroy_stream(grpc_exec_ctx* exec_ctx,
                                   grpc_transport* transport,
                                   grpc_stream* stream,
                                   grpc_closure* then_schedule_closure);

void grpc_transport_stream_op_batch_finish_with_failure(
    grpc_exec_ctx* exec_ctx, grpc_transport_stream_op_batch* op,
    grpc_error* error, grpc_call_combiner* call_combiner);

char* grpc_transport_stream_op_batch_string(grpc_transport_stream_op_batch* op);
char* grpc_transport_op_string(grpc_transport_op* op);

/* Send a batch of operations on a transport

   Takes ownership of any objects contained in ops.

   Arguments:
     transport - the transport on which to initiate the stream
     stream    - the stream on which to send the operations. This must be
                 non-NULL and previously initialized by the same transport.
     op        - a grpc_transport_stream_op_batch specifying the op to perform
   */
void grpc_transport_perform_stream_op(grpc_exec_ctx* exec_ctx,
                                      grpc_transport* transport,
                                      grpc_stream* stream,
                                      grpc_transport_stream_op_batch* op);

void grpc_transport_perform_op(grpc_exec_ctx* exec_ctx,
                               grpc_transport* transport,
                               grpc_transport_op* op);

/* Send a ping on a transport

   Calls cb with user data when a response is received. */
void grpc_transport_ping(grpc_transport* transport, grpc_closure* cb);

/* Advise peer of pending connection termination. */
void grpc_transport_goaway(grpc_transport* transport, grpc_status_code status,
                           grpc_slice debug_data);

/* Close a transport. Aborts all open streams. */
void grpc_transport_close(grpc_transport* transport);

/* Destroy the transport */
void grpc_transport_destroy(grpc_exec_ctx* exec_ctx, grpc_transport* transport);

/* Get the endpoint used by \a transport */
grpc_endpoint* grpc_transport_get_endpoint(grpc_exec_ctx* exec_ctx,
                                           grpc_transport* transport);

/* Allocate a grpc_transport_op, and preconfigure the on_consumed closure to
   \a on_consumed and then delete the returned transport op */
grpc_transport_op* grpc_make_transport_op(grpc_closure* on_consumed);
/* Allocate a grpc_transport_stream_op_batch, and preconfigure the on_consumed
   closure
   to \a on_consumed and then delete the returned transport op */
grpc_transport_stream_op_batch* grpc_make_transport_stream_op(
    grpc_closure* on_consumed);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_TRANSPORT_TRANSPORT_H */
