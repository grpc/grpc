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


cdef class _Tag:

  cdef BaseEvent event(self, grpc_event c_event):
    raise NotImplementedError()


cdef class _ConnectivityTag(_Tag):

  def __cinit__(self, user_tag):
    self._user_tag = user_tag

  cdef ConnectivityEvent event(self, grpc_event c_event):
    return ConnectivityEvent(c_event.type, c_event.success, self._user_tag)


cdef class _RequestCallTag(_Tag):

  def __cinit__(self, user_tag):
    self._user_tag = user_tag
    self.call = None
    self.call_details = None

  cdef void prepare(self) except *:
    self.call = Call()
    self.call_details = CallDetails()
    grpc_metadata_array_init(&self.c_invocation_metadata)

  cdef RequestCallEvent event(self, grpc_event c_event):
    cdef tuple invocation_metadata = _metadata(&self.c_invocation_metadata)
    grpc_metadata_array_destroy(&self.c_invocation_metadata)
    return RequestCallEvent(
        c_event.type, c_event.success, self._user_tag, self.call,
        self.call_details, invocation_metadata)


# NOTE(zgoda): `release()` reads `self._operations` in a `__dealloc__` method.
# We need to disable tp_clear phase of garbage collection to make that work.
@cython.no_gc_clear
cdef class _BatchOperationTag:

  def __cinit__(self, user_tag, operations, call):
    self._user_tag = user_tag
    self._operations = operations
    self._retained_call = call
    self.c_ops = NULL
    self.c_prepared = 0

  def __dealloc__(self):
    # A batch that never reached `event()` will leak memory if we don't release
    # the operations here.
    self.release()

  cdef void prepare(self) except *:
    cdef Operation operation
    self.c_nops = 0 if self._operations is None else len(self._operations)
    if 0 < self.c_nops:
      self.c_ops = <grpc_op *>gpr_malloc(sizeof(grpc_op) * self.c_nops)
      for index, operation in enumerate(self._operations):
        operation.c()
        self.c_ops[index] = operation.c_op
        self.c_prepared += 1

  cdef void release(self) except *:
    cdef grpc_op *c_ops = self.c_ops
    cdef size_t prepared = self.c_prepared
    cdef size_t index
    if c_ops == NULL:
      return
    # Detach first to prevent double-free bugs in `__dealloc__` if `un_c()`
    # raises an exception.
    self.c_ops = NULL
    self.c_prepared = 0
    try:
      for index in range(prepared):
        (<Operation>self._operations[index]).un_c()
    finally:
      gpr_free(c_ops)


  cdef BatchOperationEvent event(self, grpc_event c_event):
    self.release()
    if 0 < self.c_nops:
      return BatchOperationEvent(
          c_event.type, c_event.success, self._user_tag, self._operations)
    else:
      return BatchOperationEvent(
          c_event.type, c_event.success, self._user_tag, ())


cdef class _ServerShutdownTag(_Tag):

  def __cinit__(self, user_tag, shutting_down_server):
    self._user_tag = user_tag
    self._shutting_down_server = shutting_down_server

  cdef ServerShutdownEvent event(self, grpc_event c_event):
    self._shutting_down_server.notify_shutdown_complete()
    return ServerShutdownEvent(c_event.type, c_event.success, self._user_tag)
