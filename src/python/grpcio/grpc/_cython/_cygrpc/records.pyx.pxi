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

  def __cinit__(self, user_tag):
    self.user_tag = user_tag
    self.references = []


cdef class Event:

  def __cinit__(self, grpc_completion_type type, bint success,
                object tag, Call operation_call,
                CallDetails request_call_details,
                object request_metadata,
                bint is_new_request,
                Operations batch_operations):
    self.type = type
    self.success = success
    self.tag = tag
    self.operation_call = operation_call
    self.request_call_details = request_call_details
    self.request_metadata = request_metadata
    self.batch_operations = batch_operations
    self.is_new_request = is_new_request


cdef class ByteBuffer:

  def __cinit__(self, bytes data):
    grpc_init()
    if data is None:
      self.c_byte_buffer = NULL
      return

    cdef char *c_data = data
    cdef grpc_slice data_slice
    cdef size_t data_length = len(data)
    with nogil:
      data_slice = grpc_slice_from_copied_buffer(c_data, data_length)
    with nogil:
      self.c_byte_buffer = grpc_raw_byte_buffer_create(
          &data_slice, 1)
    with nogil:
      grpc_slice_unref(data_slice)

  def bytes(self):
    cdef grpc_byte_buffer_reader reader
    cdef grpc_slice data_slice
    cdef size_t data_slice_length
    cdef void *data_slice_pointer
    cdef bint reader_status
    if self.c_byte_buffer != NULL:
      with nogil:
        reader_status = grpc_byte_buffer_reader_init(
            &reader, self.c_byte_buffer)
      if not reader_status:
        return None
      result = bytearray()
      with nogil:
        while grpc_byte_buffer_reader_next(&reader, &data_slice):
          data_slice_pointer = grpc_slice_start_ptr(data_slice)
          data_slice_length = grpc_slice_length(data_slice)
          with gil:
            result += (<char *>data_slice_pointer)[:data_slice_length]
          grpc_slice_unref(data_slice)
      with nogil:
        grpc_byte_buffer_reader_destroy(&reader)
      return bytes(result)
    else:
      return None

  def __len__(self):
    cdef size_t result
    if self.c_byte_buffer != NULL:
      with nogil:
        result = grpc_byte_buffer_length(self.c_byte_buffer)
      return result
    else:
      return 0

  def __str__(self):
    return self.bytes()

  def __dealloc__(self):
    if self.c_byte_buffer != NULL:
      grpc_byte_buffer_destroy(self.c_byte_buffer)
    grpc_shutdown()


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


cdef class Metadatum:

  def __cinit__(self, bytes key, bytes value):
    self.c_metadata.key = _slice_from_bytes(key)
    self.c_metadata.value = _slice_from_bytes(value)

  cdef void _copy_metadatum(self, grpc_metadata *destination) nogil:
    destination[0].key = _copy_slice(self.c_metadata.key)
    destination[0].value = _copy_slice(self.c_metadata.value)

  @property
  def key(self):
    return _slice_bytes(self.c_metadata.key)

  @property
  def value(self):
    return _slice_bytes(self.c_metadata.value)

  def __len__(self):
    return 2

  def __getitem__(self, size_t i):
    if i == 0:
      return self.key
    elif i == 1:
      return self.value
    else:
      raise IndexError("index must be 0 (key) or 1 (value)")

  def __iter__(self):
    return iter((self.key, self.value))

  def __dealloc__(self):
    grpc_slice_unref(self.c_metadata.key)
    grpc_slice_unref(self.c_metadata.value)

cdef class _MetadataIterator:

  cdef size_t i
  cdef size_t _length
  cdef object _metadatum_indexable

  def __cinit__(self, length, metadatum_indexable):
    self._length = length
    self._metadatum_indexable = metadatum_indexable
    self.i = 0

  def __iter__(self):
    return self

  def __next__(self):
    if self.i < self._length:
      result = self._metadatum_indexable[self.i]
      self.i = self.i + 1
      return result
    else:
      raise StopIteration()


# TODO(https://github.com/grpc/grpc/issues/7950): Eliminate this; just use an
# ordinary sequence of pairs of bytestrings all the way down to the
# grpc_call_start_batch call.
cdef class Metadata:
  """Metadata being passed from application to core."""

  def __cinit__(self, metadata_iterable):
    metadata_sequence = tuple(metadata_iterable)
    cdef size_t count = len(metadata_sequence)
    with nogil:
      grpc_init()
      self.c_metadata = <grpc_metadata *>gpr_malloc(
          count * sizeof(grpc_metadata))
      self.c_count = count
    for index, metadatum in enumerate(metadata_sequence):
      self.c_metadata[index].key = grpc_slice_copy(
          (<Metadatum>metadatum).c_metadata.key)
      self.c_metadata[index].value = grpc_slice_copy(
          (<Metadatum>metadatum).c_metadata.value)

  def __dealloc__(self):
    with nogil:
      for index in range(self.c_count):
        grpc_slice_unref(self.c_metadata[index].key)
        grpc_slice_unref(self.c_metadata[index].value)
      gpr_free(self.c_metadata)
      grpc_shutdown()

  def __len__(self):
    return self.c_count

  def __getitem__(self, size_t index):
    if index < self.c_count:
      key = _slice_bytes(self.c_metadata[index].key)
      value = _slice_bytes(self.c_metadata[index].value)
      return Metadatum(key, value)
    else:
      raise IndexError()

  def __iter__(self):
    return _MetadataIterator(self.c_count, self)


cdef class MetadataArray:
  """Metadata being passed from core to application."""

  def __cinit__(self):
    with nogil:
      grpc_init()
      grpc_metadata_array_init(&self.c_metadata_array)

  def __dealloc__(self):
    with nogil:
      grpc_metadata_array_destroy(&self.c_metadata_array)
      grpc_shutdown()

  def __len__(self):
    return self.c_metadata_array.count

  def __getitem__(self, size_t i):
    if i >= self.c_metadata_array.count:
      raise IndexError()
    key = _slice_bytes(self.c_metadata_array.metadata[i].key)
    value = _slice_bytes(self.c_metadata_array.metadata[i].value)
    return Metadatum(key=key, value=value)

  def __iter__(self):
    return _MetadataIterator(self.c_metadata_array.count, self)


cdef class Operation:

  def __cinit__(self):
    grpc_init()
    self.references = []
    self._status_details = grpc_empty_slice()
    self.is_valid = False

  @property
  def type(self):
    return self.c_op.type

  @property
  def flags(self):
    return self.c_op.flags

  @property
  def has_status(self):
    return self.c_op.type == GRPC_OP_RECV_STATUS_ON_CLIENT

  @property
  def received_message(self):
    if self.c_op.type != GRPC_OP_RECV_MESSAGE:
      raise TypeError("self must be an operation receiving a message")
    return self._received_message

  @property
  def received_message_or_none(self):
    if self.c_op.type != GRPC_OP_RECV_MESSAGE:
      return None
    return self._received_message

  @property
  def received_metadata(self):
    if (self.c_op.type != GRPC_OP_RECV_INITIAL_METADATA and
        self.c_op.type != GRPC_OP_RECV_STATUS_ON_CLIENT):
      raise TypeError("self must be an operation receiving metadata")
    # TODO(https://github.com/grpc/grpc/issues/7950): Drop the "all Cython
    # objects must be legitimate for use from Python at any time" policy in
    # place today, shift the policy toward "Operation objects are only usable
    # while their calls are active", and move this making-a-copy-because-this-
    # data-needs-to-live-much-longer-than-the-call-from-which-it-arose to the
    # lowest Python layer.
    return tuple(self._received_metadata)

  @property
  def received_status_code(self):
    if self.c_op.type != GRPC_OP_RECV_STATUS_ON_CLIENT:
      raise TypeError("self must be an operation receiving a status code")
    return self._received_status_code

  @property
  def received_status_code_or_none(self):
    if self.c_op.type != GRPC_OP_RECV_STATUS_ON_CLIENT:
      return None
    return self._received_status_code

  @property
  def received_status_details(self):
    if self.c_op.type != GRPC_OP_RECV_STATUS_ON_CLIENT:
      raise TypeError("self must be an operation receiving status details")
    return _slice_bytes(self._status_details)

  @property
  def received_status_details_or_none(self):
    if self.c_op.type != GRPC_OP_RECV_STATUS_ON_CLIENT:
      return None
    return _slice_bytes(self._status_details)

  @property
  def received_cancelled(self):
    if self.c_op.type != GRPC_OP_RECV_CLOSE_ON_SERVER:
      raise TypeError("self must be an operation receiving cancellation "
                      "information")
    return False if self._received_cancelled == 0 else True

  @property
  def received_cancelled_or_none(self):
    if self.c_op.type != GRPC_OP_RECV_CLOSE_ON_SERVER:
      return None
    return False if self._received_cancelled == 0 else True

  def __dealloc__(self):
    grpc_slice_unref(self._status_details)
    grpc_shutdown()

def operation_send_initial_metadata(Metadata metadata, int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_SEND_INITIAL_METADATA
  op.c_op.flags = flags
  op.c_op.data.send_initial_metadata.count = metadata.c_count
  op.c_op.data.send_initial_metadata.metadata = metadata.c_metadata
  op.references.append(metadata)
  op.is_valid = True
  return op

def operation_send_message(data, int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_SEND_MESSAGE
  op.c_op.flags = flags
  byte_buffer = ByteBuffer(data)
  op.c_op.data.send_message.send_message = byte_buffer.c_byte_buffer
  op.references.append(byte_buffer)
  op.is_valid = True
  return op

def operation_send_close_from_client(int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_SEND_CLOSE_FROM_CLIENT
  op.c_op.flags = flags
  op.is_valid = True
  return op

def operation_send_status_from_server(
    Metadata metadata, grpc_status_code code, bytes details, int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_SEND_STATUS_FROM_SERVER
  op.c_op.flags = flags
  op.c_op.data.send_status_from_server.trailing_metadata_count = (
      metadata.c_count)
  op.c_op.data.send_status_from_server.trailing_metadata = metadata.c_metadata
  op.c_op.data.send_status_from_server.status = code
  grpc_slice_unref(op._status_details)
  op._status_details = _slice_from_bytes(details)
  op.c_op.data.send_status_from_server.status_details = &op._status_details
  op.references.append(metadata)
  op.is_valid = True
  return op

def operation_receive_initial_metadata(int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_RECV_INITIAL_METADATA
  op.c_op.flags = flags
  op._received_metadata = MetadataArray()
  op.c_op.data.receive_initial_metadata.receive_initial_metadata = (
      &op._received_metadata.c_metadata_array)
  op.is_valid = True
  return op

def operation_receive_message(int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_RECV_MESSAGE
  op.c_op.flags = flags
  op._received_message = ByteBuffer(None)
  # n.b. the c_op.data.receive_message field needs to be deleted by us,
  # anyway, so we just let that be handled by the ByteBuffer() we allocated
  # the line before.
  op.c_op.data.receive_message.receive_message = (
      &op._received_message.c_byte_buffer)
  op.is_valid = True
  return op

def operation_receive_status_on_client(int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_RECV_STATUS_ON_CLIENT
  op.c_op.flags = flags
  op._received_metadata = MetadataArray()
  op.c_op.data.receive_status_on_client.trailing_metadata = (
      &op._received_metadata.c_metadata_array)
  op.c_op.data.receive_status_on_client.status = (
      &op._received_status_code)
  op.c_op.data.receive_status_on_client.status_details = (
      &op._status_details)
  op.is_valid = True
  return op

def operation_receive_close_on_server(int flags):
  cdef Operation op = Operation()
  op.c_op.type = GRPC_OP_RECV_CLOSE_ON_SERVER
  op.c_op.flags = flags
  op.c_op.data.receive_close_on_server.cancelled = &op._received_cancelled
  op.is_valid = True
  return op


cdef class _OperationsIterator:

  cdef size_t i
  cdef Operations operations

  def __cinit__(self, Operations operations not None):
    self.i = 0
    self.operations = operations

  def __iter__(self):
    return self

  def __next__(self):
    if self.i < len(self.operations):
      result = self.operations[self.i]
      self.i = self.i + 1
      return result
    else:
      raise StopIteration()


cdef class Operations:

  def __cinit__(self, operations):
    grpc_init()
    self.operations = list(operations)  # normalize iterable
    self.c_ops = NULL
    self.c_nops = 0
    for operation in self.operations:
      if not isinstance(operation, Operation):
        raise TypeError("expected operations to be iterable of Operation")
    self.c_nops = len(self.operations)
    with nogil:
      self.c_ops = <grpc_op *>gpr_malloc(sizeof(grpc_op)*self.c_nops)
    for i in range(self.c_nops):
      self.c_ops[i] = (<Operation>(self.operations[i])).c_op

  def __len__(self):
    return self.c_nops

  def __getitem__(self, size_t i):
    # self.operations is never stale; it's only updated from this file
    return self.operations[i]

  def __dealloc__(self):
    with nogil:
      gpr_free(self.c_ops)
    grpc_shutdown()

  def __iter__(self):
    return _OperationsIterator(self)


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
