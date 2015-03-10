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
#include <grpc/support/slice.h>
#include <grpc/support/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Completion Queues enable notification of the completion of asynchronous
   actions. */
typedef struct grpc_completion_queue grpc_completion_queue;

/* The Channel interface allows creation of Call objects. */
typedef struct grpc_channel grpc_channel;

/* A server listens to some port and responds to request calls */
typedef struct grpc_server grpc_server;

/* A Call represents an RPC. When created, it is in a configuration state
   allowing properties to be set until it is invoked. After invoke, the Call
   can have messages written to it and read from it. */
typedef struct grpc_call grpc_call;

/* Type specifier for grpc_arg */
typedef enum {
  GRPC_ARG_STRING,
  GRPC_ARG_INTEGER,
  GRPC_ARG_POINTER
} grpc_arg_type;

/* A single argument... each argument has a key and a value

   A note on naming keys:
     Keys are namespaced into groups, usually grouped by library, and are
     keys for module XYZ are named XYZ.key1, XYZ.key2, etc. Module names must
     be restricted to the regex [A-Za-z][_A-Za-z0-9]{,15}.
     Key names must be restricted to the regex [A-Za-z][_A-Za-z0-9]{,47}.

     GRPC core library keys are prefixed by grpc.

     Library authors are strongly encouraged to #define symbolic constants for
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

/* An array of arguments that can be passed around.
   Used to set optional channel-level configuration.
   These configuration options are modelled as key-value pairs as defined
   by grpc_arg; keys are strings to allow easy backwards-compatible extension
   by arbitrary parties.
   All evaluation is performed at channel creation time. */
typedef struct {
  size_t num_args;
  grpc_arg *args;
} grpc_channel_args;

/* Channel argument keys: */
/* Enable census for tracing and stats collection */
#define GRPC_ARG_ENABLE_CENSUS "grpc.census"
/* Maximum number of concurrent incoming streams to allow on a http2
   connection */
#define GRPC_ARG_MAX_CONCURRENT_STREAMS "grpc.max_concurrent_streams"
/* Maximum message length that the channel can receive */
#define GRPC_ARG_MAX_MESSAGE_LENGTH "grpc.max_message_length"

/* Result of a grpc call. If the caller satisfies the prerequisites of a
   particular operation, the grpc_call_error returned will be GRPC_CALL_OK.
   Receiving any other value listed here is an indication of a bug in the
   caller. */
typedef enum grpc_call_error {
  /* everything went ok */
  GRPC_CALL_OK = 0,
  /* something failed, we don't know what */
  GRPC_CALL_ERROR,
  /* this method is not available on the server */
  GRPC_CALL_ERROR_NOT_ON_SERVER,
  /* this method is not available on the client */
  GRPC_CALL_ERROR_NOT_ON_CLIENT,
  /* this method must be called before server_accept */
  GRPC_CALL_ERROR_ALREADY_ACCEPTED,
  /* this method must be called before invoke */
  GRPC_CALL_ERROR_ALREADY_INVOKED,
  /* this method must be called after invoke */
  GRPC_CALL_ERROR_NOT_INVOKED,
  /* this call is already finished
     (writes_done or write_status has already been called) */
  GRPC_CALL_ERROR_ALREADY_FINISHED,
  /* there is already an outstanding read/write operation on the call */
  GRPC_CALL_ERROR_TOO_MANY_OPERATIONS,
  /* the flags value was illegal for this call */
  GRPC_CALL_ERROR_INVALID_FLAGS
} grpc_call_error;

/* Result of a grpc operation */
typedef enum grpc_op_error {
  /* everything went ok */
  GRPC_OP_OK = 0,
  /* something failed, we don't know what */
  GRPC_OP_ERROR
} grpc_op_error;

/* Write Flags: */
/* Hint that the write may be buffered and need not go out on the wire
   immediately. GRPC is free to buffer the message until the next non-buffered
   write, or until writes_done, but it need not buffer completely or at all. */
#define GRPC_WRITE_BUFFER_HINT (0x00000001u)
/* Force compression to be disabled for a particular write
   (start_write/add_metadata). Illegal on invoke/accept. */
#define GRPC_WRITE_NO_COMPRESS (0x00000002u)

/* A buffer of bytes */
struct grpc_byte_buffer;
typedef struct grpc_byte_buffer grpc_byte_buffer;

/* Sample helpers to obtain byte buffers (these will certainly move
   someplace else) */
grpc_byte_buffer *grpc_byte_buffer_create(gpr_slice *slices, size_t nslices);
grpc_byte_buffer *grpc_byte_buffer_copy(grpc_byte_buffer *bb);
size_t grpc_byte_buffer_length(grpc_byte_buffer *bb);
void grpc_byte_buffer_destroy(grpc_byte_buffer *byte_buffer);

/* Reader for byte buffers. Iterates over slices in the byte buffer */
struct grpc_byte_buffer_reader;
typedef struct grpc_byte_buffer_reader grpc_byte_buffer_reader;

grpc_byte_buffer_reader *grpc_byte_buffer_reader_create(
    grpc_byte_buffer *buffer);
/* At the end of the stream, returns 0. Otherwise, returns 1 and sets slice to
   be the returned slice. Caller is responsible for calling gpr_slice_unref on
   the result. */
int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader *reader,
                                 gpr_slice *slice);
void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader *reader);

/* A single metadata element */
typedef struct grpc_metadata {
  const char *key;
  const char *value;
  size_t value_length;
} grpc_metadata;

typedef enum grpc_completion_type {
  GRPC_QUEUE_SHUTDOWN,       /* Shutting down */
  GRPC_OP_COMPLETE,          /* operation completion */
  GRPC_READ,                 /* A read has completed */
  GRPC_WRITE_ACCEPTED,       /* A write has been accepted by
                                flow control */
  GRPC_FINISH_ACCEPTED,      /* writes_done or write_status has been accepted */
  GRPC_CLIENT_METADATA_READ, /* The metadata array sent by server received at
                                client */
  GRPC_FINISHED,             /* An RPC has finished. The event contains status.
                                On the server this will be OK or Cancelled. */
  GRPC_SERVER_RPC_NEW,       /* A new RPC has arrived at the server */
  GRPC_SERVER_SHUTDOWN,      /* The server has finished shutting down */
  GRPC_COMPLETION_DO_NOT_USE /* must be last, forces users to include
                                a default: case */
} grpc_completion_type;

typedef struct grpc_event {
  grpc_completion_type type;
  void *tag;
  grpc_call *call;
  /* Data associated with the completion type. Field names match the type of
     completion as listed in grpc_completion_type. */
  union {
    /* Contains a pointer to the buffer that was read, or NULL at the end of a
       stream. */
    grpc_byte_buffer *read;
    grpc_op_error write_accepted;
    grpc_op_error finish_accepted;
    grpc_op_error invoke_accepted;
    grpc_op_error op_complete;
    struct {
      size_t count;
      grpc_metadata *elements;
    } client_metadata_read;
    struct {
      grpc_status_code status;
      const char *details;
      size_t metadata_count;
      grpc_metadata *metadata_elements;
    } finished;
    struct {
      const char *method;
      const char *host;
      gpr_timespec deadline;
      size_t metadata_count;
      grpc_metadata *metadata_elements;
    } server_rpc_new;
  } data;
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
  /* Send initial metadata: one and only one instance MUST be sent for each
     call,
     unless the call was cancelled - in which case this can be skipped */
  GRPC_OP_SEND_INITIAL_METADATA = 0,
  /* Send a message: 0 or more of these operations can occur for each call */
  GRPC_OP_SEND_MESSAGE,
  /* Send a close from the server: one and only one instance MUST be sent from
     the client,
     unless the call was cancelled - in which case this can be skipped */
  GRPC_OP_SEND_CLOSE_FROM_CLIENT,
  /* Send status from the server: one and only one instance MUST be sent from
     the server
     unless the call was cancelled - in which case this can be skipped */
  GRPC_OP_SEND_STATUS_FROM_SERVER,
  /* Receive initial metadata: one and only one MUST be made on the client, must
     not be made on the server */
  GRPC_OP_RECV_INITIAL_METADATA,
  /* Receive a message: 0 or more of these operations can occur for each call */
  GRPC_OP_RECV_MESSAGE,
  /* Receive status on the client: one and only one must be made on the client
     */
  GRPC_OP_RECV_STATUS_ON_CLIENT,
  /* Receive status on the server: one and only one must be made on the server
     */
  GRPC_OP_RECV_CLOSE_ON_SERVER
} grpc_op_type;

/* Operation data: one field for each op type (except SEND_CLOSE_FROM_CLIENT
   which has
   no arguments) */
typedef struct grpc_op {
  grpc_op_type op;
  union {
    struct {
      size_t count;
      const grpc_metadata *metadata;
    } send_initial_metadata;
    grpc_byte_buffer *send_message;
    struct {
      size_t trailing_metadata_count;
      grpc_metadata *trailing_metadata;
      grpc_status_code status;
      const char *status_details;
    } send_status_from_server;
    /* ownership of the array is with the caller, but ownership of the elements
       stays with the call object (ie key, value members are owned by the call
       object, recv_initial_metadata->array is owned by the caller).
       After the operation completes, call grpc_metadata_array_destroy on this
       value, or reuse it in a future op. */
    grpc_metadata_array *recv_initial_metadata;
    grpc_byte_buffer **recv_message;
    struct {
      /* ownership of the array is with the caller, but ownership of the
         elements
         stays with the call object (ie key, value members are owned by the call
         object, trailing_metadata->array is owned by the caller).
         After the operation completes, call grpc_metadata_array_destroy on this
         value, or reuse it in a future op. */
      grpc_metadata_array *trailing_metadata;
      grpc_status_code *status;
      /* status_details is a buffer owned by the application before the op
         completes
         and after the op has completed. During the operation status_details may
         be
         reallocated to a size larger than *status_details_capacity, in which
         case
         *status_details_capacity will be updated with the new array capacity.

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
      /* out argument, set to 1 if the call failed in any way (seen as a
         cancellation
         on the server), or 0 if the call succeeded */
      int *cancelled;
    } recv_close_on_server;
  } data;
} grpc_op;

/* Initialize the grpc library.
   It is not safe to call any other grpc functions before calling this.
   (To avoid overhead, little checking is done, and some things may work. We
   do not warrant that they will continue to do so in future revisions of this
   library). */
void grpc_init(void);

/* Shut down the grpc library. 
   No memory is used by grpc after this call returns, nor are any instructions
   executing within the grpc library.
   Prior to calling, all application owned grpc objects must have been
   destroyed. */
void grpc_shutdown(void);

grpc_completion_queue *grpc_completion_queue_create(void);

/* Blocks until an event is available, the completion queue is being shut down,
   or deadline is reached. Returns NULL on timeout, otherwise the event that
   occurred. Callers should call grpc_event_finish once they have processed
   the event.

   Callers must not call grpc_completion_queue_next and
   grpc_completion_queue_pluck simultaneously on the same completion queue. */
grpc_event *grpc_completion_queue_next(grpc_completion_queue *cq,
                                       gpr_timespec deadline);

/* Blocks until an event with tag 'tag' is available, the completion queue is
   being shutdown or deadline is reached. Returns NULL on timeout, or a pointer
   to the event that occurred. Callers should call grpc_event_finish once they
   have processed the event.

   Callers must not call grpc_completion_queue_next and
   grpc_completion_queue_pluck simultaneously on the same completion queue. */
grpc_event *grpc_completion_queue_pluck(grpc_completion_queue *cq, void *tag,
                                        gpr_timespec deadline);

/* Clean up any data owned by the event */
void grpc_event_finish(grpc_event *event);

/* Begin destruction of a completion queue. Once all possible events are
   drained then grpc_completion_queue_next will start to produce
   GRPC_QUEUE_SHUTDOWN events only. At that point it's safe to call 
   grpc_completion_queue_destroy. 
   
   After calling this function applications should ensure that no
   NEW work is added to be published on this completion queue. */
void grpc_completion_queue_shutdown(grpc_completion_queue *cq);

/* Destroy a completion queue. The caller must ensure that the queue is
   drained and no threads are executing grpc_completion_queue_next */
void grpc_completion_queue_destroy(grpc_completion_queue *cq);

/* Create a call given a grpc_channel, in order to call 'method'. The request
   is not sent until grpc_call_invoke is called. All completions are sent to
   'completion_queue'. */
grpc_call *grpc_channel_create_call_old(grpc_channel *channel,
                                        const char *method, const char *host,
                                        gpr_timespec deadline);

/* Create a call given a grpc_channel, in order to call 'method'. The request
   is not sent until grpc_call_invoke is called. All completions are sent to
   'completion_queue'. */
grpc_call *grpc_channel_create_call(grpc_channel *channel,
                                    grpc_completion_queue *completion_queue,
                                    const char *method, const char *host,
                                    gpr_timespec deadline);

/* Start a batch of operations defined in the array ops; when complete, post a
   completion of type 'tag' to the completion queue bound to the call.
   The order of ops specified in the batch has no significance.
   Only one operation of each type can be active at once in any given
   batch. */
grpc_call_error grpc_call_start_batch(grpc_call *call, const grpc_op *ops,
                                      size_t nops, void *tag);

/* Create a client channel to 'target'. Additional channel level configuration
   MAY be provided by grpc_channel_args, though the expectation is that most
   clients will want to simply pass NULL. See grpc_channel_args definition
   for more on this. */
grpc_channel *grpc_channel_create(const char *target,
                                  const grpc_channel_args *args);

/* Create a lame client: this client fails every operation attempted on it. */
grpc_channel *grpc_lame_client_channel_create(void);

/* Close and destroy a grpc channel */
void grpc_channel_destroy(grpc_channel *channel);

/* THREAD-SAFETY for grpc_call
   The following functions are thread-compatible for any given call:
     grpc_call_add_metadata
     grpc_call_invoke
     grpc_call_start_write
     grpc_call_writes_done
     grpc_call_start_read
     grpc_call_destroy
   The function grpc_call_cancel is thread-safe, and can be called at any
   point before grpc_call_destroy is called. */

/* Error handling for grpc_call
   Most grpc_call functions return a grpc_error. If the error is not GRPC_OK
   then the operation failed due to some unsatisfied precondition.
   If a grpc_call fails, it's guaranteed that no change to the call state
   has been made. */

/* Add a single metadata element to the call, to be sent upon invocation.
   flags is a bit-field combination of the write flags defined above.
   REQUIRES: grpc_call_start_invoke/grpc_call_server_end_initial_metadata have
             not been called on this call.
   Produces no events. */
grpc_call_error grpc_call_add_metadata_old(grpc_call *call,
                                           grpc_metadata *metadata,
                                           gpr_uint32 flags);

/* Invoke the RPC. Starts sending metadata and request headers on the wire.
   flags is a bit-field combination of the write flags defined above.
   REQUIRES: Can be called at most once per call.
             Can only be called on the client.
   Produces a GRPC_CLIENT_METADATA_READ event with metadata_read_tag when
       the servers initial metadata has been read.
   Produces a GRPC_FINISHED event with finished_tag when the call has been
       completed (there may be other events for the call pending at this
       time) */
grpc_call_error grpc_call_invoke_old(grpc_call *call, grpc_completion_queue *cq,
                                     void *metadata_read_tag,
                                     void *finished_tag, gpr_uint32 flags);

/* Accept an incoming RPC, binding a completion queue to it.
   To be called before sending or receiving messages.
   REQUIRES: Can be called at most once per call.
             Can only be called on the server.
   Produces a GRPC_FINISHED event with finished_tag when the call has been
       completed (there may be other events for the call pending at this
       time) */
grpc_call_error grpc_call_server_accept_old(grpc_call *call,
                                            grpc_completion_queue *cq,
                                            void *finished_tag);

/* Start sending metadata.
   To be called before sending messages.
   flags is a bit-field combination of the write flags defined above.
   REQUIRES: Can be called at most once per call.
             Can only be called on the server.
             Must be called after grpc_call_server_accept */
grpc_call_error grpc_call_server_end_initial_metadata_old(grpc_call *call,
                                                          gpr_uint32 flags);

/* Called by clients to cancel an RPC on the server.
   Can be called multiple times, from any thread. */
grpc_call_error grpc_call_cancel(grpc_call *call);

/* Called by clients to cancel an RPC on the server.
   Can be called multiple times, from any thread.
   If a status has not been received for the call, set it to the status code
   and description passed in.
   Importantly, this function does not send status nor description to the
   remote endpoint. */
grpc_call_error grpc_call_cancel_with_status(grpc_call *call,
                                             grpc_status_code status,
                                             const char *description);

/* Queue a byte buffer for writing.
   flags is a bit-field combination of the write flags defined above.
   A write with byte_buffer null is allowed, and will not send any bytes on the
   wire. If this is performed without GRPC_WRITE_BUFFER_HINT flag it provides
   a mechanism to flush any previously buffered writes to outgoing flow control.
   REQUIRES: No other writes are pending on the call. It is only safe to
             start the next write after the corresponding write_accepted event
             is received.
             GRPC_INVOKE_ACCEPTED must have been received by the application
             prior to calling this on the client. On the server,
             grpc_call_server_end_of_initial_metadata must have been called
             successfully.
   Produces a GRPC_WRITE_ACCEPTED event. */
grpc_call_error grpc_call_start_write_old(grpc_call *call,
                                          grpc_byte_buffer *byte_buffer,
                                          void *tag, gpr_uint32 flags);

/* Queue a status for writing.
   REQUIRES: No other writes are pending on the call.
             grpc_call_server_end_initial_metadata must have been called on the
             call prior to calling this.
             Only callable on the server.
   Produces a GRPC_FINISH_ACCEPTED event when the status is sent. */
grpc_call_error grpc_call_start_write_status_old(grpc_call *call,
                                                 grpc_status_code status_code,
                                                 const char *status_message,
                                                 void *tag);

/* No more messages to send.
   REQUIRES: No other writes are pending on the call.
             Only callable on the client.
   Produces a GRPC_FINISH_ACCEPTED event when all bytes for the call have passed
       outgoing flow control. */
grpc_call_error grpc_call_writes_done_old(grpc_call *call, void *tag);

/* Initiate a read on a call. Output event contains a byte buffer with the
   result of the read.
   REQUIRES: No other reads are pending on the call. It is only safe to start
             the next read after the corresponding read event is received.
             On the client:
               GRPC_INVOKE_ACCEPTED must have been received by the application
               prior to calling this.
             On the server:
               grpc_call_server_accept must be called before calling this.
   Produces a single GRPC_READ event. */
grpc_call_error grpc_call_start_read_old(grpc_call *call, void *tag);

/* Destroy a call. */
void grpc_call_destroy(grpc_call *call);

/* Request a call on a server.
   Allows the server to create a single GRPC_SERVER_RPC_NEW event, with tag
   tag_new.
   If the call is subsequently cancelled, the cancellation will occur with tag
   tag_cancel.
   REQUIRES: Server must not have been shutdown.
   NOTE: calling this is the only way to obtain GRPC_SERVER_RPC_NEW events. */
grpc_call_error grpc_server_request_call_old(grpc_server *server,
                                             void *tag_new);

/* Request notification of a new call */
grpc_call_error grpc_server_request_call(
    grpc_server *server, grpc_call **call, grpc_call_details *details,
    grpc_metadata_array *request_metadata,
    grpc_completion_queue *cq_bound_to_call,
    void *tag_new);

/* Registers a method in the server.
   Methods to this (host, method) pair will not be reported by
   grpc_server_request_call, but instead be reported by
   grpc_server_request_registered_call when passed the appropriate
   registered_method (as returned by this function).
   Must be called before grpc_server_start.
   Returns NULL on failure. */
void *grpc_server_register_method(grpc_server *server, const char *method,
                                  const char *host,
                                  grpc_completion_queue *new_call_cq);

/* Request notification of a new pre-registered call */
grpc_call_error grpc_server_request_registered_call(
    grpc_server *server, void *registered_method, grpc_call **call,
    gpr_timespec *deadline, grpc_metadata_array *request_metadata,
    grpc_byte_buffer **optional_payload,
    grpc_completion_queue *cq_bound_to_call, void *tag_new);

/* Create a server. Additional configuration for each incoming channel can
   be specified with args. If no additional configuration is needed, args can
   be NULL. See grpc_channel_args for more. */
grpc_server *grpc_server_create(grpc_completion_queue *cq,
                                const grpc_channel_args *args);

/* Add a HTTP2 over plaintext over tcp listener.
   Returns bound port number on success, 0 on failure.
   REQUIRES: server not started */
int grpc_server_add_http2_port(grpc_server *server, const char *addr);

/* Start a server - tells all listeners to start listening */
void grpc_server_start(grpc_server *server);

/* Begin shutting down a server.
   After completion, no new calls or connections will be admitted.
   Existing calls will be allowed to complete.
   Shutdown is idempotent. */
void grpc_server_shutdown(grpc_server *server);

/* As per grpc_server_shutdown, but send a GRPC_SERVER_SHUTDOWN event when
   there are no more calls being serviced.
   Shutdown is idempotent, and all tags will be notified at once if multiple
   grpc_server_shutdown_and_notify calls are made. */
void grpc_server_shutdown_and_notify(grpc_server *server, void *tag);

/* Destroy a server.
   Forcefully cancels all existing calls.
   Implies grpc_server_shutdown() if one was not previously performed. */
void grpc_server_destroy(grpc_server *server);

#ifdef __cplusplus
}
#endif

#endif  /* GRPC_GRPC_H */
