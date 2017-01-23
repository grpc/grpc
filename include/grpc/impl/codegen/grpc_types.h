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

#ifndef GRPC_IMPL_CODEGEN_GRPC_TYPES_H
#define GRPC_IMPL_CODEGEN_GRPC_TYPES_H

#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/exec_ctx_fwd.h>
#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/codegen/slice.h>
#include <grpc/impl/codegen/status.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GRPC_BB_RAW
  /* Future types may include GRPC_BB_PROTOBUF, etc. */
} grpc_byte_buffer_type;

typedef struct grpc_byte_buffer {
  void *reserved;
  grpc_byte_buffer_type type;
  union {
    struct {
      void *reserved[8];
    } reserved;
    struct {
      grpc_compression_algorithm compression;
      grpc_slice_buffer slice_buffer;
    } raw;
  } data;
} grpc_byte_buffer;

/** Completion Queues enable notification of the completion of asynchronous
    actions. */
typedef struct grpc_completion_queue grpc_completion_queue;

/** An alarm associated with a completion queue. */
typedef struct grpc_alarm grpc_alarm;

/** The Channel interface allows creation of Call objects. */
typedef struct grpc_channel grpc_channel;

/** A server listens to some port and responds to request calls */
typedef struct grpc_server grpc_server;

/** A Call represents an RPC. When created, it is in a configuration state
    allowing properties to be set until it is invoked. After invoke, the Call
    can have messages written to it and read from it. */
typedef struct grpc_call grpc_call;

/** The Socket Mutator interface allows changes on socket options */
typedef struct grpc_socket_mutator grpc_socket_mutator;

/** Type specifier for grpc_arg */
typedef enum {
  GRPC_ARG_STRING,
  GRPC_ARG_INTEGER,
  GRPC_ARG_POINTER
} grpc_arg_type;

typedef struct grpc_arg_pointer_vtable {
  void *(*copy)(void *p);
  void (*destroy)(grpc_exec_ctx *exec_ctx, void *p);
  int (*cmp)(void *p, void *q);
} grpc_arg_pointer_vtable;

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
      const grpc_arg_pointer_vtable *vtable;
    } pointer;
  } value;
} grpc_arg;

/** An array of arguments that can be passed around.

    Used to set optional channel-level configuration.
    These configuration options are modelled as key-value pairs as defined
    by grpc_arg; keys are strings to allow easy backwards-compatible extension
    by arbitrary parties.
    All evaluation is performed at channel creation time (i.e. the values in
    this structure need only live through the creation invocation).

    See the description of the \ref grpc_arg_keys "available args" for more
    details. */
typedef struct {
  size_t num_args;
  grpc_arg *args;
} grpc_channel_args;

/** \defgroup grpc_arg_keys
 * Channel argument keys.
 * \{
 */
/** If non-zero, enable census for tracing and stats collection. */
#define GRPC_ARG_ENABLE_CENSUS "grpc.census"
/** If non-zero, enable load reporting. */
#define GRPC_ARG_ENABLE_LOAD_REPORTING "grpc.loadreporting"
/** Maximum number of concurrent incoming streams to allow on a http2
    connection. Int valued. */
#define GRPC_ARG_MAX_CONCURRENT_STREAMS "grpc.max_concurrent_streams"
/** Maximum message length that the channel can receive. Int valued, bytes.
    -1 means unlimited. */
#define GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH "grpc.max_receive_message_length"
/** \deprecated For backward compatibility. */
#define GRPC_ARG_MAX_MESSAGE_LENGTH GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH
/** Maximum message length that the channel can send. Int valued, bytes.
    -1 means unlimited. */
#define GRPC_ARG_MAX_SEND_MESSAGE_LENGTH "grpc.max_send_message_length"
/** Initial sequence number for http2 transports. Int valued. */
#define GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER \
  "grpc.http2.initial_sequence_number"
/** Amount to read ahead on individual streams. Defaults to 64kb, larger
    values can help throughput on high-latency connections.
    NOTE: at some point we'd like to auto-tune this, and this parameter
    will become a no-op. Int valued, bytes. */
#define GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES "grpc.http2.lookahead_bytes"
/** How much memory to use for hpack decoding. Int valued, bytes. */
#define GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER \
  "grpc.http2.hpack_table_size.decoder"
/** How much memory to use for hpack encoding. Int valued, bytes. */
#define GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER \
  "grpc.http2.hpack_table_size.encoder"
/** How big a frame are we willing to receive via HTTP2.
    Min 16384, max 16777215.
    Larger values give lower CPU usage for large messages, but more head of line
    blocking for small messages. */
#define GRPC_ARG_HTTP2_MAX_FRAME_SIZE "grpc.http2.max_frame_size"
/** How much data are we willing to queue up per stream if
    GRPC_WRITE_BUFFER_HINT is set? This is an upper bound */
#define GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE "grpc.http2.write_buffer_size"
/** Default authority to pass if none specified on call construction. A string.
 * */
#define GRPC_ARG_DEFAULT_AUTHORITY "grpc.default_authority"
/** Primary user agent: goes at the start of the user-agent metadata
    sent on each request. A string. */
#define GRPC_ARG_PRIMARY_USER_AGENT_STRING "grpc.primary_user_agent"
/** Secondary user agent: goes at the end of the user-agent metadata
    sent on each request. A string. */
#define GRPC_ARG_SECONDARY_USER_AGENT_STRING "grpc.secondary_user_agent"
/** The maximum time between subsequent connection attempts, in ms */
#define GRPC_ARG_MAX_RECONNECT_BACKOFF_MS "grpc.max_reconnect_backoff_ms"
/** The time between the first and second connection attempts, in ms */
#define GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS \
  "grpc.initial_reconnect_backoff_ms"
/* The caller of the secure_channel_create functions may override the target
   name used for SSL host name checking using this channel argument which is of
   type \a GRPC_ARG_STRING. This *should* be used for testing only.
   If this argument is not specified, the name used for SSL host name checking
   will be the target parameter (assuming that the secure channel is an SSL
   channel). If this parameter is specified and the underlying is not an SSL
   channel, it will just be ignored. */
#define GRPC_SSL_TARGET_NAME_OVERRIDE_ARG "grpc.ssl_target_name_override"
/* Maximum metadata size, in bytes. */
#define GRPC_ARG_MAX_METADATA_SIZE "grpc.max_metadata_size"
/** If non-zero, allow the use of SO_REUSEPORT if it's available (default 1) */
#define GRPC_ARG_ALLOW_REUSEPORT "grpc.so_reuseport"
/** If non-zero, a pointer to a buffer pool (use grpc_resource_quota_arg_vtable
   to fetch an appropriate pointer arg vtable) */
#define GRPC_ARG_RESOURCE_QUOTA "grpc.resource_quota"
/** Service config data in JSON form. Not intended for use outside of tests. */
#define GRPC_ARG_SERVICE_CONFIG "grpc.service_config"
/** LB policy name. */
#define GRPC_ARG_LB_POLICY_NAME "grpc.lb_policy_name"
/** The grpc_socket_mutator instance that set the socket options. A pointer. */
#define GRPC_ARG_SOCKET_MUTATOR "grpc.socket_mutator"
/** \} */

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
  /** invalid message was passed to this call */
  GRPC_CALL_ERROR_INVALID_MESSAGE,
  /** completion queue for notification has not been registered with the
      server */
  GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE,
  /** this batch of operations leads to more operations than allowed */
  GRPC_CALL_ERROR_BATCH_TOO_BIG,
  /** payload type requested is not the type registered */
  GRPC_CALL_ERROR_PAYLOAD_TYPE_MISMATCH
} grpc_call_error;

/* Default send/receive message size limits in bytes. -1 for unlimited. */
/* TODO(roth) Make this match the default receive limit after next release */
#define GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH -1
#define GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH (4 * 1024 * 1024)

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

/* Initial metadata flags */
/** Signal that the call is idempotent */
#define GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST (0x00000010u)
/** Signal that the call should not return UNAVAILABLE before it has started */
#define GRPC_INITIAL_METADATA_WAIT_FOR_READY (0x00000020u)
/** Signal that the call is cacheable. GRPC is free to use GET verb */
#define GRPC_INITIAL_METADATA_CACHEABLE_REQUEST (0x00000040u)
/** Signal that GRPC_INITIAL_METADATA_WAIT_FOR_READY was explicitly set
    by the calling application. */
#define GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET (0x00000080u)

/** Mask of all valid flags */
#define GRPC_INITIAL_METADATA_USED_MASK       \
  (GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST | \
   GRPC_INITIAL_METADATA_WAIT_FOR_READY |     \
   GRPC_INITIAL_METADATA_CACHEABLE_REQUEST |  \
   GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET)

/** A single metadata element */
typedef struct grpc_metadata {
  const char *key;
  const char *value;
  size_t value_length;
  uint32_t flags;

  /** The following fields are reserved for grpc internal use.
      There is no need to initialize them, and they will be set to garbage
      during calls to grpc. */
  struct {
    void *obfuscated[4];
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

typedef struct {
  char *method;
  size_t method_capacity;
  char *host;
  size_t host_capacity;
  gpr_timespec deadline;
  uint32_t flags;
  void *reserved;
} grpc_call_details;

typedef enum {
  /** Send initial metadata: one and only one instance MUST be sent for each
      call, unless the call was cancelled - in which case this can be skipped.
      This op completes after all bytes of metadata have been accepted by
      outgoing flow control. */
  GRPC_OP_SEND_INITIAL_METADATA = 0,
  /** Send a message: 0 or more of these operations can occur for each call.
      This op completes after all bytes for the message have been accepted by
      outgoing flow control. */
  GRPC_OP_SEND_MESSAGE,
  /** Send a close from the client: one and only one instance MUST be sent from
      the client, unless the call was cancelled - in which case this can be
      skipped.
      This op completes after all bytes for the call (including the close)
      have passed outgoing flow control. */
  GRPC_OP_SEND_CLOSE_FROM_CLIENT,
  /** Send status from the server: one and only one instance MUST be sent from
      the server unless the call was cancelled - in which case this can be
      skipped.
      This op completes after all bytes for the call (including the status)
      have passed outgoing flow control. */
  GRPC_OP_SEND_STATUS_FROM_SERVER,
  /** Receive initial metadata: one and only one MUST be made on the client,
      must not be made on the server.
      This op completes after all initial metadata has been read from the
      peer. */
  GRPC_OP_RECV_INITIAL_METADATA,
  /** Receive a message: 0 or more of these operations can occur for each call.
      This op completes after all bytes of the received message have been
      read, or after a half-close has been received on this call. */
  GRPC_OP_RECV_MESSAGE,
  /** Receive status on the client: one and only one must be made on the client.
      This operation always succeeds, meaning ops paired with this operation
      will also appear to succeed, even though they may not have. In that case
      the status will indicate some failure.
      This op completes after all activity on the call has completed. */
  GRPC_OP_RECV_STATUS_ON_CLIENT,
  /** Receive close on the server: one and only one must be made on the
      server.
      This op completes after the close has been received by the server.
      This operation always succeeds, meaning ops paired with this operation
      will also appear to succeed, even though they may not have. */
  GRPC_OP_RECV_CLOSE_ON_SERVER
} grpc_op_type;

struct grpc_byte_buffer;

/** Operation data: one field for each op type (except SEND_CLOSE_FROM_CLIENT
   which has no arguments) */
typedef struct grpc_op {
  /** Operation type, as defined by grpc_op_type */
  grpc_op_type op;
  /** Write flags bitset for grpc_begin_messages */
  uint32_t flags;
  /** Reserved for future usage */
  void *reserved;
  union {
    /** Reserved for future usage */
    struct {
      void *reserved[8];
    } reserved;
    struct {
      size_t count;
      grpc_metadata *metadata;
      /** If \a is_set, \a compression_level will be used for the call.
       * Otherwise, \a compression_level won't be considered */
      struct {
        uint8_t is_set;
        grpc_compression_level level;
      } maybe_compression_level;
    } send_initial_metadata;
    struct grpc_byte_buffer *send_message;
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
    /** ownership of the byte buffer is moved to the caller; the caller must
        call grpc_byte_buffer_destroy on this value, or reuse it in a future op.
       */
    struct grpc_byte_buffer **recv_message;
    struct {
      /** ownership of the array is with the caller, but ownership of the
          elements stays with the call object (ie key, value members are owned
          by the call object, trailing_metadata->array is owned by the caller).
          After the operation completes, call grpc_metadata_array_destroy on
         this
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

/** Information requested from the channel. */
typedef struct {
  /* If non-NULL, will be set to point to a string indicating the LB
   * policy name.  Caller takes ownership. */
  char **lb_policy_name;
  /* If non-NULL, will be set to point to a string containing the
   * service config used by the channel in JSON form. */
  char **service_config_json;
} grpc_channel_info;

typedef struct grpc_resource_quota grpc_resource_quota;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_GRPC_TYPES_H */
