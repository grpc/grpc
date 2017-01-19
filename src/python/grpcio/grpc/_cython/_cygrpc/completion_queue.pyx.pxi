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

import threading
import time

cdef int _INTERRUPT_CHECK_PERIOD_MS = 200


cdef class CompletionQueue:

  def __cinit__(self):
    grpc_init()
    with nogil:
      self.c_completion_queue = grpc_completion_queue_create(NULL)
    self.is_shutting_down = False
    self.is_shutdown = False

  cdef _interpret_event(self, grpc_event event):
    cdef OperationTag tag = None
    cdef object user_tag = None
    cdef Call operation_call = None
    cdef CallDetails request_call_details = None
    cdef Metadata request_metadata = None
    cdef Operations batch_operations = None
    cdef Operation batch_operation = None
    if event.type == GRPC_QUEUE_TIMEOUT:
      return Event(
          event.type, False, None, None, None, None, False, None)
    elif event.type == GRPC_QUEUE_SHUTDOWN:
      self.is_shutdown = True
      return Event(
          event.type, True, None, None, None, None, False, None)
    else:
      if event.tag != NULL:
        tag = <OperationTag>event.tag
        # We receive event tags only after they've been inc-ref'd elsewhere in
        # the code.
        cpython.Py_DECREF(tag)
        if tag.shutting_down_server is not None:
          tag.shutting_down_server.notify_shutdown_complete()
        user_tag = tag.user_tag
        operation_call = tag.operation_call
        request_call_details = tag.request_call_details
        if tag.request_metadata is not None:
          request_metadata = tag.request_metadata
          request_metadata._claim_slice_ownership()
        batch_operations = tag.batch_operations
        if tag.batch_operations is not None:
          for op in batch_operations.operations:
            batch_operation = <Operation>op
            if batch_operation._received_metadata is not None:
              batch_operation._received_metadata._claim_slice_ownership()
        if tag.is_new_request:
          # Stuff in the tag not explicitly handled by us needs to live through
          # the life of the call
          operation_call.references.extend(tag.references)
      return Event(
          event.type, event.success, user_tag, operation_call,
          request_call_details, request_metadata, tag.is_new_request,
          batch_operations)

  def poll(self, Timespec deadline=None):
    # We name this 'poll' to avoid problems with CPython's expectations for
    # 'special' methods (like next and __next__).
    cdef gpr_timespec c_increment
    cdef gpr_timespec c_timeout
    cdef gpr_timespec c_deadline
    with nogil:
      c_increment = gpr_time_from_millis(_INTERRUPT_CHECK_PERIOD_MS, GPR_TIMESPAN)
      c_deadline = gpr_inf_future(GPR_CLOCK_REALTIME)
      if deadline is not None:
        c_deadline = deadline.c_time

      while True:
        c_timeout = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), c_increment)
        if gpr_time_cmp(c_timeout, c_deadline) > 0:
          c_timeout = c_deadline
        event = grpc_completion_queue_next(
          self.c_completion_queue, c_timeout, NULL)
        if event.type != GRPC_QUEUE_TIMEOUT or gpr_time_cmp(c_timeout, c_deadline) == 0:
          break;

        # Handle any signals
        with gil:
          cpython.PyErr_CheckSignals()
    return self._interpret_event(event)

  def shutdown(self):
    with nogil:
      grpc_completion_queue_shutdown(self.c_completion_queue)
    self.is_shutting_down = True

  def clear(self):
    if not self.is_shutting_down:
      raise ValueError('queue must be shutting down to be cleared')
    while self.poll().type != GRPC_QUEUE_SHUTDOWN:
      pass

  def __dealloc__(self):
    cdef gpr_timespec c_deadline
    c_deadline = gpr_inf_future(GPR_CLOCK_REALTIME)
    if self.c_completion_queue != NULL:
      # Ensure shutdown
      if not self.is_shutting_down:
        grpc_completion_queue_shutdown(self.c_completion_queue)
      # Pump the queue (All outstanding calls should have been cancelled)
      while not self.is_shutdown:
        event = grpc_completion_queue_next(
            self.c_completion_queue, c_deadline, NULL)
        self._interpret_event(event)
      grpc_completion_queue_destroy(self.c_completion_queue)
    grpc_shutdown()
