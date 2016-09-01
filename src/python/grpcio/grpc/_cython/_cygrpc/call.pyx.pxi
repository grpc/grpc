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

cimport cpython


cdef class Call:

  def __cinit__(self):
    # Create an *empty* call
    self.c_call = NULL
    self.references = []

  def _start_batch(self, operations, tag, retain_self):
    if not self.is_valid:
      raise ValueError("invalid call object cannot be used from Python")
    cdef grpc_call_error result
    cdef Operations cy_operations = Operations(operations)
    cdef OperationTag operation_tag = OperationTag(tag)
    if retain_self:
      operation_tag.operation_call = self
    else:
      operation_tag.operation_call = None
    operation_tag.batch_operations = cy_operations
    cpython.Py_INCREF(operation_tag)
    with nogil:
      result = grpc_call_start_batch(
          self.c_call, cy_operations.c_ops, cy_operations.c_nops,
          <cpython.PyObject *>operation_tag, NULL)
    return result

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

  def set_credentials(
      self, CallCredentials call_credentials not None):
    cdef grpc_call_error result
    with nogil:
      result = grpc_call_set_credentials(
          self.c_call, call_credentials.c_credentials)
    return result

  def peer(self):
    cdef char *peer = NULL
    with nogil:
      peer = grpc_call_get_peer(self.c_call)
    result = <bytes>peer
    with nogil:
      gpr_free(peer)
    return result

  def __dealloc__(self):
    if self.c_call != NULL:
      grpc_call_destroy(self.c_call)

  # The object *should* always be valid from Python. Used for debugging.
  @property
  def is_valid(self):
    return self.c_call != NULL

