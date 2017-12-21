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

from libc.stdint cimport intptr_t


cdef bytes _slice_bytes(grpc_slice slice):
  cdef void *start = grpc_slice_start_ptr(slice)
  cdef size_t length = grpc_slice_length(slice)
  return (<const char *>start)[:length]

cdef grpc_slice _copy_slice(grpc_slice slice) nogil:
  cdef void *start = grpc_slice_start_ptr(slice)
  cdef size_t length = grpc_slice_length(slice)
  return grpc_slice_from_copied_buffer(<const char *>start, length)

cdef grpc_slice _slice_from_bytes(bytes value) nogil:
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


class CompressionAlgorithm:
  none = GRPC_COMPRESS_NONE
  deflate = GRPC_COMPRESS_DEFLATE
  gzip = GRPC_COMPRESS_GZIP


class CompressionLevel:
  none = GRPC_COMPRESS_LEVEL_NONE
  low = GRPC_COMPRESS_LEVEL_LOW
  medium = GRPC_COMPRESS_LEVEL_MED
  high = GRPC_COMPRESS_LEVEL_HIGH


cdef class Timespec:

  def __cinit__(self, time):
    if time is None:
      with nogil:
        self.c_time = gpr_now(GPR_CLOCK_REALTIME)
      return
    if isinstance(time, int):
      time = float(time)
    if isinstance(time, float):
      if time == float("+inf"):
        with nogil:
          self.c_time = gpr_inf_future(GPR_CLOCK_REALTIME)
      elif time == float("-inf"):
        with nogil:
          self.c_time = gpr_inf_past(GPR_CLOCK_REALTIME)
      else:
        self.c_time.seconds = time
        self.c_time.nanoseconds = (time - float(self.c_time.seconds)) * 1e9
        self.c_time.clock_type = GPR_CLOCK_REALTIME
    elif isinstance(time, Timespec):
      self.c_time = (<Timespec>time).c_time
    else:
      raise TypeError("expected time to be float, int, or Timespec, not {}"
                          .format(type(time)))

  @property
  def seconds(self):
    # TODO(atash) ensure that everywhere a Timespec is created that it's
    # converted to GPR_CLOCK_REALTIME then and not every time someone wants to
    # read values off in Python.
    cdef gpr_timespec real_time
    with nogil:
      real_time = (
          gpr_convert_clock_type(self.c_time, GPR_CLOCK_REALTIME))
    return real_time.seconds

  @property
  def nanoseconds(self):
    cdef gpr_timespec real_time = (
        gpr_convert_clock_type(self.c_time, GPR_CLOCK_REALTIME))
    return real_time.nanoseconds

  def __float__(self):
    cdef gpr_timespec real_time = (
        gpr_convert_clock_type(self.c_time, GPR_CLOCK_REALTIME))
    return <double>real_time.seconds + <double>real_time.nanoseconds / 1e9

  def __richcmp__(Timespec self not None, Timespec other not None, int op):
    cdef gpr_timespec self_c_time = self.c_time
    cdef gpr_timespec other_c_time = other.c_time
    cdef int result = gpr_time_cmp(self_c_time, other_c_time)
    if op == 0:  # <
      return result < 0
    elif op == 2:  # ==
      return result == 0
    elif op == 4:  # >
      return result > 0
    elif op == 1:  # <=
      return result <= 0
    elif op == 3:  # !=
      return result != 0
    elif op == 5:  # >=
      return result >= 0
    else:
      raise ValueError('__richcmp__ `op` contract violated')


cdef class CallDetails:

  def __cinit__(self):
    grpc_init()
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
    timespec = Timespec(float("-inf"))
    timespec.c_time = self.c_details.deadline
    return timespec


cdef class OperationTag:

  def __cinit__(self, user_tag, operations):
    self.user_tag = user_tag
    self.references = []
    self._operations = operations

  cdef void store_ops(self):
    self.c_nops = 0 if self._operations is None else len(self._operations)
    if 0 < self.c_nops:
      self.c_ops = <grpc_op *>gpr_malloc(sizeof(grpc_op) * self.c_nops)
      for index, operation in enumerate(self._operations):
        (<Operation>operation).c()
        self.c_ops[index] = (<Operation>operation).c_op

  cdef object release_ops(self):
    if 0 < self.c_nops:
      for index, operation in enumerate(self._operations):
        (<Operation>operation).c_op = self.c_ops[index]
        (<Operation>operation).un_c()
      gpr_free(self.c_ops)
      return self._operations
    else:
      return ()


cdef class Event:

  def __cinit__(self, grpc_completion_type type, bint success,
                object tag, Call operation_call,
                CallDetails request_call_details,
                object request_metadata,
                bint is_new_request,
                object batch_operations):
    self.type = type
    self.success = success
    self.tag = tag
    self.operation_call = operation_call
    self.request_call_details = request_call_details
    self.request_metadata = request_metadata
    self.batch_operations = batch_operations
    self.is_new_request = is_new_request


cdef class SslPemKeyCertPair:

  def __cinit__(self, bytes private_key, bytes certificate_chain):
    self.private_key = private_key
    self.certificate_chain = certificate_chain
    self.c_pair.private_key = self.private_key
    self.c_pair.certificate_chain = self.certificate_chain



cdef void* copy_ptr(void* ptr):
  return ptr


cdef void destroy_ptr(grpc_exec_ctx* ctx, void* ptr):
  pass


cdef int compare_ptr(void* ptr1, void* ptr2):
  if ptr1 < ptr2:
    return -1
  elif ptr1 > ptr2:
    return 1
  else:
    return 0


cdef class ChannelArg:

  def __cinit__(self, bytes key, value):
    self.key = key
    self.value = value
    self.c_arg.key = self.key
    if isinstance(value, int):
      self.c_arg.type = GRPC_ARG_INTEGER
      self.c_arg.value.integer = self.value
    elif isinstance(value, bytes):
      self.c_arg.type = GRPC_ARG_STRING
      self.c_arg.value.string = self.value
    elif hasattr(value, '__int__'):
      # Pointer objects must override __int__() to return
      # the underlying C address (Python ints are word size).  The
      # lifecycle of the pointer is fixed to the lifecycle of the
      # python object wrapping it.
      self.ptr_vtable.copy = &copy_ptr
      self.ptr_vtable.destroy = &destroy_ptr
      self.ptr_vtable.cmp = &compare_ptr
      self.c_arg.type = GRPC_ARG_POINTER
      self.c_arg.value.pointer.vtable = &self.ptr_vtable
      self.c_arg.value.pointer.address = <void*>(<intptr_t>int(self.value))
    else:
      # TODO Add supported pointer types to this message
      raise TypeError('Expected int or bytes, got {}'.format(type(value)))


cdef class ChannelArgs:

  def __cinit__(self, args):
    grpc_init()
    self.args = list(args)
    for arg in self.args:
      if not isinstance(arg, ChannelArg):
        raise TypeError("expected list of ChannelArg")
    self.c_args.arguments_length = len(self.args)
    with nogil:
      self.c_args.arguments = <grpc_arg *>gpr_malloc(
          self.c_args.arguments_length*sizeof(grpc_arg))
    for i in range(self.c_args.arguments_length):
      self.c_args.arguments[i] = (<ChannelArg>self.args[i]).c_arg

  def __dealloc__(self):
    with nogil:
      gpr_free(self.c_args.arguments)
    grpc_shutdown()

  def __len__(self):
    # self.args is never stale; it's only updated from this file
    return len(self.args)

  def __getitem__(self, size_t i):
    # self.args is never stale; it's only updated from this file
    return self.args[i]


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
    return ChannelArg(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
                      self.c_options.enabled_algorithms_bitset)


def compression_algorithm_name(grpc_compression_algorithm algorithm):
  cdef const char* name
  with nogil:
    grpc_compression_algorithm_name(algorithm, &name)
  # Let Cython do the right thing with string casting
  return name
