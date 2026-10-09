# Copyright 2017 gRPC authors.
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

from cpython.bytes cimport PyBytes_AS_STRING, PyBytes_FromStringAndSize
from libc.string cimport memcpy


# Received messages at least this large are copied out of their slices without
# the GIL. Below it the copy costs less than a GIL hand-off, and this is also
# the size from which CPython's bytes.join releases the GIL, so the receive
# path releases it exactly where it did when the message was built with
# b"".join().
cdef size_t _NOGIL_COPY_MIN_BYTES = 1024 * 1024


cdef size_t _copy_slices(grpc_byte_buffer_reader *reader, char *destination,
                         size_t capacity) noexcept nogil:
  # Copies the reader's remaining slices into destination, back to back, and
  # returns their total length. Slices beyond capacity are counted but not
  # written, so a result greater than capacity means the buffer did not fit.
  cdef grpc_slice message_slice
  cdef size_t message_slice_length
  cdef size_t offset = 0
  while grpc_byte_buffer_reader_next(reader, &message_slice):
    message_slice_length = grpc_slice_length(message_slice)
    if message_slice_length > 0 and offset + message_slice_length <= capacity:
      memcpy(destination + offset, grpc_slice_start_ptr(message_slice),
             message_slice_length)
    offset += message_slice_length
    grpc_slice_unref(message_slice)
  return offset


cdef class Operation:

  cdef void c(self) except *:
    raise NotImplementedError()

  cdef void un_c(self) except *:
    raise NotImplementedError()


cdef class SendInitialMetadataOperation(Operation):

  def __cinit__(self, initial_metadata, flags):
    self._initial_metadata = initial_metadata
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_INITIAL_METADATA

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_SEND_INITIAL_METADATA
    self.c_op.flags = self._flags
    _store_c_metadata(
        self._initial_metadata, &self._c_initial_metadata,
        &self._c_initial_metadata_count)
    self.c_op.data.send_initial_metadata.metadata = self._c_initial_metadata
    self.c_op.data.send_initial_metadata.count = self._c_initial_metadata_count
    self.c_op.data.send_initial_metadata.maybe_compression_level.is_set = 0

  cdef void un_c(self) except *:
    _release_c_metadata(
        self._c_initial_metadata, self._c_initial_metadata_count)


cdef class SendMessageOperation(Operation):

  def __cinit__(self, bytes message, int flags):
    if message is None:
      self._message = b''
    else:
      self._message = message
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_MESSAGE

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_SEND_MESSAGE
    self.c_op.flags = self._flags
    cdef grpc_slice message_slice = grpc_slice_from_copied_buffer(
        self._message, len(self._message))
    self._c_message_byte_buffer = grpc_raw_byte_buffer_create(
        &message_slice, 1)
    grpc_slice_unref(message_slice)
    self.c_op.data.send_message.send_message = self._c_message_byte_buffer

  cdef void un_c(self) except *:
    grpc_byte_buffer_destroy(self._c_message_byte_buffer)


cdef class SendCloseFromClientOperation(Operation):

  def __cinit__(self, int flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_CLOSE_FROM_CLIENT

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_SEND_CLOSE_FROM_CLIENT
    self.c_op.flags = self._flags

  cdef void un_c(self) except *:
    pass


cdef class SendStatusFromServerOperation(Operation):

  def __cinit__(self, trailing_metadata, code, object details, int flags):
    self._trailing_metadata = trailing_metadata
    self._code = code
    self._details = details
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_STATUS_FROM_SERVER

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_SEND_STATUS_FROM_SERVER
    self.c_op.flags = self._flags
    _store_c_metadata(
        self._trailing_metadata, &self._c_trailing_metadata,
        &self._c_trailing_metadata_count)
    self.c_op.data.send_status_from_server.trailing_metadata = (
        self._c_trailing_metadata)
    self.c_op.data.send_status_from_server.trailing_metadata_count = (
        self._c_trailing_metadata_count)
    self.c_op.data.send_status_from_server.status = self._code
    self._c_details = _slice_from_bytes(_encode(self._details))
    self.c_op.data.send_status_from_server.status_details = &self._c_details

  cdef void un_c(self) except *:
    grpc_slice_unref(self._c_details)
    _release_c_metadata(
        self._c_trailing_metadata, self._c_trailing_metadata_count)


cdef class ReceiveInitialMetadataOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_INITIAL_METADATA

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_RECV_INITIAL_METADATA
    self.c_op.flags = self._flags
    grpc_metadata_array_init(&self._c_initial_metadata)
    self.c_op.data.receive_initial_metadata.receive_initial_metadata = (
        &self._c_initial_metadata)

  cdef void un_c(self) except *:
    self._initial_metadata = _metadata(&self._c_initial_metadata)
    grpc_metadata_array_destroy(&self._c_initial_metadata)

  def initial_metadata(self):
    return self._initial_metadata


cdef class ReceiveMessageOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_MESSAGE

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_RECV_MESSAGE
    self.c_op.flags = self._flags
    self.c_op.data.receive_message.receive_message = (
        &self._c_message_byte_buffer)

  cdef void un_c(self) except *:
    cdef grpc_byte_buffer_reader message_reader
    cdef size_t message_length
    cdef size_t copied_length = 0
    cdef bytes message
    cdef char *destination

    self._message = None
    if self._c_message_byte_buffer == NULL:
      return
    try:
      if not grpc_byte_buffer_reader_init(
          &message_reader, self._c_message_byte_buffer):
        return
      try:
        # Allocate the result once and copy every slice straight into it
        # instead of materializing a bytes object per slice and joining them
        # afterwards. The reader iterates buffer_out, so size the result from
        # that buffer, as grpc_byte_buffer_reader_readall does.
        message_length = grpc_byte_buffer_length(message_reader.buffer_out)
        message = PyBytes_FromStringAndSize(NULL, message_length)
        destination = PyBytes_AS_STRING(message)
        if message_length >= _NOGIL_COPY_MIN_BYTES:
          # The copy only touches C state and a bytes object that no other
          # thread can see yet, so it does not need the GIL.
          with nogil:
            copied_length = _copy_slices(
                &message_reader, destination, message_length)
        else:
          copied_length = _copy_slices(
              &message_reader, destination, message_length)
      finally:
        grpc_byte_buffer_reader_destroy(&message_reader)
      if copied_length == message_length:
        self._message = message
      else:
        _LOGGER.error(
            "Received message of %d bytes does not match its byte buffer "
            "length of %d bytes; dropping it.", copied_length, message_length)
    finally:
      grpc_byte_buffer_destroy(self._c_message_byte_buffer)

  def message(self):
    return self._message


cdef class ReceiveStatusOnClientOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_STATUS_ON_CLIENT

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_RECV_STATUS_ON_CLIENT
    self.c_op.flags = self._flags
    grpc_metadata_array_init(&self._c_trailing_metadata)
    self.c_op.data.receive_status_on_client.trailing_metadata = (
        &self._c_trailing_metadata)
    self.c_op.data.receive_status_on_client.status = (
        &self._c_code)
    self.c_op.data.receive_status_on_client.status_details = (
        &self._c_details)
    self.c_op.data.receive_status_on_client.error_string = (
        &self._c_error_string)

  cdef void un_c(self) except *:
    self._trailing_metadata = _metadata(&self._c_trailing_metadata)
    grpc_metadata_array_destroy(&self._c_trailing_metadata)
    self._code = self._c_code
    self._details = _decode(_slice_bytes(self._c_details))
    grpc_slice_unref(self._c_details)
    if self._c_error_string != NULL:
      self._error_string = _decode(self._c_error_string)
      gpr_free(<void*>self._c_error_string)
    else:
      self._error_string = ""

  def trailing_metadata(self):
    return self._trailing_metadata

  def code(self):
    return self._code

  def details(self):
    return self._details

  def error_string(self):
    return self._error_string


cdef class ReceiveCloseOnServerOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_CLOSE_ON_SERVER

  cdef void c(self) except *:
    self.c_op.type = GRPC_OP_RECV_CLOSE_ON_SERVER
    self.c_op.flags = self._flags
    self.c_op.data.receive_close_on_server.cancelled = &self._c_cancelled

  cdef void un_c(self) except *:
    self._cancelled = bool(self._c_cancelled)

  def cancelled(self):
    return self._cancelled
