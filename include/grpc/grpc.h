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

#ifndef GRPC_GRPC_H
#define GRPC_GRPC_H

#include <grpc/status.h>

#include <stddef.h>
#include <grpc/byte_buffer.h>
#include <grpc/support/slice.h>
#include <grpc/support/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! \mainpage GRPC Core
 *
 * \section intro_sec The GRPC Core library is a low-level library designed
 * to be wrapped by higher level libraries.
 *
 * The top-level API is provided in grpc.h. 
 * Security related functionality lives in grpc_security.h.
 */

/** Completion Queues enable notification of the completion of asynchronous
    actions. */
typedef struct grpc_completion_queue grpc_completion_queue;

/** The Channel interface allows creation of Call objects. */
typedef struct grpc_channel grpc_channel;

/** A server listens to some port and responds to request calls */
typedef struct grpc_server grpc_server;

/** A Call represents an RPC. When created, it is in a configuration state
    allowing properties to be set until it is invoked. After invoke, the Call
    can have messages written to it and read from it. */
typedef struct grpc_call grpc_call;

/** Type specifier for grpc_arg */
typedef enum {
  GRPC_ARG_STRING,
  GRPC_ARG_INTEGER,
  GRPC_ARG_POINTER
} grpc_arg_type;

/** A single argument... each argument has a key and a value

    A note on naming keys:
      Keys are namespaced into groups, usually grouped by library, and are
      keys for module XYZ are named XYZ.key1, XYZ.key2, etc. Module names must
      be restricted to the regex [A-Za-z][_A-Za-z0-9]{,15}.
      Key names must be restricted to the regex [A-Za-z][_A-Za-z0-9]{,47}.

    GRPC core library keys are prefixed by grpc.

    Library authors are strongly encouraged to \#define symbolic constants for
    their keys so that it's possible to change them in the future. */
typedef struct {
  grpc_arg_type type;
  char *key;
  union {
    char *string;
    int integer;
    struct {
      void *p;
      void *(*copy)(void *p);
      void (*destroy)(void *p);
    } pointer;
  } value;
} grpc_arg;

/** An array of arguments that can be passed around.

    Used to set optional channel-level configuration.
    These configuration options are modelled as key-value pairs as defined
    by grpc_arg; keys are strings to allow easy backwards-compatible extension
    by arbitrary parties.
    All evaluation is performed at channel creation time (i.e. the values in
    this structure need only live through the creation invocation). */
typedef struct {
  size_t num_args;
  grpc_arg *args;
} grpc_channel_args;

/* Channel argument keys: */
/** Enable census for tracing and stats collection */
#define GRPC_ARG_ENABLE_CENSUS "grpc.census"
/** Maximum number of concurrent incoming streams to allow on a http2
    connection */
#define GRPC_ARG_MAX_CONCURRENT_STREAMS "grpc.max_concurrent_streams"
/** Maximum message length that the channel can receive */
#define GRPC_ARG_MAX_MESSAGE_LENGTH "grpc.max_message_length"
/** Initial sequence number for http2 transports */
#define GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER \
  "grpc.http2.initial_sequence_number"

/** Connectivity state of a channel. */
typedef enum {
  /** channel is idle */
  GRPC_CHANNEL_IDLE,
  /** channel is connecting */
  GRPC_CHANNEL_CONNECTING,
  /** channel is ready for work */
  GRPC_CHANNEL_READY,
  /** channel has seen a failure but expects to recover */
  GRPC_CHANNEL_TRANSIENT_FAILURE,
  /** channel has seen a failure that it cannot recover from */
  GRPC_CHANNEL_FATAL_FAILURE
} grpc_connectivity_state;

/** Result of a grpc call. If the caller satisfies the prerequisites of a
    particular operation, the grpc_call_error returned will be GRPC_CALL_OK.
    Receiving any other value listed here is an indication of a bug in the
    caller. */
typedef enum grpc_call_error {
  /** everything went ok */
  GRPC_CALL_OK = 0,
  /** something failed, we don't know what */
  GRPC_CALL_ERROR,
  /** this method is not available on the server */
  GRPC_CALL_ERROR_NOT_ON_SERVER,
  /** this method is not available on the client */
  GRPC_CALL_ERROR_NOT_ON_CLIENT,
  /** this method must be called before server_accept */
  GRPC_CALL_ERROR_ALREADY_ACCEPTED,
  /** this method must be called before invoke */
  GRPC_CALL_ERROR_ALREADY_INVOKED,
  /** this method must be called after invoke */
  GRPC_CALL_ERROR_NOT_INVOKED,
  /** this call is already finished
      (writes_done or write_status has already been called) */
  GRPC_CALL_ERROR_ALREADY_FINISHED,
  /** there is already an outstanding read/write operation on the call */
  GRPC_CALL_ERROR_TOO_MANY_OPERATIONS,
  /** the flags value was illegal for this call */
  GRPC_CALL_ERROR_INVALID_FLAGS,
  /** invalid metadata was passed to this call */
  GRPC_CALL_ERROR_INVALID_METADATA,
  /** completion queue for notification has not been registered with the 
      server */
  GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE
} grpc_call_error;

/* Write Flags: */
/** Hint that the write may be buffered and need not go out on the wire
    immediately. GRPC is free to buffer the message until the next non-buffered
    write, or until writes_done, but it need not buffer completely or at all. */
#define GRPC_WRITE_BUFFER_HINT (0x00000001u)
/** Force compression to be disabled for a particular write
    (start_write/add_metadata). Illegal on invoke/accept. */
#define GRPC_WRITE_NO_COMPRESS (0x00000002u)
/** Mask of all valid flags. */
#define GRPC_WRITE_USED_MASK (GRPC_WRITE_BUFFER_HINT | GRPC_WRITE_NO_COMPRESS)

/** A single metadata element */
typedef struct grpc_metadata {
  const char *key;
  const char *value;
  size_t value_length;

  /** The following fields are reserved for grpc internal use.
      There is no need to initialize them, and they will be set to garbage during
      calls to grpc. */
  struct {
    void *obfuscated[3];
  } internal_data;
} grpc_metadata;

/** The type of completion (for grpc_event) */
typedef enum grpc_completion_type {
  /** Shutting down */
  GRPC_QUEUE_SHUTDOWN,
  /** No event before timeout */
  GRPC_QUEUE_TIMEOUT,
  /** Operation completion */
  GRPC_OP_COMPLETE
} grpc_completion_type;

/** The result of an operation.

    Returned by a completion queue when the operation started with tag. */
typedef struct grpc_event {
  /** The type of the completion. */
  grpc_completion_type type;
  /** non-zero if the operation was successful, 0 upon failure.
      Only GRPC_OP_COMPLETE can succeed or fail. */
  int success;
  /** The tag passed to grpc_call_start_batch etc to start this operation.
      Only GRPC_OP_COMPLETE has a tag. */
  void *tag;
} grpc_event;

typedef struct {
  size_t count;
  size_t capacity;
  grpc_metadata *metadata;
} grpc_metadata_array;

void grpc_metadata_array_init(grpc_metadata_array *array);
void grpc_metadata_array_destroy(grpc_metadata_array *array);

typedef struct {
  char *method;
  size_t method_capacity;
  char *host;
  size_t host_capacity;
  gpr_timespec deadline;
} grpc_call_details;

void grpc_call_details_init(grpc_call_details *details);
void grpc_call_details_destroy(grpc_call_details *details);

typedef enum {
  /** Send initial metadata: one and only one instance MUST be sent for each
      call, unless the call was cancelled - in which case this can be skipped */
  GRPC_OP_SEND_INITIAL_METADATA = 0,
  /** Send a message: 0 or more of these operations can occur for each call */
  GRPC_OP_SEND_MESSAGE,
  /** Send a close from the client: one and only one instance MUST be sent from
      the client, unless the call was cancelled - in which case this can be 
      skipped */
  GRPC_OP_SEND_CLOSE_FROM_CLIENT,
  /** Send status from the server: one and only one instance MUST be sent from
      the server unless the call was cancelled - in which case this can be 
      skipped */
  GRPC_OP_SEND_STATUS_FROM_SERVER,
  /** Receive initial metadata: one and only one MUST be made on the client, 
      must not be made on the server */
  GRPC_OP_RECV_INITIAL_METADATA,
  /** Receive a message: 0 or more of these operations can occur for each call */
  GRPC_OP_RECV_MESSAGE,
  /** Receive status on the client: one and only one must be made on the client.
     This operation always succeeds, meaning ops paired with this operation
     will also appear to succeed, even though they may not have. In that case
     the status will indicate some failure. */
  GRPC_OP_RECV_STATUS_ON_CLIENT,
  /** Receive close on the server: one and only one must be made on the 
      server */
  GRPC_OP_RECV_CLOSE_ON_SERVER
} grpc_op_type;

/** Operation data: one field for each op type (except SEND_CLOSE_FROM_CLIENT
   which has no arguments) */
typedef struct grpc_op {
  /** Operation type, as defined by grpc_op_type */
  grpc_op_type op;
  /** Write flags bitset for grpc_begin_messages */
  gpr_uint32 flags; 
  union {
    struct {
      size_t count;
      grpc_metadata *metadata;
    } send_initial_metadata;
    grpc_byte_buffer *send_message;
    struct {
      size_t trailing_metadata_count;
      grpc_metadata *trailing_metadata;
      grpc_status_code status;
      const char *status_details;
    } send_status_from_server;
    /** ownership of the array is with the caller, but ownership of the elements
        stays with the call object (ie key, value members are owned by the call
        object, recv_initial_metadata->array is owned by the caller).
        After the operation completes, call grpc_metadata_array_destroy on this
        value, or reuse it in a future op. */
    grpc_metadata_array *recv_initial_metadata;
    /** ownership of the byte buffer is moved to the caller; the caller must call
        grpc_byte_buffer_destroy on this value, or reuse it in a future op. */
    grpc_byte_buffer **recv_message;
    struct {
      /** ownership of the array is with the caller, but ownership of the
          elements stays with the call object (ie key, value members are owned 
          by the call object, trailing_metadata->array is owned by the caller).
          After the operation completes, call grpc_metadata_array_destroy on this
          value, or reuse it in a future op. */
      grpc_metadata_array *trailing_metadata;
      grpc_status_code *status;
      /** status_details is a buffer owned by the application before the op
          completes and after the op has completed. During the operation
          status_details may be reallocated to a size larger than 
          *status_details_capacity, in which case *status_details_capacity will 
          be updated with the new array capacity.

          Pre-allocating space:
          size_t my_capacity = 8;
          char *my_details = gpr_malloc(my_capacity);
          x.status_details = &my_details;
          x.status_details_capacity = &my_capacity;

          Not pre-allocating space:
          size_t my_capacity = 0;
          char *my_details = NULL;
          x.status_details = &my_details;
          x.status_details_capacity = &my_capacity;

          After the call:
          gpr_free(my_details); */
      char **status_details;
      size_t *status_details_capacity;
    } recv_status_on_client;
    struct {
      /** out argument, set to 1 if the call failed in any way (seen as a
          cancellation on the server), or 0 if the call succeeded */
      int *cancelled;
    } recv_close_on_server;
  } data;
} grpc_op;

/** Initialize the grpc library.

    It is not safe to call any other grpc functions before calling this.
    (To avoid overhead, little checking is done, and some things may work. We
    do not warrant that they will continue to do so in future revisions of this
    library). */
void grpc_init(void);

/** Shut down the grpc library.

    No memory is used by grpc after this call returns, nor are any instructions
    executing within the grpc library.
    Prior to calling, all application owned grpc objects must have been
    destroyed. */
void grpc_shutdown(void);

/** Return a string representing the current version of grpc */
const char *grpc_version_string(void);

/** Create a completion queue */
grpc_completion_queue *grpc_completion_queue_create(void);

/** Blocks until an event is available, the completion queue is being shut down,
    or deadline is reached.

    Returns a grpc_event with type GRPC_QUEUE_TIMEOUT on timeout,
    otherwise a grpc_event describing the event that occurred.

    Callers must not call grpc_completion_queue_next and
    grpc_completion_queue_pluck simultaneously on the same completion queue. */
grpc_event grpc_completion_queue_next(grpc_completion_queue *cq,
                                      gpr_timespec deadline);

/** Blocks until an event with tag 'tag' is available, the completion queue is
    being shutdown or deadline is reached.

    Returns a grpc_event with type GRPC_QUEUE_TIMEOUT on timeout,
    otherwise a grpc_event describing the event that occurred.

    Callers must not call grpc_completion_queue_next and
    grpc_completion_queue_pluck simultaneously on the same completion queue. */
grpc_event grpc_completion_queue_pluck(grpc_completion_queue *cq, void *tag,
                                       gpr_timespec deadline);

/** Begin destruction of a completion queue. Once all possible events are
    drained then grpc_completion_queue_next will start to produce
    GRPC_QUEUE_SHUTDOWN events only. At that point it's safe to call
    grpc_completion_queue_destroy.

    After calling this function applications should ensure that no
    NEW work is added to be published on this completion queue. */
void grpc_completion_queue_shutdown(grpc_completion_queue *cq);

/** Destroy a completion queue. The caller must ensure that the queue is
    drained and no threads are executing grpc_completion_queue_next */
void grpc_completion_queue_destroy(grpc_completion_queue *cq);

/** Create a call given a grpc_channel, in order to call 'method'. All
    completions are sent to 'completion_queue'. 'method' and 'host' need only
    live through the invocation of this function. */
grpc_call *grpc_channel_create_call(grpc_channel *channel,
                                    grpc_completion_queue *completion_queue,
                                    const char *method, const char *host,
                                    gpr_timespec deadline);

/** Pre-register a method/host pair on a channel. */
void *grpc_channel_register_call(grpc_channel *channel, const char *method,
                                 const char *host);

/** Create a call given a handle returned from grpc_channel_register_call */
grpc_call *grpc_channel_create_registered_call(
    grpc_channel *channel, grpc_completion_queue *completion_queue,
    void *registered_call_handle, gpr_timespec deadline);

/** Start a batch of operations defined in the array ops; when complete, post a
    completion of type 'tag' to the completion queue bound to the call.
    The order of ops specified in the batch has no significance.
    Only one operation of each type can be active at once in any given
    batch. You must call grpc_completion_queue_next or
    grpc_completion_queue_pluck on the completion queue associated with 'call'
    for work to be performed.
    THREAD SAFETY: access to grpc_call_start_batch in multi-threaded environment
    needs to be synchronized. As an optimization, you may synchronize batches
    containing just send operations independently from batches containing just
    receive operations. */
grpc_call_error grpc_call_start_batch(grpc_call *call, const grpc_op *ops,
                                      size_t nops, void *tag);

/** Returns a newly allocated string representing the endpoint to which this
    call is communicating with. The string is in the uri format accepted by
    grpc_channel_create.
    The returned string should be disposed of with gpr_free(). 

    WARNING: this value is never authenticated or subject to any security
    related code. It must not be used for any authentication related
    functionality. Instead, use grpc_auth_context. */
char *grpc_call_get_peer(grpc_call *call);

/** Return a newly allocated string representing the target a channel was
    created for. */
char *grpc_channel_get_target(grpc_channel *channel);

/** Create a client channel to 'target'. Additional channel level configuration
    MAY be provided by grpc_channel_args, though the expectation is that most
    clients will want to simply pass NULL. See grpc_channel_args definition for
    more on this. The data in 'args' need only live through the invocation of
    this function. */
grpc_channel *grpc_channel_create(const char *target,
                                  const grpc_channel_args *args);

/** Create a lame client: this client fails every operation attempted on it. */
grpc_channel *grpc_lame_client_channel_create(const char *target);

/** Close and destroy a grpc channel */
void grpc_channel_destroy(grpc_channel *channel);

/* Error handling for grpc_call
   Most grpc_call functions return a grpc_error. If the error is not GRPC_OK
   then the operation failed due to some unsatisfied precondition.
   If a grpc_call fails, it's guaranteed that no change to the call state
   has been made. */

/** Called by clients to cancel an RPC on the server.
    Can be called multiple times, from any thread.
    THREAD-SAFETY grpc_call_cancel and grpc_call_cancel_with_status
    are thread-safe, and can be called at any point before grpc_call_destroy
    is called.*/
grpc_call_error grpc_call_cancel(grpc_call *call);

/** Called by clients to cancel an RPC on the server.
    Can be called multiple times, from any thread.
    If a status has not been received for the call, set it to the status code
    and description passed in.
    Importantly, this function does not send status nor description to the
    remote endpoint. */
grpc_call_error grpc_call_cancel_with_status(grpc_call *call,
                                             grpc_status_code status,
                                             const char *description);

/** Destroy a call.
    THREAD SAFETY: grpc_call_destroy is thread-compatible */
void grpc_call_destroy(grpc_call *call);

/** Request notification of a new call. 'cq_for_notification' must
    have been registered to the server via 
    grpc_server_register_completion_queue. */
grpc_call_error grpc_server_request_call(
    grpc_server *server, grpc_call **call, grpc_call_details *details,
    grpc_metadata_array *request_metadata,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag_new);

/** Registers a method in the server.
    Methods to this (host, method) pair will not be reported by
    grpc_server_request_call, but instead be reported by
    grpc_server_request_registered_call when passed the appropriate
    registered_method (as returned by this function).
    Must be called before grpc_server_start.
    Returns NULL on failure. */
void *grpc_server_register_method(grpc_server *server, const char *method,
                                  const char *host);

/** Request notification of a new pre-registered call. 'cq_for_notification' 
    must have been registered to the server via 
    grpc_server_register_completion_queue. */
grpc_call_error grpc_server_request_registered_call(
    grpc_server *server, void *registered_method, grpc_call **call,
    gpr_timespec *deadline, grpc_metadata_array *request_metadata,
    grpc_byte_buffer **optional_payload,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag_new);

/** Create a server. Additional configuration for each incoming channel can
    be specified with args. If no additional configuration is needed, args can
    be NULL. See grpc_channel_args for more. The data in 'args' need only live
    through the invocation of this function. */
grpc_server *grpc_server_create(const grpc_channel_args *args);

/** Register a completion queue with the server. Must be done for any
    notification completion queue that is passed to grpc_server_request_*_call
    and to grpc_server_shutdown_and_notify. Must be performed prior to
    grpc_server_start. */
void grpc_server_register_completion_queue(grpc_server *server,
                                           grpc_completion_queue *cq);

/** Add a HTTP2 over plaintext over tcp listener.
    Returns bound port number on success, 0 on failure.
    REQUIRES: server not started */
int grpc_server_add_http2_port(grpc_server *server, const char *addr);

/** Start a server - tells all listeners to start listening */
void grpc_server_start(grpc_server *server);

/** Begin shutting down a server.
    After completion, no new calls or connections will be admitted.
    Existing calls will be allowed to complete.
    Send a GRPC_OP_COMPLETE event when there are no more calls being serviced.
    Shutdown is idempotent, and all tags will be notified at once if multiple
    grpc_server_shutdown_and_notify calls are made. 'cq' must have been
    registered to this server via grpc_server_register_completion_queue. */
void grpc_server_shutdown_and_notify(grpc_server *server,
                                     grpc_completion_queue *cq, void *tag);

/** Cancel all in-progress calls.
    Only usable after shutdown. */
void grpc_server_cancel_all_calls(grpc_server *server);

/** Destroy a server.
    Shutdown must have completed beforehand (i.e. all tags generated by
    grpc_server_shutdown_and_notify must have been received, and at least
    one call to grpc_server_shutdown_and_notify must have been made). */
void grpc_server_destroy(grpc_server *server);

/** Enable or disable a tracer.

    Tracers (usually controlled by the environment variable GRPC_TRACE)
    allow printf-style debugging on GRPC internals, and are useful for
    tracking down problems in the field.

    Use of this function is not strictly thread-safe, but the
    thread-safety issues raised by it should not be of concern. */
int grpc_tracer_set_enabled(const char *name, int enabled);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_GRPC_H */
