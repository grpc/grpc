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


cdef bytes _slice_bytes(grpc_slice slice):
  cdef void *start = grpc_slice_start_ptr(slice)
  cdef size_t length = grpc_slice_length(slice)
  return (<const char *>start)[:length]

cdef grpc_slice _copy_slice(grpc_slice slice) noexcept nogil:
  cdef void *start = grpc_slice_start_ptr(slice)
  cdef size_t length = grpc_slice_length(slice)
  return grpc_slice_from_copied_buffer(<const char *>start, length)

cdef grpc_slice _slice_from_bytes(bytes value) noexcept nogil:
  cdef const char *value_ptr
  cdef size_t length
  with gil:
    value_ptr = <const char *>value
    length = len(value)
  return grpc_slice_from_copied_buffer(value_ptr, length)


class ConnectivityState:
  idle = GRPC_CHANNEL_IDLE
  connecting = GRPC_CHANNEL_CONNECTING
  ready = GRPC_CHANNEL_READY
  transient_failure = GRPC_CHANNEL_TRANSIENT_FAILURE
  shutdown = GRPC_CHANNEL_SHUTDOWN


class ChannelArgKey:
  enable_census = GRPC_ARG_ENABLE_CENSUS
  max_concurrent_streams = GRPC_ARG_MAX_CONCURRENT_STREAMS
  max_receive_message_length = GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH
  max_send_message_length = GRPC_ARG_MAX_SEND_MESSAGE_LENGTH
  http2_initial_sequence_number = GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER
  default_authority = GRPC_ARG_DEFAULT_AUTHORITY
  primary_user_agent_string = GRPC_ARG_PRIMARY_USER_AGENT_STRING
  secondary_user_agent_string = GRPC_ARG_SECONDARY_USER_AGENT_STRING
  ssl_session_cache = GRPC_SSL_SESSION_CACHE_ARG
  ssl_target_name_override = GRPC_SSL_TARGET_NAME_OVERRIDE_ARG


class WriteFlag:
  buffer_hint = GRPC_WRITE_BUFFER_HINT
  no_compress = GRPC_WRITE_NO_COMPRESS


class StatusCode:
  ok = GRPC_STATUS_OK
  cancelled = GRPC_STATUS_CANCELLED
  unknown = GRPC_STATUS_UNKNOWN
  invalid_argument = GRPC_STATUS_INVALID_ARGUMENT
  deadline_exceeded = GRPC_STATUS_DEADLINE_EXCEEDED
  not_found = GRPC_STATUS_NOT_FOUND
  already_exists = GRPC_STATUS_ALREADY_EXISTS
  permission_denied = GRPC_STATUS_PERMISSION_DENIED
  unauthenticated = GRPC_STATUS_UNAUTHENTICATED
  resource_exhausted = GRPC_STATUS_RESOURCE_EXHAUSTED
  failed_precondition = GRPC_STATUS_FAILED_PRECONDITION
  aborted = GRPC_STATUS_ABORTED
  out_of_range = GRPC_STATUS_OUT_OF_RANGE
  unimplemented = GRPC_STATUS_UNIMPLEMENTED
  internal = GRPC_STATUS_INTERNAL
  unavailable = GRPC_STATUS_UNAVAILABLE
  data_loss = GRPC_STATUS_DATA_LOSS


class CallError:
  ok = GRPC_CALL_OK
  error = GRPC_CALL_ERROR
  not_on_server = GRPC_CALL_ERROR_NOT_ON_SERVER
  not_on_client = GRPC_CALL_ERROR_NOT_ON_CLIENT
  already_accepted = GRPC_CALL_ERROR_ALREADY_ACCEPTED
  already_invoked = GRPC_CALL_ERROR_ALREADY_INVOKED
  not_invoked = GRPC_CALL_ERROR_NOT_INVOKED
  already_finished = GRPC_CALL_ERROR_ALREADY_FINISHED
  too_many_operations = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS
  invalid_flags = GRPC_CALL_ERROR_INVALID_FLAGS
  invalid_metadata = GRPC_CALL_ERROR_INVALID_METADATA


class CompletionType:
  queue_shutdown = GRPC_QUEUE_SHUTDOWN
  queue_timeout = GRPC_QUEUE_TIMEOUT
  operation_complete = GRPC_OP_COMPLETE


class OperationType:
  send_initial_metadata = GRPC_OP_SEND_INITIAL_METADATA
  send_message = GRPC_OP_SEND_MESSAGE
  send_close_from_client = GRPC_OP_SEND_CLOSE_FROM_CLIENT
  send_status_from_server = GRPC_OP_SEND_STATUS_FROM_SERVER
  receive_initial_metadata = GRPC_OP_RECV_INITIAL_METADATA
  receive_message = GRPC_OP_RECV_MESSAGE
  receive_status_on_client = GRPC_OP_RECV_STATUS_ON_CLIENT
  receive_close_on_server = GRPC_OP_RECV_CLOSE_ON_SERVER

GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM= (
  _GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM)

GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY = (
  _GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY)

class CompressionAlgorithm:
  none = GRPC_COMPRESS_NONE
  deflate = GRPC_COMPRESS_DEFLATE
  gzip = GRPC_COMPRESS_GZIP


class CompressionLevel:
  none = GRPC_COMPRESS_LEVEL_NONE
  low = GRPC_COMPRESS_LEVEL_LOW
  medium = GRPC_COMPRESS_LEVEL_MED
  high = GRPC_COMPRESS_LEVEL_HIGH


cdef class CallDetails:

  def __cinit__(self):
    fork_handlers_and_grpc_init()
    with nogil:
      grpc_call_details_init(&self.c_details)

  def __dealloc__(self):
    with nogil:
      grpc_call_details_destroy(&self.c_details)
    grpc_shutdown()

  @property
  def method(self):
    return _slice_bytes(self.c_details.method)

  @property
  def host(self):
    return _slice_bytes(self.c_details.host)

  @property
  def deadline(self):
    return _time_from_timespec(self.c_details.deadline)


cdef class SslPemKeyCertPair:

  def __cinit__(self, bytes private_key, bytes certificate_chain):
    self.private_key = private_key
    self.certificate_chain = certificate_chain
    self.c_pair.private_key = self.private_key
    self.c_pair.certificate_chain = self.certificate_chain


cdef class CompressionOptions:

  def __cinit__(self):
    with nogil:
      grpc_compression_options_init(&self.c_options)

  def enable_algorithm(self, grpc_compression_algorithm algorithm):
    with nogil:
      grpc_compression_options_enable_algorithm(&self.c_options, algorithm)

  def disable_algorithm(self, grpc_compression_algorithm algorithm):
    with nogil:
      grpc_compression_options_disable_algorithm(&self.c_options, algorithm)

  def is_algorithm_enabled(self, grpc_compression_algorithm algorithm):
    cdef int result
    with nogil:
      result = grpc_compression_options_is_algorithm_enabled(
          &self.c_options, algorithm)
    return result

  def to_channel_arg(self):
    return (
        GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
        self.c_options.enabled_algorithms_bitset,
    )


def compression_algorithm_name(grpc_compression_algorithm algorithm):
  cdef const char* name
  with nogil:
    grpc_compression_algorithm_name(algorithm, &name)
  # Let Cython do the right thing with string casting
  return name


def reset_grpc_config_vars():
  ConfigVars.Reset()
