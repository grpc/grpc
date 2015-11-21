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

#ifndef GRPC__ADAPTER__C_TYPES_H_
#define GRPC__ADAPTER__C_TYPES_H_

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>


/*=========================*/
/* Client-side credentials */
/*=========================*/

typedef struct ChannelCredentials {
  PyObject_HEAD
  grpc_channel_credentials *c_creds;
} ChannelCredentials;
void pygrpc_ChannelCredentials_dealloc(ChannelCredentials *self);
ChannelCredentials *pygrpc_ChannelCredentials_google_default(
    PyTypeObject *type, PyObject *ignored);
ChannelCredentials *pygrpc_ChannelCredentials_ssl(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
ChannelCredentials *pygrpc_ChannelCredentials_composite(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
extern PyTypeObject pygrpc_ChannelCredentials_type;

typedef struct CallCredentials {
  PyObject_HEAD
  grpc_call_credentials *c_creds;
} CallCredentials;
void pygrpc_CallCredentials_dealloc(CallCredentials *self);
CallCredentials *pygrpc_CallCredentials_composite(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
CallCredentials *pygrpc_CallCredentials_compute_engine(
    PyTypeObject *type, PyObject *ignored);
CallCredentials *pygrpc_CallCredentials_jwt(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
CallCredentials *pygrpc_CallCredentials_refresh_token(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
CallCredentials *pygrpc_CallCredentials_iam(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
extern PyTypeObject pygrpc_CallCredentials_type;

/*=========================*/
/* Server-side credentials */
/*=========================*/

typedef struct ServerCredentials {
  PyObject_HEAD
  grpc_server_credentials *c_creds;
} ServerCredentials;
void pygrpc_ServerCredentials_dealloc(ServerCredentials *self);
ServerCredentials *pygrpc_ServerCredentials_ssl(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
extern PyTypeObject pygrpc_ServerCredentials_type;


/*==================*/
/* Completion queue */
/*==================*/

typedef struct CompletionQueue {
  PyObject_HEAD
  grpc_completion_queue *c_cq;
} CompletionQueue;
CompletionQueue *pygrpc_CompletionQueue_new(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
void pygrpc_CompletionQueue_dealloc(CompletionQueue *self);
PyObject *pygrpc_CompletionQueue_next(
    CompletionQueue *self, PyObject *args, PyObject *kwargs);
PyObject *pygrpc_CompletionQueue_shutdown(
    CompletionQueue *self, PyObject *ignored);
extern PyTypeObject pygrpc_CompletionQueue_type;


/*======*/
/* Call */
/*======*/

typedef struct Call {
  PyObject_HEAD
  grpc_call *c_call;
  CompletionQueue *cq;
} Call;
Call *pygrpc_Call_new_empty(CompletionQueue *cq);
void pygrpc_Call_dealloc(Call *self);
PyObject *pygrpc_Call_start_batch(Call *self, PyObject *args, PyObject *kwargs);
PyObject *pygrpc_Call_cancel(Call *self, PyObject *args, PyObject *kwargs);
PyObject *pygrpc_Call_peer(Call *self);
PyObject *pygrpc_Call_set_credentials(Call *self, PyObject *args,
                                      PyObject *kwargs);
extern PyTypeObject pygrpc_Call_type;


/*=========*/
/* Channel */
/*=========*/

typedef struct Channel {
  PyObject_HEAD
  grpc_channel *c_chan;
} Channel;
Channel *pygrpc_Channel_new(
    PyTypeObject *type, PyObject *args, PyObject *kwargs);
void pygrpc_Channel_dealloc(Channel *self);
Call *pygrpc_Channel_create_call(
    Channel *self, PyObject *args, PyObject *kwargs);
PyObject *pygrpc_Channel_check_connectivity_state(Channel *self, PyObject *args,
                                                  PyObject *kwargs);
PyObject *pygrpc_Channel_watch_connectivity_state(Channel *self, PyObject *args,
                                                  PyObject *kwargs);
PyObject *pygrpc_Channel_target(Channel *self);
extern PyTypeObject pygrpc_Channel_type;


/*========*/
/* Server */
/*========*/

typedef struct Server {
  PyObject_HEAD
  grpc_server *c_serv;
  CompletionQueue *cq;
  int shutdown_called;
} Server;
Server *pygrpc_Server_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);
void pygrpc_Server_dealloc(Server *self);
PyObject *pygrpc_Server_request_call(
    Server *self, PyObject *args, PyObject *kwargs);
PyObject *pygrpc_Server_add_http2_port(
    Server *self, PyObject *args, PyObject *kwargs);
PyObject *pygrpc_Server_start(Server *self, PyObject *ignored);
PyObject *pygrpc_Server_shutdown(
    Server *self, PyObject *args, PyObject *kwargs);
PyObject *pygrpc_Server_cancel_all_calls(Server *self, PyObject *unused);
extern PyTypeObject pygrpc_Server_type;

/*=========*/
/* Utility */
/*=========*/

/* Every tag that passes from Python GRPC to GRPC core is of this type. */
typedef struct pygrpc_tag {
  PyObject *user_tag;
  Call *call;
  grpc_call_details request_call_details;
  grpc_metadata_array request_metadata;
  grpc_op *ops;
  size_t nops;
  int is_new_call;
} pygrpc_tag;

/* Construct a tag associated with a batch call. Does not take ownership of the
   resources in the elements of ops. */
pygrpc_tag *pygrpc_produce_batch_tag(PyObject *user_tag, Call *call,
                                     grpc_op *ops, size_t nops);


/* Construct a tag associated with a server request. The calling code should
   use the appropriate fields of the produced tag in the invocation of
   grpc_server_request_call. */
pygrpc_tag *pygrpc_produce_request_tag(PyObject *user_tag, Call *empty_call);

/* Construct a tag associated with a server shutdown. */
pygrpc_tag *pygrpc_produce_server_shutdown_tag(PyObject *user_tag);

/* Construct a tag associated with a channel state change. */
pygrpc_tag *pygrpc_produce_channel_state_change_tag(PyObject *user_tag);

/* Frees all resources owned by the tag and the tag itself. */
void pygrpc_discard_tag(pygrpc_tag *tag);

/* Consumes an event and its associated tag, providing a Python tuple of the
   form `(type, tag, call, call_details, results)` (where type is an integer
   corresponding to a grpc_completion_type, tag is an arbitrary PyObject, call
   is the call object associated with the event [if any], call_details is a
   tuple of form `(method, host, deadline)` [if such details are available],
   and resultd is a list of tuples of form `(type, metadata, message, status,
   cancelled)` [where type corresponds to a grpc_op_type, metadata is a
   sequence of 2-sequences of strings, message is a byte string, and status is
   a 2-tuple of an integer corresponding to grpc_status_code and a string of
   status details]).

   Frees all resources associated with the event tag. */
PyObject *pygrpc_consume_event(grpc_event event);

/* Transliterate the Python tuple of form `(type, metadata, message,
   status)` (where type is an integer corresponding to a grpc_op_type, metadata
   is a sequence of 2-sequences of strings, message is a byte string, and
   status is 2-tuple of an integer corresponding to grpc_status_code and a
   string of status details) to a grpc_op suitable for use in a
   grpc_call_start_batch invocation. The grpc_op is a 'directory' of resources
   that must be freed after GRPC core is done with them.

   Calls gpr_malloc (or the appropriate type-specific grpc_*_create function)
   to populate the appropriate union-discriminated members of the op.

   Returns true on success, false on failure. */
int pygrpc_produce_op(PyObject *op, grpc_op *result);

/* Discards all resources associated with the passed in op that was produced by
   pygrpc_produce_op. */
void pygrpc_discard_op(grpc_op op);

/* Transliterate the grpc_ops (which have been sent through a
   grpc_call_start_batch invocation and whose corresponding event has appeared
   on a completion queue) to a Python tuple of form `(type, metadata, message,
   status, cancelled)` (where type is an integer corresponding to a
   grpc_op_type, metadata is a sequence of 2-sequences of strings, message is a
   byte string, and status is 2-tuple of an integer corresponding to
   grpc_status_code and a string of status details).

   Calls gpr_free (or the appropriate type-specific grpc_*_destroy function) on
   the appropriate union-discriminated populated members of the ops. */
PyObject *pygrpc_consume_ops(grpc_op *op, size_t nops);

/* Transliterate from a gpr_timespec to a double (in units of seconds, either
   from the epoch if interpreted absolutely or as a delta otherwise). */
double pygrpc_cast_gpr_timespec_to_double(gpr_timespec timespec);

/* Transliterate from a double (in units of seconds from the epoch if
   interpreted absolutely or as a delta otherwise) to a gpr_timespec. */
gpr_timespec pygrpc_cast_double_to_gpr_timespec(double seconds);

/* Returns true on success, false on failure. */
int pygrpc_cast_pyseq_to_send_metadata(
    PyObject *pyseq, grpc_metadata **metadata, size_t *count);
/* Returns a metadata array as a Python object on success, else NULL. */
PyObject *pygrpc_cast_metadata_array_to_pyseq(grpc_metadata_array metadata);

/* Transliterate from a list of python channel arguments (2-tuples of string
   and string|integer|None) to a grpc_channel_args object. The strings placed
   in the grpc_channel_args object's grpc_arg elements are views of the Python
   object. The Python object must live long enough for the grpc_channel_args
   to be used. Arguments set to None are silently ignored. Returns true on
   success, false on failure. */
int pygrpc_produce_channel_args(PyObject *py_args, grpc_channel_args *c_args);
void pygrpc_discard_channel_args(grpc_channel_args args);

/* Read the bytes from grpc_byte_buffer to a gpr_malloc'd array of bytes;
   output to result and result_size. */
void pygrpc_byte_buffer_to_bytes(
    grpc_byte_buffer *buffer, char **result, size_t *result_size);


/*========*/
/* Module */
/*========*/

/* Returns 0 on success, -1 on failure. */
int pygrpc_module_add_types(PyObject *module);

#endif  /* GRPC__ADAPTER__C_TYPES_H_ */
