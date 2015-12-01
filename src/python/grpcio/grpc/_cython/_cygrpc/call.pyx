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

from grpc._cython._cygrpc cimport credentials
from grpc._cython._cygrpc cimport grpc
from grpc._cython._cygrpc cimport records


cdef class Call:

  def __cinit__(self):
    # Create an *empty* call
    self.c_call = NULL
    self.references = []

  def start_batch(self, operations, tag):
    if not self.is_valid:
      raise ValueError("invalid call object cannot be used from Python")
    cdef records.Operations cy_operations = records.Operations(operations)
    cdef records.OperationTag operation_tag = records.OperationTag(tag)
    operation_tag.operation_call = self
    operation_tag.batch_operations = cy_operations
    cpython.Py_INCREF(operation_tag)
    return grpc.grpc_call_start_batch(
        self.c_call, cy_operations.c_ops, cy_operations.c_nops,
        <cpython.PyObject *>operation_tag, NULL)

  def cancel(
      self, grpc.grpc_status_code error_code=grpc.GRPC_STATUS__DO_NOT_USE,
      details=None):
    if not self.is_valid:
      raise ValueError("invalid call object cannot be used from Python")
    if (details is None) != (error_code == grpc.GRPC_STATUS__DO_NOT_USE):
      raise ValueError("if error_code is specified, so must details "
                       "(and vice-versa)")
    if error_code != grpc.GRPC_STATUS__DO_NOT_USE:
      if isinstance(details, bytes):
        pass
      elif isinstance(details, basestring):
        details = details.encode()
      else:
        raise TypeError("expected details to be str or bytes")
      self.references.append(details)
      return grpc.grpc_call_cancel_with_status(
          self.c_call, error_code, details, NULL)
    else:
      return grpc.grpc_call_cancel(self.c_call, NULL)

  def set_credentials(
      self, credentials.CallCredentials call_credentials not None):
    return grpc.grpc_call_set_credentials(
        self.c_call, call_credentials.c_credentials)

  def peer(self):
    cdef char *peer = grpc.grpc_call_get_peer(self.c_call)
    result = <bytes>peer
    grpc.gpr_free(peer)
    return result

  def __dealloc__(self):
    if self.c_call != NULL:
      grpc.grpc_call_destroy(self.c_call)

  # The object *should* always be valid from Python. Used for debugging.
  @property
  def is_valid(self):
    return self.c_call != NULL

