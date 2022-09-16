# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cimport libc.time

ctypedef          ssize_t   intptr_t
ctypedef          size_t    uintptr_t
ctypedef   signed char      int8_t
ctypedef   signed short     int16_t
ctypedef   signed int       int32_t
ctypedef   signed long long int64_t
ctypedef unsigned char      uint8_t
ctypedef unsigned short     uint16_t
ctypedef unsigned int       uint32_t
ctypedef unsigned long long uint64_t

# C++ Utilities

# NOTE(lidiz) Unfortunately, we can't use "cimport" here because Cython
# links it with exception handling. It introduces new dependencies.
cdef extern from "<queue>" namespace "std" nogil:
    cdef cppclass queue[T]:
        queue()
        bint empty()
        T& front()
        T& back()
        void pop()
        void push(T&)
        size_t size()


cdef extern from "<mutex>" namespace "std" nogil:
    cdef cppclass mutex:
        mutex()
        void lock()
        void unlock()

    cdef cppclass unique_lock[Mutex]:
      unique_lock(Mutex&)

cdef extern from "<condition_variable>" namespace "std" nogil:
  cdef cppclass condition_variable:
    condition_variable()
    void notify_all()
    void wait(unique_lock[mutex]&)

# gRPC Core Declarations

cdef extern from "grpc/support/alloc.h":

  void *gpr_malloc(size_t size) nogil
  void *gpr_zalloc(size_t size) nogil
  void gpr_free(void *ptr) nogil
  void *gpr_realloc(void *p, size_t size) nogil


cdef extern from "grpc/byte_buffer_reader.h":

  struct grpc_byte_buffer_reader:
    # We don't care about the internals
    pass


cdef extern from "grpc/impl/codegen/grpc_types.h":
    ctypedef struct grpc_completion_queue_functor:
        void (*functor_run)(grpc_completion_queue_functor*, int);


cdef extern from "grpc/grpc.h":

  ctypedef struct grpc_slice:
    # don't worry about writing out the members of grpc_slice; we never access
    # them directly.
    pass

  grpc_slice grpc_slice_ref(grpc_slice s) nogil
  void grpc_slice_unref(grpc_slice s) nogil
  grpc_slice grpc_empty_slice() nogil
  grpc_slice grpc_slice_new(void *p, size_t len, void (*destroy)(void *)) nogil
  grpc_slice grpc_slice_new_with_len(
      void *p, size_t len, void (*destroy)(void *, size_t)) nogil
  grpc_slice grpc_slice_malloc(size_t length) nogil
  grpc_slice grpc_slice_from_copied_string(const char *source) nogil
  grpc_slice grpc_slice_from_copied_buffer(const char *source, size_t len) nogil
  grpc_slice grpc_slice_copy(grpc_slice s) nogil

  # Declare functions for function-like macros (because Cython)...
  void *grpc_slice_start_ptr "GRPC_SLICE_START_PTR" (grpc_slice s) nogil
  size_t grpc_slice_length "GRPC_SLICE_LENGTH" (grpc_slice s) nogil

  const int GPR_MS_PER_SEC
  const int GPR_US_PER_SEC
  const int GPR_NS_PER_SEC

  ctypedef enum gpr_clock_type:
    GPR_CLOCK_MONOTONIC
    GPR_CLOCK_REALTIME
    GPR_CLOCK_PRECISE
    GPR_TIMESPAN

  ctypedef struct gpr_timespec:
    int64_t seconds "tv_sec"
    int32_t nanoseconds "tv_nsec"
    gpr_clock_type clock_type

  gpr_timespec gpr_time_0(gpr_clock_type type) nogil
  gpr_timespec gpr_inf_future(gpr_clock_type type) nogil
  gpr_timespec gpr_inf_past(gpr_clock_type type) nogil

  gpr_timespec gpr_now(gpr_clock_type clock) nogil

  gpr_timespec gpr_convert_clock_type(gpr_timespec t,
                                      gpr_clock_type target_clock) nogil

  gpr_timespec gpr_time_from_millis(int64_t ms, gpr_clock_type type) nogil
  gpr_timespec gpr_time_from_nanos(int64_t ns, gpr_clock_type type) nogil
  double gpr_timespec_to_micros(gpr_timespec t) nogil

  gpr_timespec gpr_time_add(gpr_timespec a, gpr_timespec b) nogil

  int gpr_time_cmp(gpr_timespec a, gpr_timespec b) nogil

  ctypedef struct grpc_byte_buffer:
    # We don't care about the internals.
    pass

  grpc_byte_buffer *grpc_raw_byte_buffer_create(grpc_slice *slices,
                                                size_t nslices) nogil
  size_t grpc_byte_buffer_length(grpc_byte_buffer *bb) nogil
  void grpc_byte_buffer_destroy(grpc_byte_buffer *byte_buffer) nogil

  int grpc_byte_buffer_reader_init(grpc_byte_buffer_reader *reader,
                                   grpc_byte_buffer *buffer) nogil
  int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader *reader,
                                   grpc_slice *slice) nogil
  void grpc_byte_buffer_reader_destroy(grpc_byte_buffer_reader *reader) nogil

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

  const char *GRPC_ARG_ENABLE_CENSUS
  const char *GRPC_ARG_MAX_CONCURRENT_STREAMS
  const char *GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH
  const char *GRPC_ARG_MAX_SEND_MESSAGE_LENGTH
  const char *GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER
  const char *GRPC_ARG_DEFAULT_AUTHORITY
  const char *GRPC_ARG_PRIMARY_USER_AGENT_STRING
  const char *GRPC_ARG_SECONDARY_USER_AGENT_STRING
  const char *GRPC_SSL_TARGET_NAME_OVERRIDE_ARG
  const char *GRPC_SSL_SESSION_CACHE_ARG
  const char *_GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM \
    "GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM"
  const char *GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL
  const char *GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET

  const int GRPC_WRITE_BUFFER_HINT
  const int GRPC_WRITE_NO_COMPRESS
  const int GRPC_WRITE_USED_MASK

  const int GRPC_INITIAL_METADATA_WAIT_FOR_READY
  const int GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET
  const int GRPC_INITIAL_METADATA_USED_MASK

  const int GRPC_MAX_COMPLETION_QUEUE_PLUCKERS

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
    GRPC_ARG_STRING
    GRPC_ARG_INTEGER
    GRPC_ARG_POINTER

  ctypedef struct grpc_arg_pointer_vtable:
    void *(*copy)(void *)
    void (*destroy)(void *)
    int (*cmp)(void *, void *)

  ctypedef struct grpc_arg_value_pointer:
    void *address "p"
    grpc_arg_pointer_vtable *vtable

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

  ctypedef enum grpc_stream_compression_level:
    GRPC_STREAM_COMPRESS_LEVEL_NONE
    GRPC_STREAM_COMPRESS_LEVEL_LOW
    GRPC_STREAM_COMPRESS_LEVEL_MED
    GRPC_STREAM_COMPRESS_LEVEL_HIGH

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

  ctypedef enum grpc_cq_completion_type:
    GRPC_CQ_NEXT
    GRPC_CQ_PLUCK

  ctypedef enum grpc_cq_polling_type:
    GRPC_CQ_DEFAULT_POLLING
    GRPC_CQ_NON_LISTENING
    GRPC_CQ_NON_POLLING

  ctypedef struct grpc_completion_queue_attributes:
    int version
    grpc_cq_completion_type cq_completion_type
    grpc_cq_polling_type cq_polling_type
    void* cq_shutdown_cb

  ctypedef enum grpc_connectivity_state:
    GRPC_CHANNEL_IDLE
    GRPC_CHANNEL_CONNECTING
    GRPC_CHANNEL_READY
    GRPC_CHANNEL_TRANSIENT_FAILURE
    GRPC_CHANNEL_SHUTDOWN

  ctypedef struct grpc_metadata:
    grpc_slice key
    grpc_slice value
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

  void grpc_metadata_array_init(grpc_metadata_array *array) nogil
  void grpc_metadata_array_destroy(grpc_metadata_array *array) nogil

  ctypedef struct grpc_call_details:
    grpc_slice method
    grpc_slice host
    gpr_timespec deadline

  void grpc_call_details_init(grpc_call_details *details) nogil
  void grpc_call_details_destroy(grpc_call_details *details) nogil

  ctypedef enum grpc_op_type:
    GRPC_OP_SEND_INITIAL_METADATA
    GRPC_OP_SEND_MESSAGE
    GRPC_OP_SEND_CLOSE_FROM_CLIENT
    GRPC_OP_SEND_STATUS_FROM_SERVER
    GRPC_OP_RECV_INITIAL_METADATA
    GRPC_OP_RECV_MESSAGE
    GRPC_OP_RECV_STATUS_ON_CLIENT
    GRPC_OP_RECV_CLOSE_ON_SERVER

  ctypedef struct grpc_op_send_initial_metadata_maybe_compression_level:
    uint8_t is_set
    grpc_compression_level level

  ctypedef struct grpc_op_data_send_initial_metadata:
    size_t count
    grpc_metadata *metadata
    grpc_op_send_initial_metadata_maybe_compression_level maybe_compression_level

  ctypedef struct grpc_op_data_send_status_from_server:
    size_t trailing_metadata_count
    grpc_metadata *trailing_metadata
    grpc_status_code status
    grpc_slice *status_details

  ctypedef struct grpc_op_data_recv_status_on_client:
    grpc_metadata_array *trailing_metadata
    grpc_status_code *status
    grpc_slice *status_details
    char** error_string

  ctypedef struct grpc_op_data_recv_close_on_server:
    int *cancelled

  ctypedef struct grpc_op_data_send_message:
    grpc_byte_buffer *send_message

  ctypedef struct grpc_op_data_receive_message:
    grpc_byte_buffer **receive_message "recv_message"

  ctypedef struct grpc_op_data_receive_initial_metadata:
    grpc_metadata_array *receive_initial_metadata "recv_initial_metadata"

  union grpc_op_data:
    grpc_op_data_send_initial_metadata send_initial_metadata
    grpc_op_data_send_message send_message
    grpc_op_data_send_status_from_server send_status_from_server
    grpc_op_data_receive_initial_metadata receive_initial_metadata "recv_initial_metadata"
    grpc_op_data_receive_message receive_message "recv_message"
    grpc_op_data_recv_status_on_client receive_status_on_client "recv_status_on_client"
    grpc_op_data_recv_close_on_server receive_close_on_server "recv_close_on_server"

  ctypedef struct grpc_op:
    grpc_op_type type "op"
    uint32_t flags
    void * reserved
    grpc_op_data data

  void grpc_init() nogil
  void grpc_shutdown() nogil
  void grpc_shutdown_blocking() nogil
  int grpc_is_initialized() nogil

  ctypedef struct grpc_completion_queue_factory:
    pass

  grpc_completion_queue_factory *grpc_completion_queue_factory_lookup(
      const grpc_completion_queue_attributes* attributes) nogil
  grpc_completion_queue *grpc_completion_queue_create(
    const grpc_completion_queue_factory* factory,
    const grpc_completion_queue_attributes* attr, void* reserved) nogil
  grpc_completion_queue *grpc_completion_queue_create_for_next(void *reserved) nogil

  grpc_event grpc_completion_queue_next(grpc_completion_queue *cq,
                                        gpr_timespec deadline,
                                        void *reserved) nogil
  grpc_event grpc_completion_queue_pluck(grpc_completion_queue *cq, void *tag,
                                         gpr_timespec deadline,
                                         void *reserved) nogil
  void grpc_completion_queue_shutdown(grpc_completion_queue *cq) nogil
  void grpc_completion_queue_destroy(grpc_completion_queue *cq) nogil

  grpc_completion_queue *grpc_completion_queue_create_for_callback(
    grpc_completion_queue_functor* shutdown_callback,
    void *reserved) nogil

  grpc_call_error grpc_call_start_batch(
      grpc_call *call, const grpc_op *ops, size_t nops, void *tag,
      void *reserved) nogil
  grpc_call_error grpc_call_cancel(grpc_call *call, void *reserved) nogil
  grpc_call_error grpc_call_cancel_with_status(grpc_call *call,
                                               grpc_status_code status,
                                               const char *description,
                                               void *reserved) nogil
  char *grpc_call_get_peer(grpc_call *call) nogil
  void grpc_call_unref(grpc_call *call) nogil

  grpc_call *grpc_channel_create_call(
    grpc_channel *channel, grpc_call *parent_call, uint32_t propagation_mask,
    grpc_completion_queue *completion_queue, grpc_slice method,
    const grpc_slice *host, gpr_timespec deadline, void *reserved) nogil
  grpc_connectivity_state grpc_channel_check_connectivity_state(
      grpc_channel *channel, int try_to_connect) nogil
  void grpc_channel_watch_connectivity_state(
      grpc_channel *channel, grpc_connectivity_state last_observed_state,
      gpr_timespec deadline, grpc_completion_queue *cq, void *tag) nogil
  char *grpc_channel_get_target(grpc_channel *channel) nogil
  void grpc_channel_destroy(grpc_channel *channel) nogil

  grpc_server *grpc_server_create(
      const grpc_channel_args *args, void *reserved) nogil
  grpc_call_error grpc_server_request_call(
      grpc_server *server, grpc_call **call, grpc_call_details *details,
      grpc_metadata_array *request_metadata, grpc_completion_queue
      *cq_bound_to_call, grpc_completion_queue *cq_for_notification, void
      *tag_new) nogil
  void grpc_server_register_completion_queue(grpc_server *server,
                                             grpc_completion_queue *cq,
                                             void *reserved) nogil

  ctypedef struct grpc_server_config_fetcher:
    pass

  void grpc_server_set_config_fetcher(
       grpc_server* server, grpc_server_config_fetcher* config_fetcher) nogil

  ctypedef struct grpc_server_xds_status_notifier:
    void (*on_serving_status_update)(void* user_data, const char* uri,
                                   grpc_status_code code,
                                   const char* error_message)
    void* user_data;

  grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create(
       grpc_server_xds_status_notifier notifier,
       const grpc_channel_args* args) nogil


  void grpc_server_start(grpc_server *server) nogil
  void grpc_server_shutdown_and_notify(
      grpc_server *server, grpc_completion_queue *cq, void *tag) nogil
  void grpc_server_cancel_all_calls(grpc_server *server) nogil
  void grpc_server_destroy(grpc_server *server) nogil

  char* grpc_channelz_get_top_channels(intptr_t start_channel_id)
  char* grpc_channelz_get_servers(intptr_t start_server_id)
  char* grpc_channelz_get_server(intptr_t server_id)
  char* grpc_channelz_get_server_sockets(intptr_t server_id,
                                         intptr_t start_socket_id,
                                         intptr_t max_results)
  char* grpc_channelz_get_channel(intptr_t channel_id)
  char* grpc_channelz_get_subchannel(intptr_t subchannel_id)
  char* grpc_channelz_get_socket(intptr_t socket_id)

  grpc_slice grpc_dump_xds_configs() nogil


cdef extern from "grpc/grpc_security.h":

  # Declare this as an enum, this is the only way to make it a const in
  # cython
  enum: GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX

  ctypedef enum grpc_ssl_roots_override_result:
    GRPC_SSL_ROOTS_OVERRIDE_OK
    GRPC_SSL_ROOTS_OVERRIDE_FAILED_PERMANENTLY
    GRPC_SSL_ROOTS_OVERRIDE_FAILED

  ctypedef enum grpc_ssl_client_certificate_request_type:
    GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
    GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY
    GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY
    GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY
    GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY

  ctypedef enum grpc_security_level:
    GRPC_SECURITY_MIN
    GRPC_SECURITY_NONE = GRPC_SECURITY_MIN
    GRPC_INTEGRITY_ONLY
    GRPC_PRIVACY_AND_INTEGRITY
    GRPC_SECURITY_MAX = GRPC_PRIVACY_AND_INTEGRITY

  ctypedef enum grpc_ssl_certificate_config_reload_status:
    GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED
    GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW
    GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL

  ctypedef struct grpc_ssl_server_certificate_config:
    # We don't care about the internals
    pass

  ctypedef struct grpc_ssl_server_credentials_options:
    # We don't care about the internals
    pass

  grpc_ssl_server_certificate_config * grpc_ssl_server_certificate_config_create(
    const char *pem_root_certs,
    const grpc_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs)

  void grpc_ssl_server_certificate_config_destroy(grpc_ssl_server_certificate_config *config)

  ctypedef grpc_ssl_certificate_config_reload_status (*grpc_ssl_server_certificate_config_callback)(
    void *user_data,
    grpc_ssl_server_certificate_config **config)

  grpc_ssl_server_credentials_options *grpc_ssl_server_credentials_create_options_using_config(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config *certificate_config)

  grpc_ssl_server_credentials_options* grpc_ssl_server_credentials_create_options_using_config_fetcher(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config_callback cb,
    void *user_data)

  grpc_server_credentials *grpc_ssl_server_credentials_create_with_options(
      grpc_ssl_server_credentials_options *options)

  ctypedef struct grpc_ssl_pem_key_cert_pair:
    const char *private_key
    const char *certificate_chain "cert_chain"

  ctypedef struct grpc_channel_credentials:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct grpc_call_credentials:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct grpc_ssl_session_cache:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct verify_peer_options:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef void (*grpc_ssl_roots_override_callback)(char **pem_root_certs)

  grpc_ssl_session_cache *grpc_ssl_session_cache_create_lru(size_t capacity)
  void grpc_ssl_session_cache_destroy(grpc_ssl_session_cache* cache)

  void grpc_set_ssl_roots_override_callback(
      grpc_ssl_roots_override_callback cb) nogil

  grpc_channel_credentials *grpc_google_default_credentials_create(grpc_call_credentials* call_credentials) nogil
  grpc_channel_credentials *grpc_ssl_credentials_create(
      const char *pem_root_certs, grpc_ssl_pem_key_cert_pair *pem_key_cert_pair,
      verify_peer_options *verify_options, void *reserved) nogil
  grpc_channel_credentials *grpc_composite_channel_credentials_create(
      grpc_channel_credentials *creds1, grpc_call_credentials *creds2,
      void *reserved) nogil
  void grpc_channel_credentials_release(grpc_channel_credentials *creds) nogil

  grpc_channel_credentials *grpc_xds_credentials_create(
      grpc_channel_credentials *fallback_creds) nogil

  grpc_channel_credentials *grpc_insecure_credentials_create() nogil

  grpc_server_credentials *grpc_xds_server_credentials_create(
      grpc_server_credentials *fallback_creds) nogil

  grpc_server_credentials *grpc_insecure_server_credentials_create() nogil

  grpc_call_credentials *grpc_composite_call_credentials_create(
      grpc_call_credentials *creds1, grpc_call_credentials *creds2,
      void *reserved) nogil
  grpc_call_credentials *grpc_google_compute_engine_credentials_create(
      void *reserved) nogil
  grpc_call_credentials *grpc_service_account_jwt_access_credentials_create(
      const char *json_key,
      gpr_timespec token_lifetime, void *reserved) nogil
  grpc_call_credentials *grpc_google_refresh_token_credentials_create(
      const char *json_refresh_token, void *reserved) nogil
  grpc_call_credentials *grpc_google_iam_credentials_create(
      const char *authorization_token, const char *authority_selector,
      void *reserved) nogil
  void grpc_call_credentials_release(grpc_call_credentials *creds) nogil

  grpc_channel *grpc_channel_create(
      const char *target, grpc_channel_credentials *creds,
      const grpc_channel_args *args) nogil

  ctypedef struct grpc_server_credentials:
    # We don't care about the internals (and in fact don't know them)
    pass

  void grpc_server_credentials_release(grpc_server_credentials *creds) nogil

  int grpc_server_add_http2_port(grpc_server *server, const char *addr,
                                        grpc_server_credentials *creds) nogil

  grpc_call_error grpc_call_set_credentials(grpc_call *call,
                                            grpc_call_credentials *creds) nogil

  ctypedef struct grpc_auth_context:
    # We don't care about the internals (and in fact don't know them)
    pass

  ctypedef struct grpc_auth_metadata_context:
    const char *service_url
    const char *method_name
    const grpc_auth_context *channel_auth_context

  ctypedef void (*grpc_credentials_plugin_metadata_cb)(
      void *user_data, const grpc_metadata *creds_md, size_t num_creds_md,
      grpc_status_code status, const char *error_details) nogil

  ctypedef struct grpc_metadata_credentials_plugin:
    int (*get_metadata)(
        void *state, grpc_auth_metadata_context context,
        grpc_credentials_plugin_metadata_cb cb, void *user_data,
        grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
        size_t *num_creds_md, grpc_status_code *status,
        const char **error_details) except *
    void (*destroy)(void *state) except *
    void *state
    const char *type

  grpc_call_credentials *grpc_metadata_credentials_create_from_plugin(
      grpc_metadata_credentials_plugin plugin, grpc_security_level min_security_level, void *reserved) nogil

  ctypedef struct grpc_auth_property_iterator:
    pass

  ctypedef struct grpc_auth_property:
    char *name
    char *value
    size_t value_length

  grpc_auth_property *grpc_auth_property_iterator_next(
      grpc_auth_property_iterator *it)

  grpc_auth_property_iterator grpc_auth_context_property_iterator(
      const grpc_auth_context *ctx)

  grpc_auth_property_iterator grpc_auth_context_peer_identity(
      const grpc_auth_context *ctx)

  char *grpc_auth_context_peer_identity_property_name(
      const grpc_auth_context *ctx)

  grpc_auth_property_iterator grpc_auth_context_find_properties_by_name(
      const grpc_auth_context *ctx, const char *name)

  grpc_auth_context_peer_is_authenticated(
      const grpc_auth_context *ctx)

  grpc_auth_context *grpc_call_auth_context(grpc_call *call)

  void grpc_auth_context_release(grpc_auth_context *context)

  grpc_channel_credentials *grpc_local_credentials_create(
    grpc_local_connect_type type)
  grpc_server_credentials *grpc_local_server_credentials_create(
    grpc_local_connect_type type)

  ctypedef struct grpc_alts_credentials_options:
    # We don't care about the internals (and in fact don't know them)
    pass
 
  grpc_channel_credentials *grpc_alts_credentials_create(
    const grpc_alts_credentials_options *options)
  grpc_server_credentials *grpc_alts_server_credentials_create(
    const grpc_alts_credentials_options *options)

  grpc_alts_credentials_options* grpc_alts_credentials_client_options_create()
  grpc_alts_credentials_options* grpc_alts_credentials_server_options_create()
  void grpc_alts_credentials_options_destroy(grpc_alts_credentials_options *options)
  void grpc_alts_credentials_client_options_add_target_service_account(grpc_alts_credentials_options *options, const char *service_account)



cdef extern from "grpc/compression.h":

  ctypedef enum grpc_compression_algorithm:
    GRPC_COMPRESS_NONE
    GRPC_COMPRESS_DEFLATE
    GRPC_COMPRESS_GZIP
    GRPC_COMPRESS_STREAM_GZIP
    GRPC_COMPRESS_ALGORITHMS_COUNT

  ctypedef enum grpc_compression_level:
    GRPC_COMPRESS_LEVEL_NONE
    GRPC_COMPRESS_LEVEL_LOW
    GRPC_COMPRESS_LEVEL_MED
    GRPC_COMPRESS_LEVEL_HIGH
    GRPC_COMPRESS_LEVEL_COUNT

  ctypedef struct grpc_compression_options:
    uint32_t enabled_algorithms_bitset

  int grpc_compression_algorithm_parse(
      grpc_slice value, grpc_compression_algorithm *algorithm) nogil
  int grpc_compression_algorithm_name(grpc_compression_algorithm algorithm,
                                      const char **name) nogil
  grpc_compression_algorithm grpc_compression_algorithm_for_level(
      grpc_compression_level level, uint32_t accepted_encodings) nogil
  void grpc_compression_options_init(grpc_compression_options *opts) nogil
  void grpc_compression_options_enable_algorithm(
      grpc_compression_options *opts,
      grpc_compression_algorithm algorithm) nogil
  void grpc_compression_options_disable_algorithm(
      grpc_compression_options *opts,
      grpc_compression_algorithm algorithm) nogil
  int grpc_compression_options_is_algorithm_enabled(
      const grpc_compression_options *opts,
      grpc_compression_algorithm algorithm) nogil

cdef extern from "grpc/impl/codegen/compression_types.h":

  const char *_GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY \
    "GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY"


cdef extern from "grpc/grpc_security_constants.h":
  ctypedef enum grpc_local_connect_type:
    UDS
    LOCAL_TCP
