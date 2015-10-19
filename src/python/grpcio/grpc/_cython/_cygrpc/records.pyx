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

from grpc._cython._cygrpc cimport grpc
from grpc._cython._cygrpc cimport call
from grpc._cython._cygrpc cimport server


class StatusCode:
  ok = grpc.GRPC_STATUS_OK
  cancelled = grpc.GRPC_STATUS_CANCELLED
  unknown = grpc.GRPC_STATUS_UNKNOWN
  invalid_argument = grpc.GRPC_STATUS_INVALID_ARGUMENT
  deadline_exceeded = grpc.GRPC_STATUS_DEADLINE_EXCEEDED
  not_found = grpc.GRPC_STATUS_NOT_FOUND
  already_exists = grpc.GRPC_STATUS_ALREADY_EXISTS
  permission_denied = grpc.GRPC_STATUS_PERMISSION_DENIED
  unauthenticated = grpc.GRPC_STATUS_UNAUTHENTICATED
  resource_exhausted = grpc.GRPC_STATUS_RESOURCE_EXHAUSTED
  failed_precondition = grpc.GRPC_STATUS_FAILED_PRECONDITION
  aborted = grpc.GRPC_STATUS_ABORTED
  out_of_range = grpc.GRPC_STATUS_OUT_OF_RANGE
  unimplemented = grpc.GRPC_STATUS_UNIMPLEMENTED
  internal = grpc.GRPC_STATUS_INTERNAL
  unavailable = grpc.GRPC_STATUS_UNAVAILABLE
  data_loss = grpc.GRPC_STATUS_DATA_LOSS


class CallError:
  ok = grpc.GRPC_CALL_OK
  error = grpc.GRPC_CALL_ERROR
  not_on_server = grpc.GRPC_CALL_ERROR_NOT_ON_SERVER
  not_on_client = grpc.GRPC_CALL_ERROR_NOT_ON_CLIENT
  already_accepted = grpc.GRPC_CALL_ERROR_ALREADY_ACCEPTED
  already_invoked = grpc.GRPC_CALL_ERROR_ALREADY_INVOKED
  not_invoked = grpc.GRPC_CALL_ERROR_NOT_INVOKED
  already_finished = grpc.GRPC_CALL_ERROR_ALREADY_FINISHED
  too_many_operations = grpc.GRPC_CALL_ERROR_TOO_MANY_OPERATIONS
  invalid_flags = grpc.GRPC_CALL_ERROR_INVALID_FLAGS
  invalid_metadata = grpc.GRPC_CALL_ERROR_INVALID_METADATA


class CompletionType:
  queue_shutdown = grpc.GRPC_QUEUE_SHUTDOWN
  queue_timeout = grpc.GRPC_QUEUE_TIMEOUT
  operation_complete = grpc.GRPC_OP_COMPLETE


class OperationType:
  send_initial_metadata = grpc.GRPC_OP_SEND_INITIAL_METADATA
  send_message = grpc.GRPC_OP_SEND_MESSAGE
  send_close_from_client = grpc.GRPC_OP_SEND_CLOSE_FROM_CLIENT
  send_status_from_server = grpc.GRPC_OP_SEND_STATUS_FROM_SERVER
  receive_initial_metadata = grpc.GRPC_OP_RECV_INITIAL_METADATA
  receive_message = grpc.GRPC_OP_RECV_MESSAGE
  receive_status_on_client = grpc.GRPC_OP_RECV_STATUS_ON_CLIENT
  receive_close_on_server = grpc.GRPC_OP_RECV_CLOSE_ON_SERVER


cdef class Timespec:

  def __cinit__(self, time):
    if time is None:
      self.c_time = grpc.gpr_now(grpc.GPR_CLOCK_REALTIME)
    elif isinstance(time, float):
      if time == float("+inf"):
        self.c_time = grpc.gpr_inf_future(grpc.GPR_CLOCK_REALTIME)
      elif time == float("-inf"):
        self.c_time = grpc.gpr_inf_past(grpc.GPR_CLOCK_REALTIME)
      else:
        self.c_time.seconds = time
        self.c_time.nanoseconds = (time - float(self.c_time.seconds)) * 1e9
        self.c_time.clock_type = grpc.GPR_CLOCK_REALTIME
    else:
      raise TypeError("expected time to be float")

  @property
  def seconds(self):
    # TODO(atash) ensure that everywhere a Timespec is created that it's
    # converted to GPR_CLOCK_REALTIME then and not every time someone wants to
    # read values off in Python.
    cdef grpc.gpr_timespec real_time = (
        grpc.gpr_convert_clock_type(self.c_time, grpc.GPR_CLOCK_REALTIME))
    return real_time.seconds

  @property
  def nanoseconds(self):
    cdef grpc.gpr_timespec real_time = (
        grpc.gpr_convert_clock_type(self.c_time, grpc.GPR_CLOCK_REALTIME))
    return real_time.nanoseconds

  def __float__(self):
    cdef grpc.gpr_timespec real_time = (
        grpc.gpr_convert_clock_type(self.c_time, grpc.GPR_CLOCK_REALTIME))
    return <double>real_time.seconds + <double>real_time.nanoseconds / 1e9

  infinite_future = Timespec(float("+inf"))
  infinite_past = Timespec(float("-inf"))


cdef class CallDetails:

  def __cinit__(self):
    grpc.grpc_call_details_init(&self.c_details)

  def __dealloc__(self):
    grpc.grpc_call_details_destroy(&self.c_details)

  @property
  def method(self):
    if self.c_details.method != NULL:
      return <bytes>self.c_details.method
    else:
      return None

  @property
  def host(self):
    if self.c_details.host != NULL:
      return <bytes>self.c_details.host
    else:
      return None

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

  def __cinit__(self, grpc.grpc_completion_type type, bint success,
                object tag, call.Call operation_call,
                CallDetails request_call_details,
                Metadata request_metadata,
                Operations batch_operations):
    self.type = type
    self.success = success
    self.tag = tag
    self.operation_call = operation_call
    self.request_call_details = request_call_details
    self.request_metadata = request_metadata
    self.batch_operations = batch_operations


cdef class ByteBuffer:

  def __cinit__(self, data):
    if data is None:
      self.c_byte_buffer = NULL
      return
    if isinstance(data, bytes):
      pass
    elif isinstance(data, basestring):
      data = data.encode()
    else:
      raise TypeError("expected value to be of type str or bytes")

    cdef char *c_data = data
    data_slice = grpc.gpr_slice_from_copied_buffer(c_data, len(data))
    self.c_byte_buffer = grpc.grpc_raw_byte_buffer_create(
        &data_slice, 1)
    grpc.gpr_slice_unref(data_slice)

  def bytes(self):
    cdef grpc.grpc_byte_buffer_reader reader
    cdef grpc.gpr_slice data_slice
    cdef size_t data_slice_length
    cdef void *data_slice_pointer
    if self.c_byte_buffer != NULL:
      grpc.grpc_byte_buffer_reader_init(&reader, self.c_byte_buffer)
      result = b""
      while grpc.grpc_byte_buffer_reader_next(&reader, &data_slice):
        data_slice_pointer = grpc.gpr_slice_start_ptr(data_slice)
        data_slice_length = grpc.gpr_slice_length(data_slice)
        result += (<char *>data_slice_pointer)[:data_slice_length]
      grpc.grpc_byte_buffer_reader_destroy(&reader)
      return result
    else:
      return None

  def __len__(self):
    if self.c_byte_buffer != NULL:
      return grpc.grpc_byte_buffer_length(self.c_byte_buffer)
    else:
      return 0

  def __str__(self):
    return self.bytes()

  def __dealloc__(self):
    if self.c_byte_buffer != NULL:
      grpc.grpc_byte_buffer_destroy(self.c_byte_buffer)


cdef class SslPemKeyCertPair:

  def __cinit__(self, private_key, certificate_chain):
    if isinstance(private_key, bytes):
      self.private_key = private_key
    elif isinstance(private_key, basestring):
      self.private_key = private_key.encode()
    else:
      raise TypeError("expected private_key to be of type str or bytes")
    if isinstance(certificate_chain, bytes):
      self.certificate_chain = certificate_chain
    elif isinstance(certificate_chain, basestring):
      self.certificate_chain = certificate_chain.encode()
    else:
      raise TypeError("expected certificate_chain to be of type str or bytes "
                      "or int")
    self.c_pair.private_key = self.private_key
    self.c_pair.certificate_chain = self.certificate_chain


cdef class ChannelArg:

  def __cinit__(self, key, value):
    if isinstance(key, bytes):
      self.key = key
    elif isinstance(key, basestring):
      self.key = key.encode()
    else:
      raise TypeError("expected key to be of type str or bytes")
    if isinstance(value, bytes):
      self.value = value
      self.c_arg.type = grpc.GRPC_ARG_STRING
      self.c_arg.value.string = self.value
    elif isinstance(value, basestring):
      self.value = value.encode()
      self.c_arg.type = grpc.GRPC_ARG_STRING
      self.c_arg.value.string = self.value
    elif isinstance(value, int):
      self.value = int(value)
      self.c_arg.type = grpc.GRPC_ARG_INTEGER
      self.c_arg.value.integer = self.value
    else:
      raise TypeError("expected value to be of type str or bytes or int")
    self.c_arg.key = self.key


cdef class ChannelArgs:

  def __cinit__(self, args):
    self.args = list(args)
    for arg in self.args:
      if not isinstance(arg, ChannelArg):
        raise TypeError("expected list of ChannelArg")
    self.c_args.arguments_length = len(self.args)
    self.c_args.arguments = <grpc.grpc_arg *>grpc.gpr_malloc(
        self.c_args.arguments_length*sizeof(grpc.grpc_arg)
    )
    for i in range(self.c_args.arguments_length):
      self.c_args.arguments[i] = (<ChannelArg>self.args[i]).c_arg

  def __dealloc__(self):
    grpc.gpr_free(self.c_args.arguments)

  def __len__(self):
    # self.args is never stale; it's only updated from this file
    return len(self.args)

  def __getitem__(self, size_t i):
    # self.args is never stale; it's only updated from this file
    return self.args[i]


cdef class Metadatum:

  def __cinit__(self, key, value):
    if isinstance(key, bytes):
      self._key = key
    elif isinstance(key, basestring):
      self._key = key.encode()
    else:
      raise TypeError("expected key to be of type str or bytes")
    if isinstance(value, bytes):
      self._value = value
    elif isinstance(value, basestring):
      self._value = value.encode()
    else:
      raise TypeError("expected value to be of type str or bytes")
    self.c_metadata.key = self._key
    self.c_metadata.value = self._value
    self.c_metadata.value_length = len(self._value)

  @property
  def key(self):
    return <bytes>self.c_metadata.key

  @property
  def value(self):
    return <bytes>self.c_metadata.value[:self.c_metadata.value_length]

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


cdef class _MetadataIterator:

  cdef size_t i
  cdef Metadata metadata

  def __cinit__(self, Metadata metadata not None):
    self.i = 0
    self.metadata = metadata

  def __iter__(self):
    return self

  def __next__(self):
    if self.i < len(self.metadata):
      result = self.metadata[self.i]
      self.i = self.i + 1
      return result
    else:
      raise StopIteration


cdef class Metadata:

  def __cinit__(self, metadata):
    self.metadata = list(metadata)
    for metadatum in metadata:
      if not isinstance(metadatum, Metadatum):
        raise TypeError("expected list of Metadatum")
    grpc.grpc_metadata_array_init(&self.c_metadata_array)
    self.c_metadata_array.count = len(self.metadata)
    self.c_metadata_array.capacity = len(self.metadata)
    self.c_metadata_array.metadata = <grpc.grpc_metadata *>grpc.gpr_malloc(
        self.c_metadata_array.count*sizeof(grpc.grpc_metadata)
    )
    for i in range(self.c_metadata_array.count):
      self.c_metadata_array.metadata[i] = (
          (<Metadatum>self.metadata[i]).c_metadata)

  def __dealloc__(self):
    # this frees the allocated memory for the grpc_metadata_array (although
    # it'd be nice if that were documented somewhere...) TODO(atash): document
    # this in the C core
    grpc.grpc_metadata_array_destroy(&self.c_metadata_array)

  def __len__(self):
    return self.c_metadata_array.count

  def __getitem__(self, size_t i):
    return Metadatum(
        key=<bytes>self.c_metadata_array.metadata[i].key,
        value=<bytes>self.c_metadata_array.metadata[i].value[
            :self.c_metadata_array.metadata[i].value_length])

  def __iter__(self):
    return _MetadataIterator(self)


cdef class Operation:

  def __cinit__(self):
    self.references = []
    self._received_status_details = NULL
    self._received_status_details_capacity = 0
    self.is_valid = False

  @property
  def type(self):
    return self.c_op.type

  @property
  def received_message(self):
    if self.c_op.type != grpc.GRPC_OP_RECV_MESSAGE:
      raise TypeError("self must be an operation receiving a message")
    return self._received_message

  @property
  def received_metadata(self):
    if (self.c_op.type != grpc.GRPC_OP_RECV_INITIAL_METADATA and
        self.c_op.type != grpc.GRPC_OP_RECV_STATUS_ON_CLIENT):
      raise TypeError("self must be an operation receiving metadata")
    return self._received_metadata

  @property
  def received_status_code(self):
    if self.c_op.type != grpc.GRPC_OP_RECV_STATUS_ON_CLIENT:
      raise TypeError("self must be an operation receiving a status code")
    return self._received_status_code

  @property
  def received_status_details(self):
    if self.c_op.type != grpc.GRPC_OP_RECV_STATUS_ON_CLIENT:
      raise TypeError("self must be an operation receiving status details")
    if self._received_status_details:
      return self._received_status_details
    else:
      return None

  @property
  def received_cancelled(self):
    if self.c_op.type != grpc.GRPC_OP_RECV_CLOSE_ON_SERVER:
      raise TypeError("self must be an operation receiving cancellation "
                      "information")
    return False if self._received_cancelled == 0 else True

  def __dealloc__(self):
    # We *almost* don't need to do anything; most of the objects are handled by
    # Python. The remaining one(s) are primitive fields filled in by GRPC core.
    # This means that we need to clean up after receive_status_on_client.
    if self.c_op.type == grpc.GRPC_OP_RECV_STATUS_ON_CLIENT:
      grpc.gpr_free(self._received_status_details)

def operation_send_initial_metadata(Metadata metadata):
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_SEND_INITIAL_METADATA
  op.c_op.data.send_initial_metadata.count = metadata.c_metadata_array.count
  op.c_op.data.send_initial_metadata.metadata = (
      metadata.c_metadata_array.metadata)
  op.references.append(metadata)
  op.is_valid = True
  return op

def operation_send_message(data):
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_SEND_MESSAGE
  byte_buffer = ByteBuffer(data)
  op.c_op.data.send_message = byte_buffer.c_byte_buffer
  op.references.append(byte_buffer)
  op.is_valid = True
  return op

def operation_send_close_from_client():
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_SEND_CLOSE_FROM_CLIENT
  op.is_valid = True
  return op

def operation_send_status_from_server(
    Metadata metadata, grpc.grpc_status_code code, details):
  if isinstance(details, bytes):
    pass
  elif isinstance(details, basestring):
    details = details.encode()
  else:
    raise TypeError("expected a str or bytes object for details")
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_SEND_STATUS_FROM_SERVER
  op.c_op.data.send_status_from_server.trailing_metadata_count = (
      metadata.c_metadata_array.count)
  op.c_op.data.send_status_from_server.trailing_metadata = (
      metadata.c_metadata_array.metadata)
  op.c_op.data.send_status_from_server.status = code
  op.c_op.data.send_status_from_server.status_details = details
  op.references.append(metadata)
  op.references.append(details)
  op.is_valid = True
  return op

def operation_receive_initial_metadata():
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_RECV_INITIAL_METADATA
  op._received_metadata = Metadata([])
  op.c_op.data.receive_initial_metadata = (
      &op._received_metadata.c_metadata_array)
  op.is_valid = True
  return op

def operation_receive_message():
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_RECV_MESSAGE
  op._received_message = ByteBuffer(None)
  # n.b. the c_op.data.receive_message field needs to be deleted by us,
  # anyway, so we just let that be handled by the ByteBuffer() we allocated
  # the line before.
  op.c_op.data.receive_message = &op._received_message.c_byte_buffer
  op.is_valid = True
  return op

def operation_receive_status_on_client():
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_RECV_STATUS_ON_CLIENT
  op._received_metadata = Metadata([])
  op.c_op.data.receive_status_on_client.trailing_metadata = (
      &op._received_metadata.c_metadata_array)
  op.c_op.data.receive_status_on_client.status = (
      &op._received_status_code)
  op.c_op.data.receive_status_on_client.status_details = (
      &op._received_status_details)
  op.c_op.data.receive_status_on_client.status_details_capacity = (
      &op._received_status_details_capacity)
  op.is_valid = True
  return op

def operation_receive_close_on_server():
  cdef Operation op = Operation()
  op.c_op.type = grpc.GRPC_OP_RECV_CLOSE_ON_SERVER
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
      raise StopIteration


cdef class Operations:

  def __cinit__(self, operations):
    self.operations = list(operations)  # normalize iterable
    self.c_ops = NULL
    self.c_nops = 0
    for operation in self.operations:
      if not isinstance(operation, Operation):
        raise TypeError("expected operations to be iterable of Operation")
    self.c_nops = len(self.operations)
    self.c_ops = <grpc.grpc_op *>grpc.gpr_malloc(
        sizeof(grpc.grpc_op)*self.c_nops)
    for i in range(self.c_nops):
      self.c_ops[i] = (<Operation>(self.operations[i])).c_op

  def __len__(self):
    return self.c_nops

  def __getitem__(self, size_t i):
    # self.operations is never stale; it's only updated from this file
    return self.operations[i]

  def __dealloc__(self):
    grpc.gpr_free(self.c_ops)

  def __iter__(self):
    return _OperationsIterator(self)

