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

cimport cpython

import threading

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


cdef class _ChannelState:

  def __cinit__(self):
    self.condition = threading.Condition()
    self.open = True
    self.integrated_call_states = {}
    self.segregated_call_states = set()
    self.connectivity_due = set()


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


cdef BatchOperationEvent _next_call_event(
    _ChannelState channel_state, grpc_completion_queue *c_completion_queue,
    on_success):
  tag, event = _latent_event(c_completion_queue, None)
  with channel_state.condition:
    on_success(tag)
    channel_state.condition.notify_all()
  return event


# TODO(https://github.com/grpc/grpc/issues/14569): This could be a lot simpler.
cdef void _call(
    _ChannelState channel_state, _CallState call_state,
    grpc_completion_queue *c_completion_queue, on_success, int flags, method,
    host, object deadline, CallCredentials credentials,
    object operationses_and_user_tags, object metadata) except *:
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
      call_state.c_call = grpc_channel_create_call(
          channel_state.c_channel, NULL, flags,
          c_completion_queue, method_slice, host_slice_ptr,
          _timespec_from_time(deadline), NULL)
      grpc_slice_unref(method_slice)
      if host_slice_ptr:
        grpc_slice_unref(host_slice)
      if credentials is not None:
        c_call_credentials = credentials.c()
        c_call_error = grpc_call_set_credentials(
            call_state.c_call, c_call_credentials)
        grpc_call_credentials_release(c_call_credentials)
        if c_call_error != GRPC_CALL_OK:
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
          grpc_call_unref(call_state.c_call)
          call_state.c_call = NULL
          _raise_call_error(c_call_error, metadata)
      else:
        call_state.due.update(started_tags)
        on_success(started_tags)
    else:
      raise ValueError('Cannot invoke RPC on closed channel!')

cdef void _process_integrated_call_tag(
    _ChannelState state, _BatchOperationTag tag) except *:
  cdef _CallState call_state = state.integrated_call_states.pop(tag)
  call_state.due.remove(tag)
  if not call_state.due:
    grpc_call_unref(call_state.c_call)
    call_state.c_call = NULL


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
    object metadata, CallCredentials credentials, operationses_and_user_tags):
  call_state = _CallState()

  def on_success(started_tags):
    for started_tag in started_tags:
      state.integrated_call_states[started_tag] = call_state

  _call(
      state, call_state, state.c_call_completion_queue, on_success, flags,
      method, host, deadline, credentials, operationses_and_user_tags, metadata)

  return IntegratedCall(state, call_state)


cdef object _process_segregated_call_tag(
    _ChannelState state, _CallState call_state,
    grpc_completion_queue *c_completion_queue, _BatchOperationTag tag):
  call_state.due.remove(tag)
  if not call_state.due:
    grpc_call_unref(call_state.c_call)
    call_state.c_call = NULL
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
    return _next_call_event(
        self._channel_state, self._c_completion_queue, on_success)


cdef SegregatedCall _segregated_call(
    _ChannelState state, int flags, method, host, object deadline,
    object metadata, CallCredentials credentials, operationses_and_user_tags):
  cdef _CallState call_state = _CallState()
  cdef grpc_completion_queue *c_completion_queue = (
      grpc_completion_queue_create_for_next(NULL))
  cdef SegregatedCall segregated_call

  def on_success(started_tags):
    state.segregated_call_states.add(call_state)

  try:
    _call(
        state, call_state, c_completion_queue, on_success, flags, method, host,
        deadline, credentials, operationses_and_user_tags, metadata)
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
      raise ValueError('Cannot invoke RPC on closed channel!')
  completed_tag, event = _latent_event(
      state.c_connectivity_completion_queue, None)
  with state.condition:
    state.connectivity_due.remove(completed_tag)
    state.condition.notify_all()
  return event


cdef _close(_ChannelState state, grpc_status_code code, object details):
  cdef _CallState call_state
  encoded_details = _encode(details)
  with state.condition:
    if state.open:
      state.open = False
      for call_state in set(state.integrated_call_states.values()):
        grpc_call_cancel_with_status(
            call_state.c_call, code, encoded_details, NULL)
      for call_state in state.segregated_call_states:
        grpc_call_cancel_with_status(
            call_state.c_call, code, encoded_details, NULL)
      # TODO(https://github.com/grpc/grpc/issues/3064): Cancel connectivity
      # watching.

      while state.integrated_call_states:
        state.condition.wait()
      while state.segregated_call_states:
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


cdef class Channel:

  def __cinit__(
      self, bytes target, object arguments,
      ChannelCredentials channel_credentials):
    grpc_init()
    self._state = _ChannelState()
    self._vtable.copy = &_copy_pointer
    self._vtable.destroy = &_destroy_pointer
    self._vtable.cmp = &_compare_pointer
    cdef _ArgumentsProcessor arguments_processor = _ArgumentsProcessor(
        arguments)
    cdef grpc_channel_args *c_arguments = arguments_processor.c(&self._vtable)
    if channel_credentials is None:
      self._state.c_channel = grpc_insecure_channel_create(
          <char *>target, c_arguments, NULL)
    else:
      c_channel_credentials = channel_credentials.c()
      self._state.c_channel = grpc_secure_channel_create(
          c_channel_credentials, <char *>target, c_arguments, NULL)
      grpc_channel_credentials_release(c_channel_credentials)
    self._state.c_call_completion_queue = (
        grpc_completion_queue_create_for_next(NULL))
    self._state.c_connectivity_completion_queue = (
        grpc_completion_queue_create_for_next(NULL))

  def target(self):
    cdef char *c_target
    with self._state.condition:
      c_target = grpc_channel_get_target(self._state.c_channel)
      target = <bytes>c_target
      gpr_free(c_target)
      return target

  def integrated_call(
      self, int flags, method, host, object deadline, object metadata,
      CallCredentials credentials, operationses_and_tags):
    return _integrated_call(
        self._state, flags, method, host, deadline, metadata, credentials,
        operationses_and_tags)

  def next_call_event(self):
    def on_success(tag):
      _process_integrated_call_tag(self._state, tag)
    return _next_call_event(
        self._state, self._state.c_call_completion_queue, on_success)

  def segregated_call(
      self, int flags, method, host, object deadline, object metadata,
      CallCredentials credentials, operationses_and_tags):
    return _segregated_call(
        self._state, flags, method, host, deadline, metadata, credentials,
        operationses_and_tags)

  def check_connectivity_state(self, bint try_to_connect):
    with self._state.condition:
      return grpc_channel_check_connectivity_state(
          self._state.c_channel, try_to_connect)

  def watch_connectivity_state(
      self, grpc_connectivity_state last_observed_state, object deadline):
    return _watch_connectivity_state(self._state, last_observed_state, deadline)

  def close(self, code, details):
    _close(self._state, code, details)
