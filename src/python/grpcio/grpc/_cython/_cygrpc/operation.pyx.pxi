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

from cpython.ref cimport Py_INCREF, Py_DECREF

cdef void py_decref_destroy(void* user_data) noexcept with gil:
    Py_DECREF(<object>user_data)


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

  def __cinit__(self, object message, int flags):
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
    
    cdef grpc_slice message_slice
    cdef object message_obj
    cdef const unsigned char[::1] view
    cdef bytes message_bytes

    if IsPythonMemoryviewEnabled():
      if isinstance(self._message, list):
        self._message = b''.join(self._message)

      view = self._message
      message_obj = self._message
      if view.shape[0] > 0:
        Py_INCREF(message_obj)
        message_slice = grpc_slice_new_with_user_data(
            <void*>&view[0], view.shape[0], py_decref_destroy, <void*>message_obj)
      else:
        message_slice = grpc_empty_slice()
    else:
      if isinstance(self._message, list):
        message_bytes = b''.join(self._message)
      elif isinstance(self._message, bytes):
        message_bytes = self._message
      else:
        message_bytes = bytes(self._message)
      
      message_slice = grpc_slice_from_copied_buffer(
          <const char *>message_bytes, len(message_bytes))
      
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
    cdef bint message_reader_status
    cdef grpc_slice message_slice
    cdef size_t message_slice_length
    cdef list chunks = []
    cdef grpc_slice first_slice
    cdef bint has_next
    cdef GrpcSliceView view

    if self._c_message_byte_buffer != NULL:
      message_reader_status = grpc_byte_buffer_reader_init(
          &message_reader, self._c_message_byte_buffer)
      if message_reader_status:
        if IsPythonMemoryviewEnabled():
          # Peek-ahead pattern: read the first slice, then check if there's a
          # second. Most gRPC messages fit in a single slice, so this avoids
          # list.append() / len() / indexing overhead on the hot path by
          # assigning the memoryview directly to self._message.
          has_next = grpc_byte_buffer_reader_next(&message_reader, &message_slice)
          if has_next:
            first_slice = message_slice
            has_next = grpc_byte_buffer_reader_next(&message_reader, &message_slice)
            if not has_next:
              message_slice_length = grpc_slice_length(first_slice)
              if message_slice_length > 0:
                view = GrpcSliceView()
                view.set_slice(first_slice)
                self._message = memoryview(view)
              else:
                self._message = b''
              grpc_slice_unref(first_slice)
            else:
              view = GrpcSliceView()
              view.set_slice(first_slice)
              chunks.append(memoryview(view))
              grpc_slice_unref(first_slice)
              
              view = GrpcSliceView()
              view.set_slice(message_slice)
              chunks.append(memoryview(view))
              grpc_slice_unref(message_slice)
              
              while grpc_byte_buffer_reader_next(&message_reader, &message_slice):
                view = GrpcSliceView()
                view.set_slice(message_slice)
                chunks.append(memoryview(view))
                grpc_slice_unref(message_slice)
              
              self._message = chunks
          else:
            self._message = b''
        else:
          while grpc_byte_buffer_reader_next(&message_reader, &message_slice):
            message_slice_length = grpc_slice_length(message_slice)
            if message_slice_length > 0:
              chunks.append((<char *>grpc_slice_start_ptr(message_slice))[:message_slice_length])
            grpc_slice_unref(message_slice)
          self._message = b"".join(chunks)
        
        grpc_byte_buffer_reader_destroy(&message_reader)
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


def python_memoryview_enabled():
  return IsPythonMemoryviewEnabled()
