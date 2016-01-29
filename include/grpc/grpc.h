/*
 *
 * Copyright 2015-2016, Google Inc.
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
#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/propagation_bits.h>
#include <grpc/impl/codegen/grpc_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! \mainpage GRPC Core
 *
 * The GRPC Core library is a low-level library designed to be wrapped by higher
 * level libraries. The top-level API is provided in grpc.h. Security related
 * functionality lives in grpc_security.h.
 */

GRPC_API void grpc_metadata_array_init(grpc_metadata_array *array);
GRPC_API void grpc_metadata_array_destroy(grpc_metadata_array *array);

GRPC_API void grpc_call_details_init(grpc_call_details *details);
GRPC_API void grpc_call_details_destroy(grpc_call_details *details);

/** Registers a plugin to be initialized and destroyed with the library.

    The \a init and \a destroy functions will be invoked as part of
    \a grpc_init() and \a grpc_shutdown(), respectively.
    Note that these functions can be invoked an arbitrary number of times
    (and hence so will \a init and \a destroy).
    It is safe to pass NULL to either argument. Plugins are destroyed in
    the reverse order they were initialized. */
GRPC_API void grpc_register_plugin(void (*init)(void), void (*destroy)(void));

/** Initialize the grpc library.

    It is not safe to call any other grpc functions before calling this.
    (To avoid overhead, little checking is done, and some things may work. We
    do not warrant that they will continue to do so in future revisions of this
    library). */
GRPC_API void grpc_init(void);

/** Shut down the grpc library.

    No memory is used by grpc after this call returns, nor are any instructions
    executing within the grpc library.
    Prior to calling, all application owned grpc objects must have been
    destroyed. */
GRPC_API void grpc_shutdown(void);

/** Return a string representing the current version of grpc */
GRPC_API const char *grpc_version_string(void);

/** Create a completion queue */
GRPC_API grpc_completion_queue *grpc_completion_queue_create(void *reserved);

/** Blocks until an event is available, the completion queue is being shut down,
    or deadline is reached.

    Returns a grpc_event with type GRPC_QUEUE_TIMEOUT on timeout,
    otherwise a grpc_event describing the event that occurred.

    Callers must not call grpc_completion_queue_next and
    grpc_completion_queue_pluck simultaneously on the same completion queue. */
GRPC_API grpc_event grpc_completion_queue_next(grpc_completion_queue *cq,
                                               gpr_timespec deadline,
                                               void *reserved);

/** Blocks until an event with tag 'tag' is available, the completion queue is
    being shutdown or deadline is reached.

    Returns a grpc_event with type GRPC_QUEUE_TIMEOUT on timeout,
    otherwise a grpc_event describing the event that occurred.

    Callers must not call grpc_completion_queue_next and
    grpc_completion_queue_pluck simultaneously on the same completion queue.

    Completion queues support a maximum of GRPC_MAX_COMPLETION_QUEUE_PLUCKERS
    concurrently executing plucks at any time. */
GRPC_API grpc_event
grpc_completion_queue_pluck(grpc_completion_queue *cq, void *tag,
                            gpr_timespec deadline, void *reserved);

/** Maximum number of outstanding grpc_completion_queue_pluck executions per
    completion queue */
#define GRPC_MAX_COMPLETION_QUEUE_PLUCKERS 6

/** Begin destruction of a completion queue. Once all possible events are
    drained then grpc_completion_queue_next will start to produce
    GRPC_QUEUE_SHUTDOWN events only. At that point it's safe to call
    grpc_completion_queue_destroy.

    After calling this function applications should ensure that no
    NEW work is added to be published on this completion queue. */
GRPC_API void grpc_completion_queue_shutdown(grpc_completion_queue *cq);

/** Destroy a completion queue. The caller must ensure that the queue is
    drained and no threads are executing grpc_completion_queue_next */
GRPC_API void grpc_completion_queue_destroy(grpc_completion_queue *cq);

/** Create a completion queue alarm instance associated to \a cq.
 *
 * Once the alarm expires (at \a deadline) or it's cancelled (see \a
 * grpc_alarm_cancel), an event with tag \a tag will be added to \a cq. If the
 * alarm expired, the event's success bit will be true, false otherwise (ie,
 * upon cancellation). */
GRPC_API grpc_alarm *grpc_alarm_create(grpc_completion_queue *cq,
                                       gpr_timespec deadline, void *tag);

/** Cancel a completion queue alarm. Calling this function over an alarm that
 * has already fired has no effect. */
GRPC_API void grpc_alarm_cancel(grpc_alarm *alarm);

/** Destroy the given completion queue alarm, cancelling it in the process. */
GRPC_API void grpc_alarm_destroy(grpc_alarm *alarm);

/** Check the connectivity state of a channel. */
GRPC_API grpc_connectivity_state
grpc_channel_check_connectivity_state(grpc_channel *channel,
                                      int try_to_connect);

/** Watch for a change in connectivity state.
    Once the channel connectivity state is different from last_observed_state,
    tag will be enqueued on cq with success=1.
    If deadline expires BEFORE the state is changed, tag will be enqueued on cq
    with success=0. */
GRPC_API void grpc_channel_watch_connectivity_state(
    grpc_channel *channel, grpc_connectivity_state last_observed_state,
    gpr_timespec deadline, grpc_completion_queue *cq, void *tag);

/** Create a call given a grpc_channel, in order to call 'method'. All
    completions are sent to 'completion_queue'. 'method' and 'host' need only
    live through the invocation of this function.
    If parent_call is non-NULL, it must be a server-side call. It will be used
    to propagate properties from the server call to this new client call.
    */
GRPC_API grpc_call *grpc_channel_create_call(
    grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask,
    grpc_completion_queue *completion_queue, const char *method,
    const char *host, gpr_timespec deadline, void *reserved);

/** Ping the channels peer (load balanced channels will select one sub-channel
    to ping); if the channel is not connected, posts a failed. */
GRPC_API void grpc_channel_ping(grpc_channel *channel,
                                grpc_completion_queue *cq, void *tag,
                                void *reserved);

/** Pre-register a method/host pair on a channel. */
GRPC_API void *grpc_channel_register_call(grpc_channel *channel,
                                          const char *method, const char *host,
                                          void *reserved);

/** Create a call given a handle returned from grpc_channel_register_call */
GRPC_API grpc_call *grpc_channel_create_registered_call(
    grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask,
    grpc_completion_queue *completion_queue, void *registered_call_handle,
    gpr_timespec deadline, void *reserved);

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
GRPC_API grpc_call_error grpc_call_start_batch(grpc_call *call,
                                               const grpc_op *ops, size_t nops,
                                               void *tag, void *reserved);

/** Returns a newly allocated string representing the endpoint to which this
    call is communicating with. The string is in the uri format accepted by
    grpc_channel_create.
    The returned string should be disposed of with gpr_free().

    WARNING: this value is never authenticated or subject to any security
    related code. It must not be used for any authentication related
    functionality. Instead, use grpc_auth_context. */
GRPC_API char *grpc_call_get_peer(grpc_call *call);

struct census_context;

/* Set census context for a call; Must be called before first call to
   grpc_call_start_batch(). */
GRPC_API void grpc_census_call_set_context(grpc_call *call,
                                           struct census_context *context);

/* Retrieve the calls current census context. */
GRPC_API struct census_context *grpc_census_call_get_context(grpc_call *call);

/** Return a newly allocated string representing the target a channel was
    created for. */
GRPC_API char *grpc_channel_get_target(grpc_channel *channel);

/** Create a client channel to 'target'. Additional channel level configuration
    MAY be provided by grpc_channel_args, though the expectation is that most
    clients will want to simply pass NULL. See grpc_channel_args definition for
    more on this. The data in 'args' need only live through the invocation of
    this function. */
GRPC_API grpc_channel *grpc_insecure_channel_create(
    const char *target, const grpc_channel_args *args, void *reserved);

/** Create a lame client: this client fails every operation attempted on it. */
GRPC_API grpc_channel *grpc_lame_client_channel_create(
    const char *target, grpc_status_code error_code, const char *error_message);

/** Close and destroy a grpc channel */
GRPC_API void grpc_channel_destroy(grpc_channel *channel);

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
GRPC_API grpc_call_error grpc_call_cancel(grpc_call *call, void *reserved);

/** Called by clients to cancel an RPC on the server.
    Can be called multiple times, from any thread.
    If a status has not been received for the call, set it to the status code
    and description passed in.
    Importantly, this function does not send status nor description to the
    remote endpoint. */
GRPC_API grpc_call_error
grpc_call_cancel_with_status(grpc_call *call, grpc_status_code status,
                             const char *description, void *reserved);

/** Destroy a call.
    THREAD SAFETY: grpc_call_destroy is thread-compatible */
GRPC_API void grpc_call_destroy(grpc_call *call);

/** Request notification of a new call.
    Once a call is received, a notification tagged with \a tag_new is added to
    \a cq_for_notification. \a call, \a details and \a request_metadata are
    updated with the appropriate call information. \a cq_bound_to_call is bound
    to \a call, and batch operation notifications for that call will be posted
    to \a cq_bound_to_call.
    Note that \a cq_for_notification must have been registered to the server via
    \a grpc_server_register_completion_queue. */
GRPC_API grpc_call_error
grpc_server_request_call(grpc_server *server, grpc_call **call,
                         grpc_call_details *details,
                         grpc_metadata_array *request_metadata,
                         grpc_completion_queue *cq_bound_to_call,
                         grpc_completion_queue *cq_for_notification,
                         void *tag_new);

/** Registers a method in the server.
    Methods to this (host, method) pair will not be reported by
    grpc_server_request_call, but instead be reported by
    grpc_server_request_registered_call when passed the appropriate
    registered_method (as returned by this function).
    Must be called before grpc_server_start.
    Returns NULL on failure. */
GRPC_API void *grpc_server_register_method(grpc_server *server,
                                           const char *method,
                                           const char *host);

/** Request notification of a new pre-registered call. 'cq_for_notification'
    must have been registered to the server via
    grpc_server_register_completion_queue. */
GRPC_API grpc_call_error grpc_server_request_registered_call(
    grpc_server *server, void *registered_method, grpc_call **call,
    gpr_timespec *deadline, grpc_metadata_array *request_metadata,
    grpc_byte_buffer **optional_payload,
    grpc_completion_queue *cq_bound_to_call,
    grpc_completion_queue *cq_for_notification, void *tag_new);

/** Create a server. Additional configuration for each incoming channel can
    be specified with args. If no additional configuration is needed, args can
    be NULL. See grpc_channel_args for more. The data in 'args' need only live
    through the invocation of this function. */
GRPC_API grpc_server *grpc_server_create(const grpc_channel_args *args,
                                         void *reserved);

/** Register a completion queue with the server. Must be done for any
    notification completion queue that is passed to grpc_server_request_*_call
    and to grpc_server_shutdown_and_notify. Must be performed prior to
    grpc_server_start. */
GRPC_API void grpc_server_register_completion_queue(grpc_server *server,
                                                    grpc_completion_queue *cq,
                                                    void *reserved);

/** Add a HTTP2 over plaintext over tcp listener.
    Returns bound port number on success, 0 on failure.
    REQUIRES: server not started */
GRPC_API int grpc_server_add_insecure_http2_port(grpc_server *server,
                                                 const char *addr);

/** Start a server - tells all listeners to start listening */
GRPC_API void grpc_server_start(grpc_server *server);

/** Begin shutting down a server.
    After completion, no new calls or connections will be admitted.
    Existing calls will be allowed to complete.
    Send a GRPC_OP_COMPLETE event when there are no more calls being serviced.
    Shutdown is idempotent, and all tags will be notified at once if multiple
    grpc_server_shutdown_and_notify calls are made. 'cq' must have been
    registered to this server via grpc_server_register_completion_queue. */
GRPC_API void grpc_server_shutdown_and_notify(grpc_server *server,
                                              grpc_completion_queue *cq,
                                              void *tag);

/** Cancel all in-progress calls.
    Only usable after shutdown. */
GRPC_API void grpc_server_cancel_all_calls(grpc_server *server);

/** Destroy a server.
    Shutdown must have completed beforehand (i.e. all tags generated by
    grpc_server_shutdown_and_notify must have been received, and at least
    one call to grpc_server_shutdown_and_notify must have been made). */
GRPC_API void grpc_server_destroy(grpc_server *server);

/** Enable or disable a tracer.

    Tracers (usually controlled by the environment variable GRPC_TRACE)
    allow printf-style debugging on GRPC internals, and are useful for
    tracking down problems in the field.

    Use of this function is not strictly thread-safe, but the
    thread-safety issues raised by it should not be of concern. */
GRPC_API int grpc_tracer_set_enabled(const char *name, int enabled);

/** Check whether a metadata key is legal (will be accepted by core) */
GRPC_API int grpc_header_key_is_legal(const char *key, size_t length);

/** Check whether a non-binary metadata value is legal (will be accepted by
    core) */
GRPC_API int grpc_header_nonbin_value_is_legal(const char *value,
                                               size_t length);

/** Check whether a metadata key corresponds to a binary value */
GRPC_API int grpc_is_binary_header(const char *key, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_GRPC_H */
