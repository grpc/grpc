# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cimport libc.time


cdef extern from "grpc/support/alloc.h":
  void *gpr_malloc(size_t size)
  void gpr_free(void *ptr)
  void *gpr_realloc(void *p, size_t size)

cdef extern from "grpc/support/slice.h":
  ctypedef struct gpr_slice:
    # don't worry about writing out the members of gpr_slice; we never access
    # them directly.
    pass

  gpr_slice gpr_slice_ref(gpr_slice s)
  void gpr_slice_unref(gpr_slice s)
  gpr_slice gpr_slice_new(void *p, size_t len, void (*destroy)(void *))
  gpr_slice gpr_slice_new_with_len(
      void *p, size_t len, void (*destroy)(void *, size_t))
  gpr_slice gpr_slice_malloc(size_t length)
  gpr_slice gpr_slice_from_copied_string(const char *source)
  gpr_slice gpr_slice_from_copied_buffer(const char *source, size_t len)

  # Declare functions for function-like macros (because Cython)...
  void *gpr_slice_start_ptr "GPR_SLICE_START_PTR" (gpr_slice s)
  size_t gpr_slice_length "GPR_SLICE_LENGTH" (gpr_slice s)


cdef extern from "grpc/support/port_platform.h":
  # As long as the header file gets this type right, we don't need to get this
  # type exactly; just close enough that the operations will be supported in the
  # underlying C layers.
  ctypedef unsigned int gpr_uint32


cdef extern from "grpc/support/time.h":

  ctypedef enum gpr_clock_type:
    GPR_CLOCK_MONOTONIC
    GPR_CLOCK_REALTIME
    GPR_CLOCK_PRECISE
    GPR_TIMESPAN

  ctypedef struct gpr_timespec:
    libc.time.time_t seconds "tv_sec"
    int nanoseconds "tv_nsec"
    gpr_clock_type clock_type

  gpr_timespec gpr_time_0(gpr_clock_type type)
  gpr_timespec gpr_inf_future(gpr_clock_type type)
  gpr_timespec gpr_inf_past(gpr_clock_type type)

  gpr_timespec gpr_now(gpr_clock_type clock)

  gpr_timespec gpr_convert_clock_type(gpr_timespec t,
                                      gpr_clock_type target_clock)


cdef extern from "grpc/status.h":
  ctypedef enum grpc_status_code:
    GRPC_STATUS_OK
    GRPC_STATUS_CANCELLED
    GRPC_STATUS_UNKNOWN
    GRPC_STATUS_INVALID_ARGUMENT
    GRPC_STATUS_DEADLINE_EXCEEDED
    GRPC_STATUS_NOT_FOUND
    GRPC_STATUS_ALREADY_EXISTS
    GRPC_STATUS_PERMISSION_DENIED
    GRPC_STATUS_UNAUTHENTICATED
    GRPC_STATUS_RESOURCE_EXHAUSTED
    GRPC_STATUS_FAILED_PRECONDITION
    GRPC_STATUS_ABORTED
    GRPC_STATUS_OUT_OF_RANGE
    GRPC_STATUS_UNIMPLEMENTED
    GRPC_STATUS_INTERNAL
    GRPC_STATUS_UNAVAILABLE
    GRPC_STATUS_DATA_LOSS
    GRPC_STATUS__DO_NOT_USE


cdef extern from "grpc/byte_buffer_reader.h":
  struct grpc_byte_buffer_reader:
    # We don't care about the internals
    pass


cdef extern from "grpc/byte_buffer.h":
  ctypedef struct grpc_byte_buffer:
    # We don't care about the internals.
    pass

  grpc_byte_buffer *grpc_raw_byte_buffer_create(gpr_slice *slices,
                                                size_t nslices)
  size_t grpc_byte_buffer_length(grpc_byte_buffer *bb)
  void grpc_byte_buffer_destroy(grpc_byte_buffer *byte_buffer)

  void grpc_byte_buffer_reader_init(grpc_byte_buffer_reader *reader,
                                    grpc_byte_buffer *buffer)
  int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader *reader,
                                   gpr_slice *slice)
  void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader *reader)


cdef extern from "grpc/grpc.h":

  ctypedef struct grpc_completion_queue:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct grpc_channel:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct grpc_server:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct grpc_call:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef enum grpc_arg_type:
    grpc_arg_string "GRPC_ARG_STRING"
    grpc_arg_integer "GRPC_ARG_INTEGER"
    grpc_arg_pointer "GRPC_ARG_POINTER"

  ctypedef struct grpc_arg_value_pointer:
    void *address "p"
    void *(*copy)(void *)
    void (*destroy)(void *)

  union grpc_arg_value:
    char *string
    int integer
    grpc_arg_value_pointer pointer

  ctypedef struct grpc_arg:
    grpc_arg_type type
    char *key
    grpc_arg_value value

  ctypedef struct grpc_channel_args:
    size_t arguments_length "num_args"
    grpc_arg *arguments "args"

  ctypedef enum grpc_call_error:
    GRPC_CALL_OK
    GRPC_CALL_ERROR
    GRPC_CALL_ERROR_NOT_ON_SERVER
    GRPC_CALL_ERROR_NOT_ON_CLIENT
    GRPC_CALL_ERROR_ALREADY_ACCEPTED
    GRPC_CALL_ERROR_ALREADY_INVOKED
    GRPC_CALL_ERROR_NOT_INVOKED
    GRPC_CALL_ERROR_ALREADY_FINISHED
    GRPC_CALL_ERROR_TOO_MANY_OPERATIONS
    GRPC_CALL_ERROR_INVALID_FLAGS
    GRPC_CALL_ERROR_INVALID_METADATA

  ctypedef struct grpc_metadata:
    const char *key
    const char *value
    size_t value_length
    # ignore the 'internal_data.obfuscated' fields.

  ctypedef enum grpc_completion_type:
    GRPC_QUEUE_SHUTDOWN
    GRPC_QUEUE_TIMEOUT
    GRPC_OP_COMPLETE

  ctypedef struct grpc_event:
    grpc_completion_type type
    int success
    void *tag

  ctypedef struct grpc_metadata_array:
    size_t count
    size_t capacity
    grpc_metadata *metadata

  void grpc_metadata_array_init(grpc_metadata_array *array)
  void grpc_metadata_array_destroy(grpc_metadata_array *array)

  ctypedef struct grpc_call_details:
    char *method
    size_t method_capacity
    char *host
    size_t host_capacity
    gpr_timespec deadline

  void grpc_call_details_init(grpc_call_details *details)
  void grpc_call_details_destroy(grpc_call_details *details)

  ctypedef enum grpc_op_type:
    GRPC_OP_SEND_INITIAL_METADATA
    GRPC_OP_SEND_MESSAGE
    GRPC_OP_SEND_CLOSE_FROM_CLIENT
    GRPC_OP_SEND_STATUS_FROM_SERVER
    GRPC_OP_RECV_INITIAL_METADATA
    GRPC_OP_RECV_MESSAGE
    GRPC_OP_RECV_STATUS_ON_CLIENT
    GRPC_OP_RECV_CLOSE_ON_SERVER

  ctypedef struct grpc_op_data_send_initial_metadata:
    size_t count
    grpc_metadata *metadata

  ctypedef struct grpc_op_data_send_status_from_server:
    size_t trailing_metadata_count
    grpc_metadata *trailing_metadata
    grpc_status_code status
    const char *status_details

  ctypedef struct grpc_op_data_recv_status_on_client:
    grpc_metadata_array *trailing_metadata
    grpc_status_code *status
    char **status_details
    size_t *status_details_capacity

  ctypedef struct grpc_op_data_recv_close_on_server:
    int *cancelled

  union grpc_op_data:
    grpc_op_data_send_initial_metadata send_initial_metadata
    grpc_byte_buffer *send_message
    grpc_op_data_send_status_from_server send_status_from_server
    grpc_metadata_array *receive_initial_metadata "recv_initial_metadata"
    grpc_byte_buffer **receive_message "recv_message"
    grpc_op_data_recv_status_on_client receive_status_on_client "recv_status_on_client"
    grpc_op_data_recv_close_on_server receive_close_on_server "recv_close_on_server"

  ctypedef struct grpc_op:
    grpc_op_type type "op"
    gpr_uint32 flags
    grpc_op_data data

  void grpc_init()
  void grpc_shutdown()

  grpc_completion_queue *grpc_completion_queue_create(void *reserved)
  grpc_event grpc_completion_queue_next(grpc_completion_queue *cq,
                                        gpr_timespec deadline,
                                        void *reserved) nogil
  void grpc_completion_queue_shutdown(grpc_completion_queue *cq)
  void grpc_completion_queue_destroy(grpc_completion_queue *cq)

  grpc_call_error grpc_call_start_batch(grpc_call *call, const grpc_op *ops,
                                        size_t nops, void *tag, void *reserved)
  grpc_call_error grpc_call_cancel(grpc_call *call, void *reserved)
  grpc_call_error grpc_call_cancel_with_status(grpc_call *call,
                                               grpc_status_code status,
                                               const char *description,
                                               void *reserved)
  void grpc_call_destroy(grpc_call *call)


  grpc_channel *grpc_insecure_channel_create(const char *target,
                                             const grpc_channel_args *args,
                                             void *reserved)
  grpc_call *grpc_channel_create_call(grpc_channel *channel,
                                      grpc_call *parent_call,
                                      gpr_uint32 propagation_mask,
                                      grpc_completion_queue *completion_queue,
                                      const char *method, const char *host,
                                      gpr_timespec deadline, void *reserved)
  void grpc_channel_destroy(grpc_channel *channel)

  grpc_server *grpc_server_create(const grpc_channel_args *args, void *reserved)
  grpc_call_error grpc_server_request_call(
      grpc_server *server, grpc_call **call, grpc_call_details *details,
      grpc_metadata_array *request_metadata, grpc_completion_queue
      *cq_bound_to_call, grpc_completion_queue *cq_for_notification, void
      *tag_new)
  void grpc_server_register_completion_queue(grpc_server *server,
                                             grpc_completion_queue *cq,
                                             void *reserved)
  int grpc_server_add_insecure_http2_port(grpc_server *server, const char *addr)
  void grpc_server_start(grpc_server *server)
  void grpc_server_shutdown_and_notify(
      grpc_server *server, grpc_completion_queue *cq, void *tag)
  void grpc_server_cancel_all_calls(grpc_server *server)
  void grpc_server_destroy(grpc_server *server)


cdef extern from "grpc/grpc_security.h":

  ctypedef struct grpc_ssl_pem_key_cert_pair:
    const char *private_key
    const char *certificate_chain "cert_chain"

  ctypedef struct grpc_channel_credentials:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct grpc_call_credentials:
    # We don't care about the internals (and in fact don't know them)
    pass

  grpc_channel_credentials *grpc_google_default_credentials_create()
  grpc_channel_credentials *grpc_ssl_credentials_create(
      const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
      void *reserved)
  grpc_channel_credentials *grpc_composite_channel_credentials_create(
      grpc_channel_credentials *creds1, grpc_call_credentials *creds2,
      void *reserved)
  void grpc_channel_credentials_release(grpc_channel_credentials *creds)

  grpc_call_credentials *grpc_composite_call_credentials_create(
      grpc_call_credentials *creds1, grpc_call_credentials *creds2,
      void *reserved)
  grpc_call_credentials *grpc_google_compute_engine_credentials_create(
      void *reserved)
  grpc_call_credentials *grpc_service_account_jwt_access_credentials_create(
      const char *json_key,
      gpr_timespec token_lifetime, void *reserved)
  grpc_call_credentials *grpc_google_refresh_token_credentials_create(
      const char *json_refresh_token, void *reserved)
  grpc_call_credentials *grpc_google_iam_credentials_create(
      const char *authorization_token, const char *authority_selector,
      void *reserved)
  void grpc_call_credentials_release(grpc_call_credentials *creds)

  grpc_channel *grpc_secure_channel_create(
      grpc_channel_credentials *creds, const char *target,
      const grpc_channel_args *args, void *reserved)

  ctypedef struct grpc_server_credentials:
    # We don't care about the internals (and in fact don't know them)
    pass

  grpc_server_credentials *grpc_ssl_server_credentials_create(
      const char *pem_root_certs,
      grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
      size_t num_key_cert_pairs, int force_client_auth, void *reserved)
  void grpc_server_credentials_release(grpc_server_credentials *creds)

  int grpc_server_add_secure_http2_port(grpc_server *server, const char *addr,
                                        grpc_server_credentials *creds)

  grpc_call_error grpc_call_set_credentials(grpc_call *call,
                                            grpc_call_credentials *creds)
