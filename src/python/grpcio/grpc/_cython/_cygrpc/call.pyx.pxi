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


cdef class Call:

  def __cinit__(self):
    # Create an *empty* call
    fork_handlers_and_grpc_init()
    self.c_call = NULL
    self.references = []

  def _start_batch(self, operations, tag, retain_self):
    if not self.is_valid:
      raise ValueError("invalid call object cannot be used from Python")
    cdef _BatchOperationTag batch_operation_tag = _BatchOperationTag(
        tag, operations, self if retain_self else None)
    batch_operation_tag.prepare()
    cpython.Py_INCREF(batch_operation_tag)
    cdef grpc_call_error error
    with nogil:
      error = grpc_call_start_batch(
          self.c_call, batch_operation_tag.c_ops, batch_operation_tag.c_nops,
          <cpython.PyObject *>batch_operation_tag, NULL)
    return error

  def start_client_batch(self, operations, tag):
    # We don't reference this call in the operations tag because
    # it should be cancelled when it goes out of scope
    return self._start_batch(operations, tag, False)

  def start_server_batch(self, operations, tag):
    return self._start_batch(operations, tag, True)

  def cancel(
      self, grpc_status_code error_code=GRPC_STATUS__DO_NOT_USE,
      details=None):
    details = str_to_bytes(details)
    if not self.is_valid:
      raise ValueError("invalid call object cannot be used from Python")
    if (details is None) != (error_code == GRPC_STATUS__DO_NOT_USE):
      raise ValueError("if error_code is specified, so must details "
                       "(and vice-versa)")
    cdef grpc_call_error result
    cdef char *c_details = NULL
    if error_code != GRPC_STATUS__DO_NOT_USE:
      self.references.append(details)
      c_details = details
      with nogil:
        result = grpc_call_cancel_with_status(
            self.c_call, error_code, c_details, NULL)
      return result
    else:
      with nogil:
        result = grpc_call_cancel(self.c_call, NULL)
      return result

  def set_credentials(self, CallCredentials call_credentials not None):
    cdef grpc_call_credentials *c_call_credentials = call_credentials.c()
    cdef grpc_call_error call_error = grpc_call_set_credentials(
        self.c_call, c_call_credentials)
    grpc_call_credentials_release(c_call_credentials)
    return call_error

  def peer(self):
    cdef char *peer = NULL
    with nogil:
      peer = grpc_call_get_peer(self.c_call)
    result = <bytes>peer
    with nogil:
      gpr_free(peer)
    return result

  def __dealloc__(self):
    with nogil:
      if self.c_call != NULL:
        grpc_call_unref(self.c_call)
      grpc_shutdown()

  # The object *should* always be valid from Python. Used for debugging.
  @property
  def is_valid(self):
    return self.c_call != NULL

  def _custom_op_on_c_call(self, int op):
    return _custom_op_on_c_call(op, self.c_call)
