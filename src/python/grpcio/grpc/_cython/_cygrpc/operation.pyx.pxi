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


cdef class Operation:

  cdef void c(self):
    raise NotImplementedError()

  cdef void un_c(self):
    raise NotImplementedError()


cdef class SendInitialMetadataOperation(Operation):

  def __cinit__(self, initial_metadata, flags):
    self._initial_metadata = initial_metadata
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_INITIAL_METADATA

  cdef void c(self):
    self.c_op.type = GRPC_OP_SEND_INITIAL_METADATA
    self.c_op.flags = self._flags
    _store_c_metadata(
        self._initial_metadata, &self._c_initial_metadata,
        &self._c_initial_metadata_count)
    self.c_op.data.send_initial_metadata.metadata = self._c_initial_metadata
    self.c_op.data.send_initial_metadata.count = self._c_initial_metadata_count
    self.c_op.data.send_initial_metadata.maybe_compression_level.is_set = 0

  cdef void un_c(self):
    _release_c_metadata(
        self._c_initial_metadata, self._c_initial_metadata_count)


cdef class SendMessageOperation(Operation):

  def __cinit__(self, bytes message, int flags):
    self._message = message
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_MESSAGE

  cdef void c(self):
    self.c_op.type = GRPC_OP_SEND_MESSAGE
    self.c_op.flags = self._flags
    cdef grpc_slice message_slice = grpc_slice_from_copied_buffer(
        self._message, len(self._message))
    self._c_message_byte_buffer = grpc_raw_byte_buffer_create(
        &message_slice, 1)
    grpc_slice_unref(message_slice)
    self.c_op.data.send_message.send_message = self._c_message_byte_buffer

  cdef void un_c(self):
    grpc_byte_buffer_destroy(self._c_message_byte_buffer)


cdef class SendCloseFromClientOperation(Operation):

  def __cinit__(self, int flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_CLOSE_FROM_CLIENT

  cdef void c(self):
    self.c_op.type = GRPC_OP_SEND_CLOSE_FROM_CLIENT
    self.c_op.flags = self._flags

  cdef void un_c(self):
    pass


cdef class SendStatusFromServerOperation(Operation):

  def __cinit__(self, trailing_metadata, code, object details, int flags):
    self._trailing_metadata = trailing_metadata
    self._code = code
    self._details = details
    self._flags = flags

  def type(self):
    return GRPC_OP_SEND_STATUS_FROM_SERVER

  cdef void c(self):
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

  cdef void un_c(self):
    grpc_slice_unref(self._c_details)
    _release_c_metadata(
        self._c_trailing_metadata, self._c_trailing_metadata_count)


cdef class ReceiveInitialMetadataOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_INITIAL_METADATA

  cdef void c(self):
    self.c_op.type = GRPC_OP_RECV_INITIAL_METADATA
    self.c_op.flags = self._flags
    grpc_metadata_array_init(&self._c_initial_metadata)
    self.c_op.data.receive_initial_metadata.receive_initial_metadata = (
        &self._c_initial_metadata)

  cdef void un_c(self):
    self._initial_metadata = _metadata(&self._c_initial_metadata)
    grpc_metadata_array_destroy(&self._c_initial_metadata)

  def initial_metadata(self):
    return self._initial_metadata


cdef class ReceiveMessageOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_MESSAGE

  cdef void c(self):
    self.c_op.type = GRPC_OP_RECV_MESSAGE
    self.c_op.flags = self._flags
    self.c_op.data.receive_message.receive_message = (
        &self._c_message_byte_buffer)

  cdef void un_c(self):
    cdef grpc_byte_buffer_reader message_reader
    cdef bint message_reader_status
    cdef grpc_slice message_slice
    cdef size_t message_slice_length
    cdef void *message_slice_pointer
    if self._c_message_byte_buffer != NULL:
      message_reader_status = grpc_byte_buffer_reader_init(
          &message_reader, self._c_message_byte_buffer)
      if message_reader_status:
        message = bytearray()
        while grpc_byte_buffer_reader_next(&message_reader, &message_slice):
          message_slice_pointer = grpc_slice_start_ptr(message_slice)
          message_slice_length = grpc_slice_length(message_slice)
          message += (<char *>message_slice_pointer)[:message_slice_length]
          grpc_slice_unref(message_slice)
        grpc_byte_buffer_reader_destroy(&message_reader)
        self._message = bytes(message)
      else:
        self._message = None
      grpc_byte_buffer_destroy(self._c_message_byte_buffer)
    else:
      self._message = None

  def message(self):
    return self._message


cdef class ReceiveStatusOnClientOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_STATUS_ON_CLIENT

  cdef void c(self):
    self.c_op.type = GRPC_OP_RECV_STATUS_ON_CLIENT
    self.c_op.flags = self._flags
    grpc_metadata_array_init(&self._c_trailing_metadata)
    self.c_op.data.receive_status_on_client.trailing_metadata = (
        &self._c_trailing_metadata)
    self.c_op.data.receive_status_on_client.status = (
        &self._c_code)
    self.c_op.data.receive_status_on_client.status_details = (
        &self._c_details)

  cdef void un_c(self):
    self._trailing_metadata = _metadata(&self._c_trailing_metadata)
    grpc_metadata_array_destroy(&self._c_trailing_metadata)
    self._code = self._c_code
    self._details = _decode(_slice_bytes(self._c_details))
    grpc_slice_unref(self._c_details)

  def trailing_metadata(self):
    return self._trailing_metadata

  def code(self):
    return self._code

  def details(self):
    return self._details


cdef class ReceiveCloseOnServerOperation(Operation):

  def __cinit__(self, flags):
    self._flags = flags

  def type(self):
    return GRPC_OP_RECV_CLOSE_ON_SERVER

  cdef void c(self):
    self.c_op.type = GRPC_OP_RECV_CLOSE_ON_SERVER
    self.c_op.flags = self._flags
    self.c_op.data.receive_close_on_server.cancelled = &self._c_cancelled

  cdef void un_c(self):
    self._cancelled = bool(self._c_cancelled)

  def cancelled(self):
    return self._cancelled
