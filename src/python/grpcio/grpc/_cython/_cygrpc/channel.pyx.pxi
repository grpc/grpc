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

from grpc import _observability

_INTERNAL_CALL_ERROR_MESSAGE_FORMAT = (
    'Internal gRPC call error %d. ' +
    'Please report to https://github.com/grpc/grpc/issues')


cdef str _call_error_metadata(metadata):
  return 'metadata was invalid: %s' % metadata


cdef str _call_error_no_metadata(c_call_error):
  return _INTERNAL_CALL_ERROR_MESSAGE_FORMAT % c_call_error


cdef str _call_error(c_call_error, metadata):
  if c_call_error == GRPC_CALL_ERROR_INVALID_METADATA:
    return _call_error_metadata(metadata)
  else:
    return _call_error_no_metadata(c_call_error)


cdef _check_call_error_no_metadata(c_call_error):
  if c_call_error != GRPC_CALL_OK:
    return _INTERNAL_CALL_ERROR_MESSAGE_FORMAT % c_call_error
  else:
    return None


cdef _check_and_raise_call_error_no_metadata(c_call_error):
  error = _check_call_error_no_metadata(c_call_error)
  if error is not None:
    raise ValueError(error)


cdef _check_call_error(c_call_error, metadata):
  if c_call_error == GRPC_CALL_ERROR_INVALID_METADATA:
    return _call_error_metadata(metadata)
  else:
    return _check_call_error_no_metadata(c_call_error)


cdef void _raise_call_error_no_metadata(c_call_error) except *:
  raise ValueError(_call_error_no_metadata(c_call_error))


cdef void _raise_call_error(c_call_error, metadata) except *:
  raise ValueError(_call_error(c_call_error, metadata))


cdef _destroy_c_completion_queue(grpc_completion_queue *c_completion_queue):
  grpc_completion_queue_shutdown(c_completion_queue)
  grpc_completion_queue_destroy(c_completion_queue)


cdef class _CallState:

  def __cinit__(self):
    self.due = set()

  cdef void maybe_delete_call_tracer(self) except *:
    if not self.call_tracer_capsule:
      return
    _observability.delete_call_tracer(self.call_tracer_capsule)

  cdef void maybe_set_client_call_tracer_on_call(self, bytes method_name, bytes target) except *:
    # TODO(xuanwn): use channel args to exclude those metrics.
    for exclude_prefix in _observability._SERVICES_TO_EXCLUDE:
      if exclude_prefix in method_name:
        return
    with _observability.get_plugin() as plugin:
      if not (plugin and plugin.observability_enabled):
        return
      capsule = plugin.create_client_call_tracer(method_name, target)
      capsule_ptr = cpython.PyCapsule_GetPointer(capsule, CLIENT_CALL_TRACER)
      _set_call_tracer(self.c_call, capsule_ptr)
      self.call_tracer_capsule = capsule

cdef class _ChannelState:

  def __cinit__(self, target):
    self.target = target
    self.condition = threading.Condition()
    self.open = True
    self.integrated_call_states = {}
    self.segregated_call_states = set()
    self.connectivity_due = set()
    self.closed_reason = None

cdef class CallHandle:

  def __cinit__(self, _ChannelState channel_state, object method):
    self.method = method
    cpython.Py_INCREF(method)
    # Note that since we always pass None for host, we set the
    # second-to-last parameter of grpc_channel_register_call to a fixed
    # NULL value.
    self.c_call_handle = grpc_channel_register_call(
      channel_state.c_channel, <const char *>method, NULL, NULL)

  def __dealloc__(self):
    cpython.Py_DECREF(self.method)

  @property
  def call_handle(self):
    return cpython.PyLong_FromVoidPtr(self.c_call_handle)



cdef tuple _operate(grpc_call *c_call, object operations, object user_tag):
  cdef grpc_call_error c_call_error
  cdef _BatchOperationTag tag = _BatchOperationTag(user_tag, operations, None)
  tag.prepare()
  cpython.Py_INCREF(tag)
  with nogil:
    c_call_error = grpc_call_start_batch(
        c_call, tag.c_ops, tag.c_nops, <cpython.PyObject *>tag, NULL)
  return c_call_error, tag


cdef object _operate_from_integrated_call(
    _ChannelState channel_state, _CallState call_state, object operations,
    object user_tag):
  cdef grpc_call_error c_call_error
  cdef _BatchOperationTag tag
  with channel_state.condition:
    if call_state.due:
      c_call_error, tag = _operate(call_state.c_call, operations, user_tag)
      if c_call_error == GRPC_CALL_OK:
        call_state.due.add(tag)
        channel_state.integrated_call_states[tag] = call_state
        return True
      else:
        _raise_call_error_no_metadata(c_call_error)
    else:
      return False


cdef object _operate_from_segregated_call(
    _ChannelState channel_state, _CallState call_state, object operations,
    object user_tag):
  cdef grpc_call_error c_call_error
  cdef _BatchOperationTag tag
  with channel_state.condition:
    if call_state.due:
      c_call_error, tag = _operate(call_state.c_call, operations, user_tag)
      if c_call_error == GRPC_CALL_OK:
        call_state.due.add(tag)
        return True
      else:
        _raise_call_error_no_metadata(c_call_error)
    else:
      return False


cdef _cancel(
    _ChannelState channel_state, _CallState call_state, grpc_status_code code,
    str details):
  cdef grpc_call_error c_call_error
  with channel_state.condition:
    if call_state.due:
      c_call_error = grpc_call_cancel_with_status(
          call_state.c_call, code, _encode(details), NULL)
      _check_and_raise_call_error_no_metadata(c_call_error)


cdef _next_call_event(
    _ChannelState channel_state, grpc_completion_queue *c_completion_queue,
    on_success, on_failure, deadline):
  """Block on the next event out of the completion queue.

  On success, `on_success` will be invoked with the tag taken from the CQ.
  In the case of a failure due to an exception raised in a signal handler,
  `on_failure` will be invoked with no arguments. Note that this situation
  can only occur on the main thread.

  Args:
    channel_state: The state for the channel on which the RPC is running.
    c_completion_queue: The CQ which will be polled.
    on_success: A callable object to be invoked upon successful receipt of a
      tag from the CQ.
    on_failure: A callable object to be invoked in case a Python exception is
      raised from a signal handler during polling.
    deadline: The point after which the RPC will time out.
  """
  try:
    tag, event = _latent_event(c_completion_queue, deadline)
  # NOTE(rbellevi): This broad except enables us to clean up resources before
  # propagating any exceptions raised by signal handlers to the application.
  except:
    if on_failure is not None:
      on_failure()
    raise
  else:
    with channel_state.condition:
      on_success(tag)
      channel_state.condition.notify_all()
    return event


# TODO(https://github.com/grpc/grpc/issues/14569): This could be a lot simpler.
cdef void _call(
    _ChannelState channel_state, _CallState call_state,
    grpc_completion_queue *c_completion_queue, on_success, int flags, method,
    host, object deadline, CallCredentials credentials,
    object operationses_and_user_tags, object metadata,
    object context, object registered_call_handle) except *:
  """Invokes an RPC.

  Args:
    channel_state: A _ChannelState with its "open" attribute set to True. RPCs
      may not be invoked on a closed channel.
    call_state: An empty _CallState to be altered (specifically assigned a
      c_call and having its due set populated) if the RPC invocation is
      successful.
    c_completion_queue: A grpc_completion_queue to be used for the call's
      operations.
    on_success: A behavior to be called if attempting to start operations for
      the call succeeds. If called the behavior will be called while holding the
      channel_state condition and passed the tags associated with operations
      that were successfully started for the call.
    flags: Flags to be passed to gRPC Core as part of call creation.
    method: The fully-qualified name of the RPC method being invoked.
    host: A "host" string to be passed to gRPC Core as part of call creation.
    deadline: A float for the deadline of the RPC, or None if the RPC is to have
      no deadline.
    credentials: A _CallCredentials for the RPC or None.
    operationses_and_user_tags: A sequence of length-two sequences the first
      element of which is a sequence of Operations and the second element of
      which is an object to be used as a tag. A SendInitialMetadataOperation
      must be present in the first element of this value.
    metadata: The metadata for this call.
    context: Context object for distributed tracing.
    registered_call_handle: An int representing the call handle of the method, or
      None if the method is not registered.
  """
  cdef grpc_slice method_slice
  cdef grpc_slice host_slice
  cdef grpc_slice *host_slice_ptr
  cdef grpc_call_credentials *c_call_credentials
  cdef grpc_call_error c_call_error
  cdef tuple error_and_wrapper_tag
  cdef _BatchOperationTag wrapper_tag
  with channel_state.condition:
    if channel_state.open:
      method_slice = _slice_from_bytes(method)
      if host is None:
        host_slice_ptr = NULL
      else:
        host_slice = _slice_from_bytes(host)
        host_slice_ptr = &host_slice
      if registered_call_handle:
        call_state.c_call = grpc_channel_create_registered_call(
            channel_state.c_channel, NULL, flags,
            c_completion_queue, cpython.PyLong_AsVoidPtr(registered_call_handle),
            _timespec_from_time(deadline), NULL)
      else:
        call_state.c_call = grpc_channel_create_call(
            channel_state.c_channel, NULL, flags,
            c_completion_queue, method_slice, host_slice_ptr,
            _timespec_from_time(deadline), NULL)
      grpc_slice_unref(method_slice)
      if host_slice_ptr:
        grpc_slice_unref(host_slice)
      call_state.maybe_set_client_call_tracer_on_call(method, channel_state.target)
      if context is not None:
        set_census_context_on_call(call_state, context)
      if credentials is not None:
        c_call_credentials = credentials.c()
        c_call_error = grpc_call_set_credentials(
            call_state.c_call, c_call_credentials)
        grpc_call_credentials_release(c_call_credentials)
        if c_call_error != GRPC_CALL_OK:
          #TODO(xuanwn): Expand the scope of nogil
          with nogil:
            grpc_call_unref(call_state.c_call)
          call_state.c_call = NULL
          _raise_call_error_no_metadata(c_call_error)
      started_tags = set()
      for operations, user_tag in operationses_and_user_tags:
        c_call_error, tag = _operate(call_state.c_call, operations, user_tag)
        if c_call_error == GRPC_CALL_OK:
          started_tags.add(tag)
        else:
          grpc_call_cancel(call_state.c_call, NULL)
          #TODO(xuanwn): Expand the scope of nogil
          with nogil:
            grpc_call_unref(call_state.c_call)
          call_state.c_call = NULL
          _raise_call_error(c_call_error, metadata)
      else:
        call_state.due.update(started_tags)
        on_success(started_tags)
    else:
      raise ValueError('Cannot invoke RPC: %s' % channel_state.closed_reason)


cdef void _process_integrated_call_tag(
    _ChannelState state, _BatchOperationTag tag) except *:
  cdef _CallState call_state = state.integrated_call_states.pop(tag)
  call_state.due.remove(tag)
  if not call_state.due:
    with nogil:
      grpc_call_unref(call_state.c_call)
    call_state.c_call = NULL
    call_state.maybe_delete_call_tracer()

cdef class IntegratedCall:

  def __cinit__(self, _ChannelState channel_state, _CallState call_state):
    self._channel_state = channel_state
    self._call_state = call_state

  def operate(self, operations, tag):
    return _operate_from_integrated_call(
        self._channel_state, self._call_state, operations, tag)

  def cancel(self, code, details):
    _cancel(self._channel_state, self._call_state, code, details)


cdef IntegratedCall _integrated_call(
    _ChannelState state, int flags, method, host, object deadline,
    object metadata, CallCredentials credentials, operationses_and_user_tags,
    object context, object registered_call_handle):
  call_state = _CallState()

  def on_success(started_tags):
    for started_tag in started_tags:
      state.integrated_call_states[started_tag] = call_state

  _call(
      state, call_state, state.c_call_completion_queue, on_success, flags,
      method, host, deadline, credentials, operationses_and_user_tags,
      metadata, context, registered_call_handle)

  return IntegratedCall(state, call_state)


cdef object _process_segregated_call_tag(
    _ChannelState state, _CallState call_state,
    grpc_completion_queue *c_completion_queue, _BatchOperationTag tag):
  call_state.due.remove(tag)
  if not call_state.due:
    #TODO(xuanwn): Expand the scope of nogil
    with nogil:
      grpc_call_unref(call_state.c_call)
    call_state.c_call = NULL
    call_state.maybe_delete_call_tracer()
    state.segregated_call_states.remove(call_state)
    _destroy_c_completion_queue(c_completion_queue)
    return True
  else:
    return False


cdef class SegregatedCall:

  def __cinit__(self, _ChannelState channel_state, _CallState call_state):
    self._channel_state = channel_state
    self._call_state = call_state

  def operate(self, operations, tag):
    return _operate_from_segregated_call(
        self._channel_state, self._call_state, operations, tag)

  def cancel(self, code, details):
    _cancel(self._channel_state, self._call_state, code, details)

  def next_event(self):
    def on_success(tag):
      _process_segregated_call_tag(
        self._channel_state, self._call_state, self._c_completion_queue, tag)
    def on_failure():
      self._call_state.due.clear()
      with nogil:
        grpc_call_unref(self._call_state.c_call)
      self._call_state.c_call = NULL
      self._channel_state.segregated_call_states.remove(self._call_state)
      _destroy_c_completion_queue(self._c_completion_queue)
    return _next_call_event(
        self._channel_state, self._c_completion_queue, on_success, on_failure, None)


cdef SegregatedCall _segregated_call(
    _ChannelState state, int flags, method, host, object deadline,
    object metadata, CallCredentials credentials, operationses_and_user_tags,
    object context, object registered_call_handle):
  cdef _CallState call_state = _CallState()
  cdef SegregatedCall segregated_call
  cdef grpc_completion_queue *c_completion_queue

  def on_success(started_tags):
    state.segregated_call_states.add(call_state)

  with state.condition:
    if state.open:
      c_completion_queue = (grpc_completion_queue_create_for_next(NULL))
    else:
      raise ValueError('Cannot invoke RPC on closed channel!')

  try:
    _call(
        state, call_state, c_completion_queue, on_success, flags, method, host,
        deadline, credentials, operationses_and_user_tags, metadata,
        context, registered_call_handle)
  except:
    _destroy_c_completion_queue(c_completion_queue)
    raise

  segregated_call = SegregatedCall(state, call_state)
  segregated_call._c_completion_queue = c_completion_queue
  return segregated_call


cdef object _watch_connectivity_state(
    _ChannelState state, grpc_connectivity_state last_observed_state,
    object deadline):
  cdef _ConnectivityTag tag = _ConnectivityTag(object())
  with state.condition:
    if state.open:
      cpython.Py_INCREF(tag)
      grpc_channel_watch_connectivity_state(
          state.c_channel, last_observed_state, _timespec_from_time(deadline),
          state.c_connectivity_completion_queue, <cpython.PyObject *>tag)
      state.connectivity_due.add(tag)
    else:
      raise ValueError('Cannot monitor channel state: %s' % state.closed_reason)
  completed_tag, event = _latent_event(
      state.c_connectivity_completion_queue, None)
  with state.condition:
    state.connectivity_due.remove(completed_tag)
    state.condition.notify_all()
  return event


cdef _close(Channel channel, grpc_status_code code, object details,
    drain_calls):
  cdef _ChannelState state = channel._state
  cdef _CallState call_state
  encoded_details = _encode(details)
  with state.condition:
    if state.open:
      state.open = False
      state.closed_reason = details
      for call_state in set(state.integrated_call_states.values()):
        grpc_call_cancel_with_status(
            call_state.c_call, code, encoded_details, NULL)
      for call_state in state.segregated_call_states:
        grpc_call_cancel_with_status(
            call_state.c_call, code, encoded_details, NULL)
      # TODO(https://github.com/grpc/grpc/issues/3064): Cancel connectivity
      # watching.

      if drain_calls:
        while not _calls_drained(state):
          event = channel.next_call_event()
          if event.completion_type == CompletionType.queue_timeout:
              continue  
          event.tag(event)
      else:
        while state.integrated_call_states:
          state.condition.wait()
        while state.connectivity_due:
          state.condition.wait()

      _destroy_c_completion_queue(state.c_call_completion_queue)
      _destroy_c_completion_queue(state.c_connectivity_completion_queue)
      grpc_channel_destroy(state.c_channel)
      state.c_channel = NULL
      grpc_shutdown()
      state.condition.notify_all()
    else:
      # Another call to close already completed in the past or is currently
      # being executed in another thread.
      while state.c_channel != NULL:
        state.condition.wait()


cdef _calls_drained(_ChannelState state):
  return not (state.integrated_call_states or state.segregated_call_states or
              state.connectivity_due)

cdef class Channel:

  def __cinit__(
      self, bytes target, object arguments,
      ChannelCredentials channel_credentials):
    arguments = () if arguments is None else tuple(arguments)
    fork_handlers_and_grpc_init()
    self._state = _ChannelState(target)
    self._state.c_call_completion_queue = (
        grpc_completion_queue_create_for_next(NULL))
    self._state.c_connectivity_completion_queue = (
        grpc_completion_queue_create_for_next(NULL))
    self._arguments = arguments
    cdef _ChannelArgs channel_args = _ChannelArgs(arguments)
    c_channel_credentials = (
        channel_credentials.c() if channel_credentials is not None
        else grpc_insecure_credentials_create())
    self._state.c_channel = grpc_channel_create(
        <char *>target, c_channel_credentials, channel_args.c_args())
    self._registered_call_handles = {}
    grpc_channel_credentials_release(c_channel_credentials)

  def target(self):
    cdef char *c_target
    with self._state.condition:
      c_target = grpc_channel_get_target(self._state.c_channel)
      target = <bytes>c_target
      gpr_free(c_target)
      return target

  def integrated_call(
      self, int flags, method, host, object deadline, object metadata,
      CallCredentials credentials, operationses_and_tags,
      object context = None, object registered_call_handle = None):
    return _integrated_call(
        self._state, flags, method, host, deadline, metadata, credentials,
        operationses_and_tags, context, registered_call_handle)

  def next_call_event(self):
    def on_success(tag):
      if tag is not None:
        _process_integrated_call_tag(self._state, tag)
    if is_fork_support_enabled():
      queue_deadline = time.time() + 1.0
    else:
      queue_deadline = None
    # NOTE(gnossen): It is acceptable for on_failure to be None here because
    # failure conditions can only ever happen on the main thread and this
    # method is only ever invoked on the channel spin thread.
    return _next_call_event(self._state, self._state.c_call_completion_queue,
                            on_success, None, queue_deadline)

  def segregated_call(
      self, int flags, method, host, object deadline, object metadata,
      CallCredentials credentials, operationses_and_tags,
      object context = None, object registered_call_handle = None):
    return _segregated_call(
        self._state, flags, method, host, deadline, metadata, credentials,
        operationses_and_tags, context, registered_call_handle)

  def check_connectivity_state(self, bint try_to_connect):
    with self._state.condition:
      if self._state.open:
        return grpc_channel_check_connectivity_state(
            self._state.c_channel, try_to_connect)
      else:
        raise ValueError('Cannot invoke RPC: %s' % self._state.closed_reason)

  def watch_connectivity_state(
      self, grpc_connectivity_state last_observed_state, object deadline):
    return _watch_connectivity_state(self._state, last_observed_state, deadline)

  def close(self, code, details):
    _close(self, code, details, False)

  def close_on_fork(self, code, details):
    _close(self, code, details, True)

  def get_registered_call_handle(self, method):
    """
    Get or registers a call handler for a method.

    This method is not thread-safe.

    Args:
      method: Required, the method name for the RPC.

    Returns:
      The registered call handle pointer in the form of a Python Long. 
    """
    if method not in self._registered_call_handles.keys():
      self._registered_call_handles[method] = CallHandle(self._state, method)
    return self._registered_call_handles[method].call_handle
