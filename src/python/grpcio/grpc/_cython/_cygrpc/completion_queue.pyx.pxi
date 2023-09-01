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


g_interrupt_check_period_ms = 200

cdef grpc_event _next(grpc_completion_queue *c_completion_queue, deadline) except *:
  global g_interrupt_check_period_ms
  cdef gpr_timespec c_increment
  cdef gpr_timespec c_timeout
  cdef gpr_timespec c_deadline
  c_increment = gpr_time_from_millis(g_interrupt_check_period_ms, GPR_TIMESPAN)
  if deadline is None:
    c_deadline = gpr_inf_future(GPR_CLOCK_REALTIME)
  else:
    c_deadline = _timespec_from_time(deadline)

  while True:
    with nogil:
      c_timeout = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), c_increment)
      if gpr_time_cmp(c_timeout, c_deadline) > 0:
        c_timeout = c_deadline

      c_event = grpc_completion_queue_next(c_completion_queue, c_timeout, NULL)

      if (c_event.type != GRPC_QUEUE_TIMEOUT or
          gpr_time_cmp(c_timeout, c_deadline) == 0):
        break

    # Handle any signals
    cpython.PyErr_CheckSignals()
  return c_event

cdef _interpret_event(grpc_event c_event):
  cdef _Tag tag
  if c_event.type == GRPC_QUEUE_TIMEOUT:
    # TODO(ericgribkoff) Do not coopt ConnectivityEvent here.
    return None, ConnectivityEvent(GRPC_QUEUE_TIMEOUT, False, None)
  elif c_event.type == GRPC_QUEUE_SHUTDOWN:
    # NOTE(nathaniel): For now we coopt ConnectivityEvent here.
    return None, ConnectivityEvent(GRPC_QUEUE_SHUTDOWN, False, None)
  else:
    tag = <_Tag>c_event.tag
    # We receive event tags only after they've been inc-ref'd elsewhere in
    # the code.
    cpython.Py_DECREF(tag)
    return tag, tag.event(c_event)

cdef _internal_latent_event(_LatentEventArg latent_event_arg):
  cdef grpc_event c_event = _next(latent_event_arg.c_completion_queue, latent_event_arg.deadline)
  return _interpret_event(c_event)

cdef _latent_event(grpc_completion_queue *c_completion_queue, object deadline):
    global g_gevent_activated

    latent_event_arg = _LatentEventArg()
    latent_event_arg.c_completion_queue = c_completion_queue
    latent_event_arg.deadline = deadline

    if g_gevent_activated:
      # For gevent, completion_queue_next is run in a native thread pool.
      global g_gevent_threadpool

      result = g_gevent_threadpool.apply(_internal_latent_event, (latent_event_arg,))
      return result
    else:
      return _internal_latent_event(latent_event_arg)

cdef class CompletionQueue:

  def __cinit__(self, shutdown_cq=False):
    cdef grpc_completion_queue_attributes c_attrs
    fork_handlers_and_grpc_init()
    if shutdown_cq:
      c_attrs.version = 1
      c_attrs.cq_completion_type = GRPC_CQ_NEXT
      c_attrs.cq_polling_type = GRPC_CQ_NON_LISTENING
      c_attrs.cq_shutdown_cb = NULL
      self.c_completion_queue = grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&c_attrs), &c_attrs, NULL);
    else:
      self.c_completion_queue = grpc_completion_queue_create_for_next(NULL)
    self.is_shutting_down = False
    self.is_shutdown = False

  cdef _interpret_event(self, grpc_event c_event):
    unused_tag, event = _interpret_event(c_event)
    if event.completion_type == GRPC_QUEUE_SHUTDOWN:
      self.is_shutdown = True
    return event

  def _internal_poll(self, deadline):
    return self._interpret_event(_next(self.c_completion_queue, deadline))

  # We name this 'poll' to avoid problems with CPython's expectations for
  # 'special' methods (like next and __next__).
  def poll(self, deadline=None):
    global g_gevent_activated
    if g_gevent_activated:
      return g_gevent_threadpool.apply(CompletionQueue._internal_poll, (self, deadline))
    else:
      return self._internal_poll(deadline)

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
