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
"""Test making many calls and immediately cancelling most of them."""

import threading
import unittest

from grpc._cython import cygrpc
from grpc.framework.foundation import logging_pool
from tests.unit.framework.common import test_constants

_INFINITE_FUTURE = cygrpc.Timespec(float('+inf'))
_EMPTY_FLAGS = 0
_EMPTY_METADATA = cygrpc.Metadata(())

_SERVER_SHUTDOWN_TAG = 'server_shutdown'
_REQUEST_CALL_TAG = 'request_call'
_RECEIVE_CLOSE_ON_SERVER_TAG = 'receive_close_on_server'
_RECEIVE_MESSAGE_TAG = 'receive_message'
_SERVER_COMPLETE_CALL_TAG = 'server_complete_call'

_SUCCESS_CALL_FRACTION = 1.0 / 8.0


class _State(object):

    def __init__(self):
        self.condition = threading.Condition()
        self.handlers_released = False
        self.parked_handlers = 0
        self.handled_rpcs = 0


def _is_cancellation_event(event):
    return (event.tag is _RECEIVE_CLOSE_ON_SERVER_TAG and
            event.batch_operations[0].received_cancelled)


class _Handler(object):

    def __init__(self, state, completion_queue, rpc_event):
        self._state = state
        self._lock = threading.Lock()
        self._completion_queue = completion_queue
        self._call = rpc_event.operation_call

    def __call__(self):
        with self._state.condition:
            self._state.parked_handlers += 1
            if self._state.parked_handlers == test_constants.THREAD_CONCURRENCY:
                self._state.condition.notify_all()
            while not self._state.handlers_released:
                self._state.condition.wait()

        with self._lock:
            self._call.start_server_batch(
                cygrpc.Operations(
                    (cygrpc.operation_receive_close_on_server(_EMPTY_FLAGS),)),
                _RECEIVE_CLOSE_ON_SERVER_TAG)
            self._call.start_server_batch(
                cygrpc.Operations(
                    (cygrpc.operation_receive_message(_EMPTY_FLAGS),)),
                _RECEIVE_MESSAGE_TAG)
        first_event = self._completion_queue.poll()
        if _is_cancellation_event(first_event):
            self._completion_queue.poll()
        else:
            with self._lock:
                operations = (
                    cygrpc.operation_send_initial_metadata(_EMPTY_METADATA,
                                                           _EMPTY_FLAGS),
                    cygrpc.operation_send_message(b'\x79\x57', _EMPTY_FLAGS),
                    cygrpc.operation_send_status_from_server(
                        _EMPTY_METADATA, cygrpc.StatusCode.ok, b'test details!',
                        _EMPTY_FLAGS),)
                self._call.start_server_batch(
                    cygrpc.Operations(operations), _SERVER_COMPLETE_CALL_TAG)
            self._completion_queue.poll()
            self._completion_queue.poll()


def _serve(state, server, server_completion_queue, thread_pool):
    for _ in range(test_constants.RPC_CONCURRENCY):
        call_completion_queue = cygrpc.CompletionQueue()
        server.request_call(call_completion_queue, server_completion_queue,
                            _REQUEST_CALL_TAG)
        rpc_event = server_completion_queue.poll()
        thread_pool.submit(_Handler(state, call_completion_queue, rpc_event))
        with state.condition:
            state.handled_rpcs += 1
            if test_constants.RPC_CONCURRENCY <= state.handled_rpcs:
                state.condition.notify_all()
    server_completion_queue.poll()


class _QueueDriver(object):

    def __init__(self, condition, completion_queue, due):
        self._condition = condition
        self._completion_queue = completion_queue
        self._due = due
        self._events = []
        self._returned = False

    def start(self):

        def in_thread():
            while True:
                event = self._completion_queue.poll()
                with self._condition:
                    self._events.append(event)
                    self._due.remove(event.tag)
                    self._condition.notify_all()
                    if not self._due:
                        self._returned = True
                        return

        thread = threading.Thread(target=in_thread)
        thread.start()

    def events(self, at_least):
        with self._condition:
            while len(self._events) < at_least:
                self._condition.wait()
            return tuple(self._events)


class CancelManyCallsTest(unittest.TestCase):

    def testCancelManyCalls(self):
        server_thread_pool = logging_pool.pool(
            test_constants.THREAD_CONCURRENCY)

        server_completion_queue = cygrpc.CompletionQueue()
        server = cygrpc.Server(cygrpc.ChannelArgs([]))
        server.register_completion_queue(server_completion_queue)
        port = server.add_http2_port(b'[::]:0')
        server.start()
        channel = cygrpc.Channel('localhost:{}'.format(port).encode(),
                                 cygrpc.ChannelArgs([]))

        state = _State()

        server_thread_args = (state, server, server_completion_queue,
                              server_thread_pool,)
        server_thread = threading.Thread(target=_serve, args=server_thread_args)
        server_thread.start()

        client_condition = threading.Condition()
        client_due = set()
        client_completion_queue = cygrpc.CompletionQueue()
        client_driver = _QueueDriver(client_condition, client_completion_queue,
                                     client_due)
        client_driver.start()

        with client_condition:
            client_calls = []
            for index in range(test_constants.RPC_CONCURRENCY):
                client_call = channel.create_call(
                    None, _EMPTY_FLAGS, client_completion_queue, b'/twinkies',
                    None, _INFINITE_FUTURE)
                operations = (
                    cygrpc.operation_send_initial_metadata(_EMPTY_METADATA,
                                                           _EMPTY_FLAGS),
                    cygrpc.operation_send_message(b'\x45\x56', _EMPTY_FLAGS),
                    cygrpc.operation_send_close_from_client(_EMPTY_FLAGS),
                    cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),
                    cygrpc.operation_receive_message(_EMPTY_FLAGS),
                    cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS),)
                tag = 'client_complete_call_{0:04d}_tag'.format(index)
                client_call.start_client_batch(
                    cygrpc.Operations(operations), tag)
                client_due.add(tag)
                client_calls.append(client_call)

        with state.condition:
            while True:
                if state.parked_handlers < test_constants.THREAD_CONCURRENCY:
                    state.condition.wait()
                elif state.handled_rpcs < test_constants.RPC_CONCURRENCY:
                    state.condition.wait()
                else:
                    state.handlers_released = True
                    state.condition.notify_all()
                    break

        client_driver.events(test_constants.RPC_CONCURRENCY *
                             _SUCCESS_CALL_FRACTION)
        with client_condition:
            for client_call in client_calls:
                client_call.cancel()

        with state.condition:
            server.shutdown(server_completion_queue, _SERVER_SHUTDOWN_TAG)


if __name__ == '__main__':
    unittest.main(verbosity=2)
