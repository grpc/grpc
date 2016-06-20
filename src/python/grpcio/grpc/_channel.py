# Copyright 2016, Google Inc.
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

"""Invocation-side implementation of gRPC Python."""

import threading
import time

import grpc
from grpc import _common
from grpc import _grpcio_metadata
from grpc._cython import cygrpc
from grpc.framework.foundation import callable_util

_USER_AGENT = 'Python-gRPC-{}'.format(_grpcio_metadata.__version__)

_EMPTY_FLAGS = 0
_INFINITE_FUTURE = cygrpc.Timespec(float('+inf'))
_EMPTY_METADATA = cygrpc.Metadata(())

_CHANNEL_SUBSCRIPTION_CALLBACK_ERROR_LOG_MESSAGE = (
    'Exception calling channel subscription callback!')


def _deadline(timeout):
  if timeout is None:
    return None, _INFINITE_FUTURE
  else:
    deadline = time.time() + timeout
    return deadline, cygrpc.Timespec(deadline)


class _RPCState(object):

  def __init__(self, rpc_call, completion_queue):
    self.rpc_call = rpc_call
    self.completion_queue = completion_queue
    self.initial_metadata = None
    self.response = None
    self.trailing_metadata = None
    self.code = None
    self.details = None
    self.initial_metadata_event = threading.Event()
    self.status_event = threading.Event()
    self.callbacks = []
    self.callback_lock = threading.Lock()
    self.write_event = threading.Event()
    self.deserialize_error = False


def _validate(state):
  if state.code is not None and state.code != grpc.StatusCode.OK:
    raise grpc.RpcError(state.code, state.details)


def _abort(state, code, details):
  state.rpc_call.cancel(
      _common.STATUS_CODE_TO_CYGRPC_STATUS_CODE.get(code), details)


def _finalize(call, state):
  if state.deserialize_error:
    state.code = grpc.StatusCode.INTERNAL
    state.details = b'Exception deserializing response!'
  state.write_event.set()
  state.initial_metadata_event.set()
  state.status_event.set()
  with state.callback_lock:
    for callback in state.callbacks:
      callable_util.call_logging_exceptions(
            callback, 'Exception calling callback!', call)
    state.callbacks = None


def _handle_event(call):
  """Drives the completion queue for a call.

  This should only be called by a single thread per call.
  """
  state = call._state
  response_deserializer = call._response_deserializer
  event = state.completion_queue.poll()
  for batch_operation in event.batch_operations:
    operation_type = batch_operation.type
    if operation_type == cygrpc.OperationType.receive_initial_metadata:
      state.initial_metadata = batch_operation.received_metadata
      state.initial_metadata_event.set()
    elif operation_type == cygrpc.OperationType.receive_message:
      state.got_message = True
      serialized_response = batch_operation.received_message.bytes()
      if serialized_response is not None:
        response = _common.deserialize(
            serialized_response, response_deserializer)
        if response is None:
          _abort(state, grpc.StatusCode.INTERNAL,
                 b'Exception deserializing response!')
          state.deserialize_error = True
        else:
          state.response = response
    elif operation_type == cygrpc.OperationType.send_message:
      state.write_failure = not event.success
      state.write_event.set()
    elif operation_type == cygrpc.OperationType.receive_status_on_client:
      state.trailing_metadata = batch_operation.received_metadata
      state.code = _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE.get(
          batch_operation.received_status_code)
      state.details = batch_operation.received_status_details

  if state.code is not None:
    _finalize(call, state)


def _run_request_consumer_thread(call, request_iterator, request_serializer):
  state = call._state
  write_thread_joined = False
  def consume_request_iterator():
    for request in request_iterator:
      state.write_event.wait()
      if not write_thread_joined and state.code is not None:
        return
      serialized_request = _common.serialize(request, request_serializer)
      if serialized_request is None:
        _abort(state, grpc.StatusCode.INTERNAL,
               b'Exception serializing request!')
        return
      operations = (
          cygrpc.operation_send_message(serialized_request, _EMPTY_FLAGS),)
      state.write_event.clear()
      state.rpc_call.start_batch(operations, None)

    if state.code is None:
      operations = (cygrpc.operation_send_close_from_client(_EMPTY_FLAGS),)
      state.rpc_call.start_batch(operations, None)

  def cleanup(timeout):
    write_thread_joined = True
    state.write_event.set()

  state.write_event.set()
  thread = _common.CleanupThread(cleanup, target=consume_request_iterator)
  thread.start()


class _Call(grpc.Call):

  def __init__(self, state, response_deserializer, deadline):
    self._state = state
    self._response_deserializer = response_deserializer
    self._deadline = deadline

  def is_active(self):
    return self._state.code is None

  def time_remaining(self):
    if self._deadline is None:
      return None
    else:
      return max(self._deadline - time.time(), 0)

  def cancel(self):
    if self._state.code is None:
      _abort(self._state, grpc.StatusCode.CANCELLED, 'Cancelled!')

  def add_done_callback(self, callback):
    with self._state.callback_lock:
      if self.is_active():
        self._state.callbacks.append(callback)
        return
    callable_util.call_logging_exceptions(
            callback, 'Exception calling callback!', self)

  def initial_metadata(self):
    self._state.initial_metadata_event.wait()
    return self._state.initial_metadata

  def trailing_metadata(self):
    self._state.status_event.wait()
    return self._state.trailing_metadata

  def code(self):
    self._state.status_event.wait()
    return self._state.code

  def details(self):
    self._state.status_event.wait()
    return self._state.details

  def __str__(self):
    if self._state.code is None:
      return '<In-Flight RPC at {}>'.format(id(self))
    else:
      return '<Terminated RPC ({}, {}) at {}>'.format(
          self._state.code, self._state.details, id(self))

  def __del__(self):
    _abort(self._state, grpc.StatusCode.CANCELLED, 'Cancelled!')
    while self._state.code is None:
      _handle_event(self)


class UnaryCall(_Call, grpc.UnaryCall):

  def response(self, timeout=None):
    if not self._state.status_event.wait(timeout):
      raise grpc.RpcTimeoutError()
    _validate(self._state)
    return self._state.response

  def _invoke(self):
    while self._state.code is None:
      _handle_event(self)


class StreamingCall(_Call, grpc.StreamingCall):

  def response_iterator(self):
    self._state.initial_metadata_event.wait()
    return self

  def _next(self):
    state = self._state
    if state.code is grpc.StatusCode.OK:
      raise StopIteration()
    _validate(state)
    state.rpc_call.start_batch(
        (cygrpc.operation_receive_message(_EMPTY_FLAGS),), None)
    while True:
      state.response = None
      _handle_event(self)
      if state.code is grpc.StatusCode.OK:
        raise StopIteration()
      _validate(state)
      if state.response is not None:
        return state.response

  def next(self):
    return self._next()

  def __next__(self):
    return self._next()

  def __iter__(self):
    return self

  def _invoke(self):
    while not self._state.initial_metadata_event.is_set():
      # Note we can be writing, but not reading at this point
      _handle_event(self)
      _validate(self._state)


class _UnaryUnaryMultiCallable(grpc.UnaryUnaryMultiCallable):

  def __init__(
      self, channel, method, request_serializer, response_deserializer):
    self._channel = channel
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def _create_call(
      self, request, timeout=None, metadata=None, credentials=None):
    deadline, deadline_timespec = _deadline(timeout)
    completion_queue = cygrpc.CompletionQueue()
    rpc_call = self._channel.create_call(
        None, 0, completion_queue, self._method, None, deadline_timespec)
    state = _RPCState(rpc_call, completion_queue)
    if credentials is not None:
      rpc_call.set_credentials(credentials._credentials)

    serialized_request = _common.serialize(request, self._request_serializer)
    if serialized_request is None:
      _abort(state, grpc.StatusCode.INTERNAL,
             b'Exception serializing request!')
    operations = (
        cygrpc.operation_send_initial_metadata(
            _common.metadata(metadata), _EMPTY_FLAGS),
        cygrpc.operation_send_message(serialized_request, _EMPTY_FLAGS),
        cygrpc.operation_send_close_from_client(_EMPTY_FLAGS),
        cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),
        cygrpc.operation_receive_message(_EMPTY_FLAGS),
        cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS),
    )
    rpc_call.start_batch(operations, None)
    return UnaryCall(state, self._response_deserializer, deadline)

  def call(self, request, timeout=None, metadata=None, credentials=None):
    call = self._create_call(request, timeout, metadata, credentials)
    call._invoke()
    return call

  def __call__(self, request, timeout=None, metadata=None, credentials=None):
    return self.call(request, timeout, metadata, credentials).response()

  def call_async(self, request, timeout=None, metadata=None, credentials=None):
    call = self._create_call(request, timeout, metadata, credentials)
    thread = _common.CleanupThread(lambda x: call.cancel(), target=call._invoke)
    thread.start()
    return call


class _UnaryStreamMultiCallable(grpc.UnaryStreamMultiCallable):

  def __init__(
      self, channel, method, request_serializer,
      response_deserializer):
    self._channel = channel
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def _create_call(
      self, request, timeout=None, metadata=None, credentials=None):
    deadline, deadline_timespec = _deadline(timeout)
    completion_queue = cygrpc.CompletionQueue()
    rpc_call = self._channel.create_call(
        None, 0, completion_queue, self._method, None, deadline_timespec)
    state = _RPCState(rpc_call, completion_queue)
    if credentials is not None:
      rpc_call.set_credentials(credentials._credentials)

    serialized_request = _common.serialize(request, self._request_serializer)
    if serialized_request is None:
      _abort(rpc_call, grpc.StatusCode.INTERNAL,
             b'Exception serializing request!')
    rpc_call.start_batch(
        (cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),), None)
    operations = (
        cygrpc.operation_send_initial_metadata(
            _common.metadata(metadata), _EMPTY_FLAGS),
        cygrpc.operation_send_message(serialized_request, _EMPTY_FLAGS),
        cygrpc.operation_send_close_from_client(_EMPTY_FLAGS),
        cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS),
    )
    rpc_call.start_batch(operations, None)

    return StreamingCall(state, self._response_deserializer, deadline)

  def call(self, request, timeout=None, metadata=None, credentials=None):
    call = self._create_call(request, timeout, metadata, credentials)
    call._invoke()
    return call

  def __call__(self, request, timeout=None, metadata=None, credentials=None):
    return self.call(
        request, timeout, metadata, credentials).response_iterator()

  def call_async(self, request, timeout=None, metadata=None, credentials=None):
    call = self._create_call(request, timeout, metadata, credentials)
    thread = _common.CleanupThread(lambda x: call.cancel(), target=call._invoke)
    thread.start()
    return call


class _StreamUnaryMultiCallable(grpc.StreamUnaryMultiCallable):

  def __init__(
      self, channel, method, request_serializer,
      response_deserializer):
    self._channel = channel
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def _create_call(
      self, request_iterator, timeout=None, metadata=None, credentials=None):
    deadline, deadline_timespec = _deadline(timeout)
    completion_queue = cygrpc.CompletionQueue()
    rpc_call = self._channel.create_call(
        None, 0, completion_queue, self._method, None, deadline_timespec)
    state = _RPCState(rpc_call, completion_queue)
    if credentials is not None:
      rpc_call.set_credentials(credentials._credentials)
    operations = (
        cygrpc.operation_send_initial_metadata(
            _common.metadata(metadata), _EMPTY_FLAGS),
        cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),
        cygrpc.operation_receive_message(_EMPTY_FLAGS),
        cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS),
    )
    rpc_call.start_batch(operations, None)
    call = UnaryCall(state, self._response_deserializer, deadline)
    _run_request_consumer_thread(
        call, request_iterator, self._request_serializer)
    return call

  def call(self, request_iterator, timeout=None,
           metadata=None, credentials=None):
    call = self._create_call(request_iterator, timeout, metadata, credentials)
    call._invoke()
    return call

  def __call__(self, request_iterator, timeout=None,
               metadata=None, credentials=None):
    return self.call(
        request_iterator, timeout, metadata, credentials).response()

  def call_async(self, request_iterator, timeout=None,
                 metadata=None, credentials=None):
    call = self._create_call(request_iterator, timeout, metadata, credentials)
    thread = _common.CleanupThread(lambda x: call.cancel(), target=call._invoke)
    thread.start()
    return call


class _StreamStreamMultiCallable(grpc.StreamStreamMultiCallable):

  def __init__(
      self, channel, method, request_serializer,
      response_deserializer):
    self._channel = channel
    self._method = method
    self._request_serializer = request_serializer
    self._response_deserializer = response_deserializer

  def _create_call(self, request_iterator, timeout=None,
                   metadata=None, credentials=None):
    deadline, deadline_timespec = _deadline(timeout)
    completion_queue = cygrpc.CompletionQueue()
    rpc_call = self._channel.create_call(
        None, 0, completion_queue, self._method, None, deadline_timespec)
    state = _RPCState(rpc_call, completion_queue)
    if credentials is not None:
      rpc_call.set_credentials(credentials._credentials)

    rpc_call.start_batch(
        (cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),), None)
    operations = (
        cygrpc.operation_send_initial_metadata(
            _common.metadata(metadata), _EMPTY_FLAGS),
        cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS),
    )
    rpc_call.start_batch(operations, None)
    call = StreamingCall(state, self._response_deserializer, deadline)
    _run_request_consumer_thread(
        call, request_iterator, self._request_serializer)
    return call

  def call(self, request_iterator, timeout=None,
           metadata=None, credentials=None):
    call = self._create_call(request_iterator, timeout, metadata, credentials)
    call._invoke()
    return call

  def __call__(self, request_iterator, timeout=None,
               metadata=None, credentials=None):
    return self.call(
        request_iterator, timeout, metadata, credentials).response_iterator()

  def call_async(self, request_iterator, timeout=None,
                 metadata=None, credentials=None):
    call = self._create_call(request_iterator, timeout, metadata, credentials)
    thread = _common.CleanupThread(lambda x: call.cancel(), target=call._invoke)
    thread.start()
    return call


class _ChannelConnectivityState(object):

  def __init__(self, channel):
    self.lock = threading.Lock()
    self.channel = channel
    self.polling = False
    self.connectivity = None
    self.try_to_connect = False
    self.callbacks_and_connectivities = []
    self.delivering = False


def _deliveries(state):
  callbacks_needing_update = []
  for callback_and_connectivity in state.callbacks_and_connectivities:
    callback, callback_connectivity, = callback_and_connectivity
    if callback_connectivity is not state.connectivity:
      callbacks_needing_update.append(callback)
      callback_and_connectivity[1] = state.connectivity
  return callbacks_needing_update


def _deliver(state, initial_connectivity, initial_callbacks):
  connectivity = initial_connectivity
  callbacks = initial_callbacks
  while True:
    for callback in callbacks:
      callable_util.call_logging_exceptions(
          callback, _CHANNEL_SUBSCRIPTION_CALLBACK_ERROR_LOG_MESSAGE,
          connectivity)
    with state.lock:
      callbacks = _deliveries(state)
      if callbacks:
        connectivity = state.connectivity
      else:
        state.delivering = False
        return


def _spawn_delivery(state, callbacks):
  delivering_thread = threading.Thread(
      target=_deliver, args=(state, state.connectivity, callbacks,))
  delivering_thread.start()
  state.delivering = True


# NOTE(https://github.com/grpc/grpc/issues/3064): We'd rather not poll.
def _poll_connectivity(state, channel, initial_try_to_connect):
  try_to_connect = initial_try_to_connect
  connectivity = channel.check_connectivity_state(try_to_connect)
  with state.lock:
    state.connectivity = (
        _common.CYGRPC_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY[
            connectivity])
    callbacks = tuple(
        callback for callback, unused_but_known_to_be_none_connectivity
        in state.callbacks_and_connectivities)
    for callback_and_connectivity in state.callbacks_and_connectivities:
      callback_and_connectivity[1] = state.connectivity
    if callbacks:
      _spawn_delivery(state, callbacks)
  completion_queue = cygrpc.CompletionQueue()
  while True:
    channel.watch_connectivity_state(
        connectivity, cygrpc.Timespec(time.time() + 0.2),
        completion_queue, None)
    event = completion_queue.poll()
    with state.lock:
      if not state.callbacks_and_connectivities and not state.try_to_connect:
        state.polling = False
        state.connectivity = None
        break
      try_to_connect = state.try_to_connect
      state.try_to_connect = False
    if event.success or try_to_connect:
      connectivity = channel.check_connectivity_state(try_to_connect)
      with state.lock:
        state.connectivity = (
            _common.CYGRPC_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY[
                connectivity])
        if not state.delivering:
          callbacks = _deliveries(state)
          if callbacks:
            _spawn_delivery(state, callbacks)


def _subscribe(state, callback, try_to_connect):
  with state.lock:
    if not state.callbacks_and_connectivities and not state.polling:
      polling_thread = threading.Thread(
          target=_poll_connectivity,
          args=(state, state.channel, bool(try_to_connect)))
      polling_thread.start()
      state.polling = True
      state.callbacks_and_connectivities.append([callback, None])
    elif not state.delivering and state.connectivity is not None:
      _spawn_delivery(state, (callback,))
      state.try_to_connect |= bool(try_to_connect)
      state.callbacks_and_connectivities.append(
          [callback, state.connectivity])
    else:
      state.try_to_connect |= bool(try_to_connect)
      state.callbacks_and_connectivities.append([callback, None])


def _unsubscribe(state, callback):
  with state.lock:
    for index, (subscribed_callback, unused_connectivity) in enumerate(
        state.callbacks_and_connectivities):
      if callback == subscribed_callback:
        state.callbacks_and_connectivities.pop(index)
        break


def _moot(state):
  with state.lock:
    del state.callbacks_and_connectivities[:]


def _options(options):
  if options is None:
    pairs = ((cygrpc.ChannelArgKey.primary_user_agent_string, _USER_AGENT),)
  else:
    pairs = list(options) + [
        (cygrpc.ChannelArgKey.primary_user_agent_string, _USER_AGENT)]
  return cygrpc.ChannelArgs(
      cygrpc.ChannelArg(arg_name, arg_value) for arg_name, arg_value in pairs)


class Channel(grpc.Channel):

  def __init__(self, target, options, credentials):
    self._channel = cygrpc.Channel(target, _options(options), credentials)
    self._connectivity_state = _ChannelConnectivityState(self._channel)

  def subscribe(self, callback, try_to_connect=None):
    _subscribe(self._connectivity_state, callback, try_to_connect)

  def unsubscribe(self, callback):
    _unsubscribe(self._connectivity_state, callback)

  def wait_for_ready(self, timeout=None):
    connected_event = threading.Event()
    def check_ready_callback(connectivity):
      if connectivity == grpc.ChannelConnectivity.READY:
        connected_event.set()
    self.subscribe(check_ready_callback, try_to_connect=True)
    is_connected = connected_event.wait(timeout)
    self.unsubscribe(check_ready_callback)
    if not is_connected:
      raise grpc.RpcTimeoutError('Channel failed to connect in time')

  def unary_unary(
      self, method, request_serializer=None, response_deserializer=None):
    return _UnaryUnaryMultiCallable(
        self._channel, method,
        request_serializer, response_deserializer)

  def unary_stream(
      self, method, request_serializer=None, response_deserializer=None):
    return _UnaryStreamMultiCallable(
        self._channel, method,
        request_serializer, response_deserializer)

  def stream_unary(
      self, method, request_serializer=None, response_deserializer=None):
    return _StreamUnaryMultiCallable(
        self._channel, method,
        request_serializer, response_deserializer)

  def stream_stream(
      self, method, request_serializer=None, response_deserializer=None):
    return _StreamStreamMultiCallable(
        self._channel, method,
        request_serializer, response_deserializer)

  def __del__(self):
    _moot(self._connectivity_state)
