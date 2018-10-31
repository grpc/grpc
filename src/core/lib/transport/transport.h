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

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "src/core/lib/channel/context.h"
#include "src/core/lib/gpr/arena.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/core/lib/transport/metadata_batch.h"

/* Minimum and maximum protocol accepted versions. */
#define GRPC_PROTOCOL_VERSION_MAX_MAJOR 2
#define GRPC_PROTOCOL_VERSION_MAX_MINOR 1
#define GRPC_PROTOCOL_VERSION_MIN_MAJOR 2
#define GRPC_PROTOCOL_VERSION_MIN_MINOR 1

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
void grpc_stream_unref(grpc_stream_refcount* refcount, const char* reason);
#define GRPC_STREAM_REF_INIT(rc, ir, cb, cb_arg, objtype) \
  grpc_stream_ref_init(rc, ir, cb, cb_arg, objtype)
#else
void grpc_stream_ref_init(grpc_stream_refcount* refcount, int initial_refs,
                          grpc_iomgr_cb_func cb, void* cb_arg);
void grpc_stream_ref(grpc_stream_refcount* refcount);
void grpc_stream_unref(grpc_stream_refcount* refcount);
#define GRPC_STREAM_REF_INIT(rc, ir, cb, cb_arg, objtype) \
  grpc_stream_ref_init(rc, ir, cb, cb_arg)
#endif

/* Wrap a buffer that is owned by some stream object into a slice that shares
   the same refcount */
grpc_slice grpc_slice_from_stream_owned_buffer(grpc_stream_refcount* refcount,
                                               void* buffer, size_t length);

struct grpc_transport_one_way_stats {
  uint64_t framing_bytes = 0;
  uint64_t data_bytes = 0;
  uint64_t header_bytes = 0;
};

struct grpc_transport_stream_stats {
  grpc_transport_one_way_stats incoming;
  grpc_transport_one_way_stats outgoing;
};

void grpc_transport_move_one_way_stats(grpc_transport_one_way_stats* from,
                                       grpc_transport_one_way_stats* to);

void grpc_transport_move_stats(grpc_transport_stream_stats* from,
                               grpc_transport_stream_stats* to);

// This struct (which is present in both grpc_transport_stream_op_batch
// and grpc_transport_op_batch) is a convenience to allow filters or
// transports to schedule a closure related to a particular batch without
// having to allocate memory.  The general pattern is to initialize the
// closure with the callback arg set to the batch and extra_arg set to
// whatever state is associated with the handler (e.g., the call element
// or the transport stream object).
//
// Note that this can only be used by the current handler of a given
// batch on the way down the stack (i.e., whichever filter or transport is
// currently handling the batch).  Once a filter or transport passes control
// of the batch to the next handler, it cannot depend on the contents of
// this struct anymore, because the next handler may reuse it.
typedef struct {
  void* extra_arg;
  grpc_closure closure;
} grpc_handler_private_op_data;

typedef struct grpc_transport_stream_op_batch_payload
    grpc_transport_stream_op_batch_payload;

/* Transport stream op: a set of operations to perform on a transport
   against a single stream */
struct grpc_transport_stream_op_batch {
  grpc_transport_stream_op_batch()
      : send_initial_metadata(false),
        send_trailing_metadata(false),
        send_message(false),
        recv_initial_metadata(false),
        recv_message(false),
        recv_trailing_metadata(false),
        cancel_stream(false) {}

  /** Should be scheduled when all of the non-recv operations in the batch
      are complete.

      The recv ops (recv_initial_metadata, recv_message, and
      recv_trailing_metadata) each have their own callbacks.  If a batch
      contains both recv ops and non-recv ops, on_complete should be
      scheduled as soon as the non-recv ops are complete, regardless of
      whether or not the recv ops are complete.  If a batch contains
      only recv ops, on_complete can be null. */
  grpc_closure* on_complete = nullptr;

  /** Values for the stream op (fields set are determined by flags above) */
  grpc_transport_stream_op_batch_payload* payload = nullptr;

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

  /** Cancel this stream with the provided error */
  bool cancel_stream : 1;

  /***************************************************************************
   * remaining fields are initialized and used at the discretion of the
   * current handler of the op */

  grpc_handler_private_op_data handler_private;
};

struct grpc_transport_stream_op_batch_payload {
  explicit grpc_transport_stream_op_batch_payload(
      grpc_call_context_element* context)
      : context(context) {}
  ~grpc_transport_stream_op_batch_payload() {
    // We don't really own `send_message`, so release ownership and let the
    // owner clean the data.
    send_message.send_message.release();
  }

  struct {
    grpc_metadata_batch* send_initial_metadata = nullptr;
    /** Iff send_initial_metadata != NULL, flags associated with
        send_initial_metadata: a bitfield of GRPC_INITIAL_METADATA_xxx */
    uint32_t send_initial_metadata_flags = 0;
    // If non-NULL, will be set by the transport to the peer string (a char*).
    // The transport retains ownership of the string.
    // Note: This pointer may be used by the transport after the
    // send_initial_metadata op is completed.  It must remain valid
    // until the call is destroyed.
    gpr_atm* peer_string = nullptr;
  } send_initial_metadata;

  struct {
    grpc_metadata_batch* send_trailing_metadata = nullptr;
  } send_trailing_metadata;

  struct {
    // The transport (or a filter that decides to return a failure before
    // the op gets down to the transport) takes ownership.
    // The batch's on_complete will not be called until after the byte
    // stream is orphaned.
    grpc_core::OrphanablePtr<grpc_core::ByteStream> send_message;
  } send_message;

  struct {
    grpc_metadata_batch* recv_initial_metadata = nullptr;
    // Flags are used only on the server side.  If non-null, will be set to
    // a bitfield of the GRPC_INITIAL_METADATA_xxx macros (e.g., to
    // indicate if the call is idempotent).
    uint32_t* recv_flags = nullptr;
    /** Should be enqueued when initial metadata is ready to be processed. */
    grpc_closure* recv_initial_metadata_ready = nullptr;
    // If not NULL, will be set to true if trailing metadata is
    // immediately available.  This may be a signal that we received a
    // Trailers-Only response.
    bool* trailing_metadata_available = nullptr;
    // If non-NULL, will be set by the transport to the peer string (a char*).
    // The transport retains ownership of the string.
    // Note: This pointer may be used by the transport after the
    // recv_initial_metadata op is completed.  It must remain valid
    // until the call is destroyed.
    gpr_atm* peer_string = nullptr;
  } recv_initial_metadata;

  struct {
    // Will be set by the transport to point to the byte stream
    // containing a received message.
    // Will be NULL if trailing metadata is received instead of a message.
    grpc_core::OrphanablePtr<grpc_core::ByteStream>* recv_message = nullptr;
    /** Should be enqueued when one message is ready to be processed. */
    grpc_closure* recv_message_ready = nullptr;
  } recv_message;

  struct {
    grpc_metadata_batch* recv_trailing_metadata = nullptr;
    grpc_transport_stream_stats* collect_stats = nullptr;
    /** Should be enqueued when initial metadata is ready to be processed. */
    grpc_closure* recv_trailing_metadata_ready = nullptr;
  } recv_trailing_metadata;

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
    grpc_error* cancel_error = GRPC_ERROR_NONE;
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
  void (*set_accept_stream_fn)(void* user_data, grpc_transport* transport,
                               const void* server_data);
  void* set_accept_stream_user_data;
  /** add this transport to a pollset */
  grpc_pollset* bind_pollset;
  /** add this transport to a pollset_set */
  grpc_pollset_set* bind_pollset_set;
  /** send a ping, if either on_initiate or on_ack is not NULL */
  struct {
    /** Ping may be delayed by the transport, on_initiate callback will be
        called when the ping is actually being sent. */
    grpc_closure* on_initiate;
    /** Called when the ping ack is received */
    grpc_closure* on_ack;
  } send_ping;
  // If true, will reset the channel's connection backoff.
  bool reset_connect_backoff;

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
int grpc_transport_init_stream(grpc_transport* transport, grpc_stream* stream,
                               grpc_stream_refcount* refcount,
                               const void* server_data, gpr_arena* arena);

void grpc_transport_set_pops(grpc_transport* transport, grpc_stream* stream,
                             grpc_polling_entity* pollent);

/* Destroy transport data for a stream.

   Requires: a recv_batch with final_state == GRPC_STREAM_CLOSED has been
   received by the up-layer. Must not be called in the same call stack as
   recv_frame.

   Arguments:
     transport - the transport on which to create this stream
     stream    - the grpc_stream to destroy (memory is still owned by the
                 caller, but any child memory must be cleaned up) */
void grpc_transport_destroy_stream(grpc_transport* transport,
                                   grpc_stream* stream,
                                   grpc_closure* then_schedule_closure);

void grpc_transport_stream_op_batch_finish_with_failure(
    grpc_transport_stream_op_batch* op, grpc_error* error,
    grpc_call_combiner* call_combiner);

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
void grpc_transport_perform_stream_op(grpc_transport* transport,
                                      grpc_stream* stream,
                                      grpc_transport_stream_op_batch* op);

void grpc_transport_perform_op(grpc_transport* transport,
                               grpc_transport_op* op);

/* Send a ping on a transport

   Calls cb with user data when a response is received. */
void grpc_transport_ping(grpc_transport* transport, grpc_closure* cb);

/* Advise peer of pending connection termination. */
void grpc_transport_goaway(grpc_transport* transport, grpc_status_code status,
                           grpc_slice debug_data);

/* Destroy the transport */
void grpc_transport_destroy(grpc_transport* transport);

/* Get the endpoint used by \a transport */
grpc_endpoint* grpc_transport_get_endpoint(grpc_transport* transport);

/* Allocate a grpc_transport_op, and preconfigure the on_consumed closure to
   \a on_consumed and then delete the returned transport op */
grpc_transport_op* grpc_make_transport_op(grpc_closure* on_consumed);
/* Allocate a grpc_transport_stream_op_batch, and preconfigure the on_consumed
   closure
   to \a on_consumed and then delete the returned transport op */
grpc_transport_stream_op_batch* grpc_make_transport_stream_op(
    grpc_closure* on_consumed);

#endif /* GRPC_CORE_LIB_TRANSPORT_TRANSPORT_H */
