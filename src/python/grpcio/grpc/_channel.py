# Copyright 2016 gRPC authors.
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
"""Invocation-side implementation of gRPC Python."""

import logging
import sys
import threading
import time

import grpc
from grpc import _common
from grpc import _grpcio_metadata
from grpc._cython import cygrpc
from grpc.framework.foundation import callable_util

_USER_AGENT = 'grpc-python/{}'.format(_grpcio_metadata.__version__)

_EMPTY_FLAGS = 0

_UNARY_UNARY_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.send_message,
    cygrpc.OperationType.send_close_from_client,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_message,
    cygrpc.OperationType.receive_status_on_client,
)
_UNARY_STREAM_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.send_message,
    cygrpc.OperationType.send_close_from_client,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_status_on_client,
)
_STREAM_UNARY_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_message,
    cygrpc.OperationType.receive_status_on_client,
)
_STREAM_STREAM_INITIAL_DUE = (
    cygrpc.OperationType.send_initial_metadata,
    cygrpc.OperationType.receive_initial_metadata,
    cygrpc.OperationType.receive_status_on_client,
)

_CHANNEL_SUBSCRIPTION_CALLBACK_ERROR_LOG_MESSAGE = (
    'Exception calling channel subscription callback!')


def _deadline(timeout):
    return None if timeout is None else time.time() + timeout


def _unknown_code_details(unknown_cygrpc_code, details):
    return 'Server sent unknown code {} and details "{}"'.format(
        unknown_cygrpc_code, details)


def _wait_once_until(condition, until):
    if until is None:
        condition.wait()
    else:
        remaining = until - time.time()
        if remaining < 0:
            raise grpc.FutureTimeoutError()
        else:
            condition.wait(timeout=remaining)


_INTERNAL_CALL_ERROR_MESSAGE_FORMAT = (
    'Internal gRPC call error %d. ' +
    'Please report to https://github.com/grpc/grpc/issues')


def _check_call_error(call_error, metadata):
    if call_error == cygrpc.CallError.invalid_metadata:
        raise ValueError('metadata was invalid: %s' % metadata)
    elif call_error != cygrpc.CallError.ok:
        raise ValueError(_INTERNAL_CALL_ERROR_MESSAGE_FORMAT % call_error)


def _call_error_set_RPCstate(state, call_error, metadata):
    if call_error == cygrpc.CallError.invalid_metadata:
        _abort(state, grpc.StatusCode.INTERNAL,
               'metadata was invalid: %s' % metadata)
    else:
        _abort(state, grpc.StatusCode.INTERNAL,
               _INTERNAL_CALL_ERROR_MESSAGE_FORMAT % call_error)


class _RPCState(object):

    def __init__(self, due, initial_metadata, trailing_metadata, code, details):
        self.condition = threading.Condition()
        # The cygrpc.OperationType objects representing events due from the RPC's
        # completion queue.
        self.due = set(due)
        self.initial_metadata = initial_metadata
        self.response = None
        self.trailing_metadata = trailing_metadata
        self.code = code
        self.details = details
        # The semantics of grpc.Future.cancel and grpc.Future.cancelled are
        # slightly wonky, so they have to be tracked separately from the rest of the
        # result of the RPC. This field tracks whether cancellation was requested
        # prior to termination of the RPC.
        self.cancelled = False
        self.callbacks = []


def _abort(state, code, details):
    if state.code is None:
        state.code = code
        state.details = details
        if state.initial_metadata is None:
            state.initial_metadata = ()
        state.trailing_metadata = ()


def _handle_event(event, state, response_deserializer):
    callbacks = []
    for batch_operation in event.batch_operations:
        operation_type = batch_operation.type()
        state.due.remove(operation_type)
        if operation_type == cygrpc.OperationType.receive_initial_metadata:
            state.initial_metadata = batch_operation.initial_metadata()
        elif operation_type == cygrpc.OperationType.receive_message:
            serialized_response = batch_operation.message()
            if serialized_response is not None:
                response = _common.deserialize(serialized_response,
                                               response_deserializer)
                if response is None:
                    details = 'Exception deserializing response!'
                    _abort(state, grpc.StatusCode.INTERNAL, details)
                else:
                    state.response = response
        elif operation_type == cygrpc.OperationType.receive_status_on_client:
            state.trailing_metadata = batch_operation.trailing_metadata()
            if state.code is None:
                code = _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE.get(
                    batch_operation.code())
                if code is None:
                    state.code = grpc.StatusCode.UNKNOWN
                    state.details = _unknown_code_details(
                        code, batch_operation.details())
                else:
                    state.code = code
                    state.details = batch_operation.details()
            callbacks.extend(state.callbacks)
            state.callbacks = None
    return callbacks


def _event_handler(state, call, response_deserializer):

    def handle_event(event):
        with state.condition:
            callbacks = _handle_event(event, state, response_deserializer)
            state.condition.notify_all()
            done = not state.due
        for callback in callbacks:
            callback()
        return call if done else None

    return handle_event


def _consume_request_iterator(request_iterator, state, call,
                              request_serializer):
    event_handler = _event_handler(state, call, None)

    def consume_request_iterator():
        while True:
            try:
                request = next(request_iterator)
            except StopIteration:
                break
            except Exception:  # pylint: disable=broad-except
                logging.exception("Exception iterating requests!")
                call.cancel()
                _abort(state, grpc.StatusCode.UNKNOWN,
                       "Exception iterating requests!")
                return
            serialized_request = _common.serialize(request, request_serializer)
            with state.condition:
                if state.code is None and not state.cancelled:
                    if serialized_request is None:
                        call.cancel()
                        details = 'Exception serializing request!'
                        _abort(state, grpc.StatusCode.INTERNAL, details)
                        return
                    else:
                        operations = (cygrpc.SendMessageOperation(
                            serialized_request, _EMPTY_FLAGS),)
                        call.start_client_batch(operations, event_handler)
                        state.due.add(cygrpc.OperationType.send_message)
                        while True:
                            state.condition.wait()
                            if state.code is None:
                                if cygrpc.OperationType.send_message not in state.due:
                                    break
                            else:
                                return
                else:
                    return
        with state.condition:
            if state.code is None:
                operations = (
                    cygrpc.SendCloseFromClientOperation(_EMPTY_FLAGS),)
                call.start_client_batch(operations, event_handler)
                state.due.add(cygrpc.OperationType.send_close_from_client)

    def stop_consumption_thread(timeout):  # pylint: disable=unused-argument
        with state.condition:
            if state.code is None:
                call.cancel()
                state.cancelled = True
                _abort(state, grpc.StatusCode.CANCELLED, 'Cancelled!')
                state.condition.notify_all()

    consumption_thread = _common.CleanupThread(
        stop_consumption_thread, target=consume_request_iterator)
    consumption_thread.start()


class _Rendezvous(grpc.RpcError, grpc.Future, grpc.Call):

    def __init__(self, state, call, response_deserializer, deadline):
        super(_Rendezvous, self).__init__()
        self._state = state
        self._call = call
        self._response_deserializer = response_deserializer
        self._deadline = deadline

    def cancel(self):
        with self._state.condition:
            if self._state.code is None:
                self._call.cancel()
                self._state.cancelled = True
                _abort(self._state, grpc.StatusCode.CANCELLED, 'Cancelled!')
                self._state.condition.notify_all()
            return False

    def cancelled(self):
        with self._state.condition:
            return self._state.cancelled

    def running(self):
        with self._state.condition:
            return self._state.code is None

    def done(self):
        with self._state.condition:
            return self._state.code is not None

    def result(self, timeout=None):
        until = None if timeout is None else time.time() + timeout
        with self._state.condition:
            while True:
                if self._state.code is None:
                    _wait_once_until(self._state.condition, until)
                elif self._state.code is grpc.StatusCode.OK:
                    return self._state.response
                elif self._state.cancelled:
                    raise grpc.FutureCancelledError()
                else:
                    raise self

    def exception(self, timeout=None):
        until = None if timeout is None else time.time() + timeout
        with self._state.condition:
            while True:
                if self._state.code is None:
                    _wait_once_until(self._state.condition, until)
                elif self._state.code is grpc.StatusCode.OK:
                    return None
                elif self._state.cancelled:
                    raise grpc.FutureCancelledError()
                else:
                    return self

    def traceback(self, timeout=None):
        until = None if timeout is None else time.time() + timeout
        with self._state.condition:
            while True:
                if self._state.code is None:
                    _wait_once_until(self._state.condition, until)
                elif self._state.code is grpc.StatusCode.OK:
                    return None
                elif self._state.cancelled:
                    raise grpc.FutureCancelledError()
                else:
                    try:
                        raise self
                    except grpc.RpcError:
                        return sys.exc_info()[2]

    def add_done_callback(self, fn):
        with self._state.condition:
            if self._state.code is None:
                self._state.callbacks.append(lambda: fn(self))
                return

        fn(self)

    def _next(self):
        with self._state.condition:
            if self._state.code is None:
                event_handler = _event_handler(self._state, self._call,
                                               self._response_deserializer)
                self._call.start_client_batch(
                    (cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),),
                    event_handler)
                self._state.due.add(cygrpc.OperationType.receive_message)
            elif self._state.code is grpc.StatusCode.OK:
                raise StopIteration()
            else:
                raise self
            while True:
                self._state.condition.wait()
                if self._state.response is not None:
                    response = self._state.response
                    self._state.response = None
                    return response
                elif cygrpc.OperationType.receive_message not in self._state.due:
                    if self._state.code is grpc.StatusCode.OK:
                        raise StopIteration()
                    elif self._state.code is not None:
                        raise self

    def __iter__(self):
        return self

    def __next__(self):
        return self._next()

    def next(self):
        return self._next()

    def is_active(self):
        with self._state.condition:
            return self._state.code is None

    def time_remaining(self):
        if self._deadline is None:
            return None
        else:
            return max(self._deadline - time.time(), 0)

    def add_callback(self, callback):
        with self._state.condition:
            if self._state.callbacks is None:
                return False
            else:
                self._state.callbacks.append(callback)
                return True

    def initial_metadata(self):
        with self._state.condition:
            while self._state.initial_metadata is None:
                self._state.condition.wait()
            return self._state.initial_metadata

    def trailing_metadata(self):
        with self._state.condition:
            while self._state.trailing_metadata is None:
                self._state.condition.wait()
            return self._state.trailing_metadata

    def code(self):
        with self._state.condition:
            while self._state.code is None:
                self._state.condition.wait()
            return self._state.code

    def details(self):
        with self._state.condition:
            while self._state.details is None:
                self._state.condition.wait()
            return _common.decode(self._state.details)

    def _repr(self):
        with self._state.condition:
            if self._state.code is None:
                return '<_Rendezvous object of in-flight RPC>'
            else:
                return '<_Rendezvous of RPC that terminated with ({}, {})>'.format(
                    self._state.code, _common.decode(self._state.details))

    def __repr__(self):
        return self._repr()

    def __str__(self):
        return self._repr()

    def __del__(self):
        with self._state.condition:
            if self._state.code is None:
                self._call.cancel()
                self._state.cancelled = True
                self._state.code = grpc.StatusCode.CANCELLED
                self._state.condition.notify_all()


def _start_unary_request(request, timeout, request_serializer):
    deadline = _deadline(timeout)
    serialized_request = _common.serialize(request, request_serializer)
    if serialized_request is None:
        state = _RPCState((), (), (), grpc.StatusCode.INTERNAL,
                          'Exception serializing request!')
        rendezvous = _Rendezvous(state, None, None, deadline)
        return deadline, None, rendezvous
    else:
        return deadline, serialized_request, None


def _end_unary_response_blocking(state, call, with_call, deadline):
    if state.code is grpc.StatusCode.OK:
        if with_call:
            rendezvous = _Rendezvous(state, call, None, deadline)
            return state.response, rendezvous
        else:
            return state.response
    else:
        raise _Rendezvous(state, None, None, deadline)


class _UnaryUnaryMultiCallable(grpc.UnaryUnaryMultiCallable):

    def __init__(self, channel, managed_call, method, request_serializer,
                 response_deserializer):
        self._channel = channel
        self._managed_call = managed_call
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer

    def _prepare(self, request, timeout, metadata):
        deadline, serialized_request, rendezvous = (_start_unary_request(
            request, timeout, self._request_serializer))
        if serialized_request is None:
            return None, None, None, rendezvous
        else:
            state = _RPCState(_UNARY_UNARY_INITIAL_DUE, None, None, None, None)
            operations = (
                cygrpc.SendInitialMetadataOperation(metadata, _EMPTY_FLAGS),
                cygrpc.SendMessageOperation(serialized_request, _EMPTY_FLAGS),
                cygrpc.SendCloseFromClientOperation(_EMPTY_FLAGS),
                cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),
                cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
                cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
            )
            return state, operations, deadline, None

    def _blocking(self, request, timeout, metadata, credentials):
        state, operations, deadline, rendezvous = self._prepare(
            request, timeout, metadata)
        if rendezvous:
            raise rendezvous
        else:
            completion_queue = cygrpc.CompletionQueue()
            call = self._channel.create_call(None, 0, completion_queue,
                                             self._method, None, deadline)
            if credentials is not None:
                call.set_credentials(credentials._credentials)
            call_error = call.start_client_batch(operations, None)
            _check_call_error(call_error, metadata)
            _handle_event(completion_queue.poll(), state,
                          self._response_deserializer)
            return state, call, deadline

    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        state, call, deadline = self._blocking(request, timeout, metadata,
                                               credentials)
        return _end_unary_response_blocking(state, call, False, deadline)

    def with_call(self, request, timeout=None, metadata=None, credentials=None):
        state, call, deadline = self._blocking(request, timeout, metadata,
                                               credentials)
        return _end_unary_response_blocking(state, call, True, deadline)

    def future(self, request, timeout=None, metadata=None, credentials=None):
        state, operations, deadline, rendezvous = self._prepare(
            request, timeout, metadata)
        if rendezvous:
            return rendezvous
        else:
            call, drive_call = self._managed_call(None, 0, self._method, None,
                                                  deadline)
            if credentials is not None:
                call.set_credentials(credentials._credentials)
            event_handler = _event_handler(state, call,
                                           self._response_deserializer)
            with state.condition:
                call_error = call.start_client_batch(operations, event_handler)
                if call_error != cygrpc.CallError.ok:
                    _call_error_set_RPCstate(state, call_error, metadata)
                    return _Rendezvous(state, None, None, deadline)
                drive_call()
            return _Rendezvous(state, call, self._response_deserializer,
                               deadline)


class _UnaryStreamMultiCallable(grpc.UnaryStreamMultiCallable):

    def __init__(self, channel, managed_call, method, request_serializer,
                 response_deserializer):
        self._channel = channel
        self._managed_call = managed_call
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer

    def __call__(self, request, timeout=None, metadata=None, credentials=None):
        deadline, serialized_request, rendezvous = (_start_unary_request(
            request, timeout, self._request_serializer))
        if serialized_request is None:
            raise rendezvous
        else:
            state = _RPCState(_UNARY_STREAM_INITIAL_DUE, None, None, None, None)
            call, drive_call = self._managed_call(None, 0, self._method, None,
                                                  deadline)
            if credentials is not None:
                call.set_credentials(credentials._credentials)
            event_handler = _event_handler(state, call,
                                           self._response_deserializer)
            with state.condition:
                call.start_client_batch(
                    (cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),),
                    event_handler)
                operations = (
                    cygrpc.SendInitialMetadataOperation(metadata, _EMPTY_FLAGS),
                    cygrpc.SendMessageOperation(serialized_request,
                                                _EMPTY_FLAGS),
                    cygrpc.SendCloseFromClientOperation(_EMPTY_FLAGS),
                    cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
                )
                call_error = call.start_client_batch(operations, event_handler)
                if call_error != cygrpc.CallError.ok:
                    _call_error_set_RPCstate(state, call_error, metadata)
                    return _Rendezvous(state, None, None, deadline)
                drive_call()
            return _Rendezvous(state, call, self._response_deserializer,
                               deadline)


class _StreamUnaryMultiCallable(grpc.StreamUnaryMultiCallable):

    def __init__(self, channel, managed_call, method, request_serializer,
                 response_deserializer):
        self._channel = channel
        self._managed_call = managed_call
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer

    def _blocking(self, request_iterator, timeout, metadata, credentials):
        deadline = _deadline(timeout)
        state = _RPCState(_STREAM_UNARY_INITIAL_DUE, None, None, None, None)
        completion_queue = cygrpc.CompletionQueue()
        call = self._channel.create_call(None, 0, completion_queue,
                                         self._method, None, deadline)
        if credentials is not None:
            call.set_credentials(credentials._credentials)
        with state.condition:
            call.start_client_batch(
                (cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),), None)
            operations = (
                cygrpc.SendInitialMetadataOperation(metadata, _EMPTY_FLAGS),
                cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
                cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
            )
            call_error = call.start_client_batch(operations, None)
            _check_call_error(call_error, metadata)
            _consume_request_iterator(request_iterator, state, call,
                                      self._request_serializer)
        while True:
            event = completion_queue.poll()
            with state.condition:
                _handle_event(event, state, self._response_deserializer)
                state.condition.notify_all()
                if not state.due:
                    break
        return state, call, deadline

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):
        state, call, deadline = self._blocking(request_iterator, timeout,
                                               metadata, credentials)
        return _end_unary_response_blocking(state, call, False, deadline)

    def with_call(self,
                  request_iterator,
                  timeout=None,
                  metadata=None,
                  credentials=None):
        state, call, deadline = self._blocking(request_iterator, timeout,
                                               metadata, credentials)
        return _end_unary_response_blocking(state, call, True, deadline)

    def future(self,
               request_iterator,
               timeout=None,
               metadata=None,
               credentials=None):
        deadline = _deadline(timeout)
        state = _RPCState(_STREAM_UNARY_INITIAL_DUE, None, None, None, None)
        call, drive_call = self._managed_call(None, 0, self._method, None,
                                              deadline)
        if credentials is not None:
            call.set_credentials(credentials._credentials)
        event_handler = _event_handler(state, call, self._response_deserializer)
        with state.condition:
            call.start_client_batch(
                (cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),),
                event_handler)
            operations = (
                cygrpc.SendInitialMetadataOperation(metadata, _EMPTY_FLAGS),
                cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
                cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
            )
            call_error = call.start_client_batch(operations, event_handler)
            if call_error != cygrpc.CallError.ok:
                _call_error_set_RPCstate(state, call_error, metadata)
                return _Rendezvous(state, None, None, deadline)
            drive_call()
            _consume_request_iterator(request_iterator, state, call,
                                      self._request_serializer)
        return _Rendezvous(state, call, self._response_deserializer, deadline)


class _StreamStreamMultiCallable(grpc.StreamStreamMultiCallable):

    def __init__(self, channel, managed_call, method, request_serializer,
                 response_deserializer):
        self._channel = channel
        self._managed_call = managed_call
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer

    def __call__(self,
                 request_iterator,
                 timeout=None,
                 metadata=None,
                 credentials=None):
        deadline = _deadline(timeout)
        state = _RPCState(_STREAM_STREAM_INITIAL_DUE, None, None, None, None)
        call, drive_call = self._managed_call(None, 0, self._method, None,
                                              deadline)
        if credentials is not None:
            call.set_credentials(credentials._credentials)
        event_handler = _event_handler(state, call, self._response_deserializer)
        with state.condition:
            call.start_client_batch(
                (cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),),
                event_handler)
            operations = (
                cygrpc.SendInitialMetadataOperation(metadata, _EMPTY_FLAGS),
                cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
            )
            call_error = call.start_client_batch(operations, event_handler)
            if call_error != cygrpc.CallError.ok:
                _call_error_set_RPCstate(state, call_error, metadata)
                return _Rendezvous(state, None, None, deadline)
            drive_call()
            _consume_request_iterator(request_iterator, state, call,
                                      self._request_serializer)
        return _Rendezvous(state, call, self._response_deserializer, deadline)


class _ChannelCallState(object):

    def __init__(self, channel):
        self.lock = threading.Lock()
        self.channel = channel
        self.completion_queue = cygrpc.CompletionQueue()
        self.managed_calls = None


def _run_channel_spin_thread(state):

    def channel_spin():
        while True:
            event = state.completion_queue.poll()
            completed_call = event.tag(event)
            if completed_call is not None:
                with state.lock:
                    state.managed_calls.remove(completed_call)
                    if not state.managed_calls:
                        state.managed_calls = None
                        return

    def stop_channel_spin(timeout):  # pylint: disable=unused-argument
        with state.lock:
            if state.managed_calls is not None:
                for call in state.managed_calls:
                    call.cancel()

    channel_spin_thread = _common.CleanupThread(
        stop_channel_spin, target=channel_spin)
    channel_spin_thread.start()


def _channel_managed_call_management(state):

    def create(parent, flags, method, host, deadline):
        """Creates a managed cygrpc.Call and a function to call to drive it.

    If operations are successfully added to the returned cygrpc.Call, the
    returned function must be called. If operations are not successfully added
    to the returned cygrpc.Call, the returned function must not be called.

    Args:
      parent: A cygrpc.Call to be used as the parent of the created call.
      flags: An integer bitfield of call flags.
      method: The RPC method.
      host: A host string for the created call.
      deadline: A float to be the deadline of the created call or None if the
        call is to have an infinite deadline.

    Returns:
      A cygrpc.Call with which to conduct an RPC and a function to call if
        operations are successfully started on the call.
    """
        call = state.channel.create_call(parent, flags, state.completion_queue,
                                         method, host, deadline)

        def drive():
            with state.lock:
                if state.managed_calls is None:
                    state.managed_calls = set((call,))
                    _run_channel_spin_thread(state)
                else:
                    state.managed_calls.add(call)

        return call, drive

    return create


class _ChannelConnectivityState(object):

    def __init__(self, channel):
        self.lock = threading.RLock()
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
        target=_deliver, args=(
            state,
            state.connectivity,
            callbacks,
        ))
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
        callbacks = tuple(callback
                          for callback, unused_but_known_to_be_none_connectivity
                          in state.callbacks_and_connectivities)
        for callback_and_connectivity in state.callbacks_and_connectivities:
            callback_and_connectivity[1] = state.connectivity
        if callbacks:
            _spawn_delivery(state, callbacks)
    completion_queue = cygrpc.CompletionQueue()
    while True:
        channel.watch_connectivity_state(connectivity,
                                         time.time() + 0.2, completion_queue,
                                         None)
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
                    # NOTE(nathaniel): The field is only ever used as a
                    # sequence so it's fine that both lists and tuples are
                    # assigned to it.
                    callbacks = _deliveries(state)  # pylint: disable=redefined-variable-type
                    if callbacks:
                        _spawn_delivery(state, callbacks)


def _moot(state):
    with state.lock:
        del state.callbacks_and_connectivities[:]


def _subscribe(state, callback, try_to_connect):
    with state.lock:
        if not state.callbacks_and_connectivities and not state.polling:
            polling_thread = _common.CleanupThread(
                lambda timeout: _moot(state),
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


def _options(options):
    return list(options) + [
        (
            cygrpc.ChannelArgKey.primary_user_agent_string,
            _USER_AGENT,
        ),
    ]


class Channel(grpc.Channel):
    """A cygrpc.Channel-backed implementation of grpc.Channel."""

    def __init__(self, target, options, credentials):
        """Constructor.

        Args:
          target: The target to which to connect.
          options: Configuration options for the channel.
          credentials: A cygrpc.ChannelCredentials or None.
        """
        self._channel = cygrpc.Channel(
            _common.encode(target), _options(options), credentials)
        self._call_state = _ChannelCallState(self._channel)
        self._connectivity_state = _ChannelConnectivityState(self._channel)

    def subscribe(self, callback, try_to_connect=None):
        _subscribe(self._connectivity_state, callback, try_to_connect)

    def unsubscribe(self, callback):
        _unsubscribe(self._connectivity_state, callback)

    def unary_unary(self,
                    method,
                    request_serializer=None,
                    response_deserializer=None):
        return _UnaryUnaryMultiCallable(
            self._channel, _channel_managed_call_management(self._call_state),
            _common.encode(method), request_serializer, response_deserializer)

    def unary_stream(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        return _UnaryStreamMultiCallable(
            self._channel, _channel_managed_call_management(self._call_state),
            _common.encode(method), request_serializer, response_deserializer)

    def stream_unary(self,
                     method,
                     request_serializer=None,
                     response_deserializer=None):
        return _StreamUnaryMultiCallable(
            self._channel, _channel_managed_call_management(self._call_state),
            _common.encode(method), request_serializer, response_deserializer)

    def stream_stream(self,
                      method,
                      request_serializer=None,
                      response_deserializer=None):
        return _StreamStreamMultiCallable(
            self._channel, _channel_managed_call_management(self._call_state),
            _common.encode(method), request_serializer, response_deserializer)

    def __del__(self):
        _moot(self._connectivity_state)
