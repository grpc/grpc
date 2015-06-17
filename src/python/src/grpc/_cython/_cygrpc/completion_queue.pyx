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

from grpc._cython._cygrpc cimport call
from grpc._cython._cygrpc cimport records


cdef class CompletionQueue:

  def __cinit__(self):
    self.c_completion_queue = grpc.grpc_completion_queue_create()
    self.is_shutting_down = False
    self.is_shutdown = False

  def poll(self, records.Timespec deadline=None):
    # We name this 'poll' to avoid problems with CPython's expectations for
    # 'special' methods (like next and __next__).
    cdef grpc.gpr_timespec c_deadline = grpc.gpr_inf_future
    cdef records.OperationTag tag = None
    cdef object user_tag = None
    cdef call.Call operation_call = None
    cdef records.CallDetails request_call_details = None
    cdef records.Metadata request_metadata = None
    cdef records.Operations batch_operations = None
    if deadline is not None:
      c_deadline = deadline.c_time
    cdef grpc.grpc_event event
    with nogil:
      event = grpc.grpc_completion_queue_next(
          self.c_completion_queue, c_deadline)
    if event.type == grpc.GRPC_QUEUE_TIMEOUT:
      return records.Event(event.type, False, None, None, None, None, None)
    elif event.type == grpc.GRPC_QUEUE_SHUTDOWN:
      self.is_shutdown = True
      return records.Event(event.type, True, None, None, None, None, None)
    else:
      if event.tag != NULL:
        # TODO(atash): Does this work? Do I need to <object> *then*
        # <records.OperationTag> to ensure reference counts are handled
        # properly?
        tag = <records.OperationTag>event.tag
        # We receive event tags only after they've been inc-ref'd elsewhere in
        # the code.
        cpython.Py_DECREF(tag)
        if tag.shutting_down_server is not None:
          tag.shutting_down_server.notify_shutdown_complete()
        user_tag = tag.user_tag
        operation_call = tag.operation_call
        request_call_details = tag.request_call_details
        request_metadata = tag.request_metadata
        batch_operations = tag.batch_operations
      return records.Event(
          event.type, event.success, user_tag, operation_call,
          request_call_details, request_metadata, batch_operations)

  def shutdown(self):
    grpc.grpc_completion_queue_shutdown(self.c_completion_queue)
    self.is_shutting_down = True

  def __dealloc__(self):
    if self.c_completion_queue != NULL:
      # Ensure shutdown, pump the queue
      if not self.is_shutting_down:
        self.shutdown()
      while not self.is_shutdown:
        self.poll()
      grpc.grpc_completion_queue_destroy(self.c_completion_queue)
