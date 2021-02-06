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
"""Test a corner-case at the level of the Cython API."""

import threading
import unittest

from grpc._cython import cygrpc
from tests.unit._cython import test_utilities

_EMPTY_FLAGS = 0
_EMPTY_METADATA = ()


class _ServerDriver(object):

    def __init__(self, completion_queue, shutdown_tag):
        self._condition = threading.Condition()
        self._completion_queue = completion_queue
        self._shutdown_tag = shutdown_tag
        self._events = []
        self._saw_shutdown_tag = False

    def start(self):

        def in_thread():
            while True:
                event = self._completion_queue.poll()
                with self._condition:
                    self._events.append(event)
                    self._condition.notify()
                    if event.tag is self._shutdown_tag:
                        self._saw_shutdown_tag = True
                        break

        thread = threading.Thread(target=in_thread)
        thread.start()

    def done(self):
        with self._condition:
            return self._saw_shutdown_tag

    def first_event(self):
        with self._condition:
            while not self._events:
                self._condition.wait()
            return self._events[0]

    def events(self):
        with self._condition:
            while not self._saw_shutdown_tag:
                self._condition.wait()
            return tuple(self._events)


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

    def done(self):
        with self._condition:
            return self._returned

    def event_with_tag(self, tag):
        with self._condition:
            while True:
                for event in self._events:
                    if event.tag is tag:
                        return event
                self._condition.wait()

    def events(self):
        with self._condition:
            while not self._returned:
                self._condition.wait()
            return tuple(self._events)


class ReadSomeButNotAllResponsesTest(unittest.TestCase):

    def testReadSomeButNotAllResponses(self):
        server_completion_queue = cygrpc.CompletionQueue()
        server = cygrpc.Server([(
            b'grpc.so_reuseport',
            0,
        )], False)
        server.register_completion_queue(server_completion_queue)
        port = server.add_http2_port(b'[::]:0')
        server.start()
        channel = cygrpc.Channel('localhost:{}'.format(port).encode(), set(),
                                 None)

        server_shutdown_tag = 'server_shutdown_tag'
        server_driver = _ServerDriver(server_completion_queue,
                                      server_shutdown_tag)
        server_driver.start()

        client_condition = threading.Condition()
        client_due = set()

        server_call_condition = threading.Condition()
        server_send_initial_metadata_tag = 'server_send_initial_metadata_tag'
        server_send_first_message_tag = 'server_send_first_message_tag'
        server_send_second_message_tag = 'server_send_second_message_tag'
        server_complete_rpc_tag = 'server_complete_rpc_tag'
        server_call_due = set((
            server_send_initial_metadata_tag,
            server_send_first_message_tag,
            server_send_second_message_tag,
            server_complete_rpc_tag,
        ))
        server_call_completion_queue = cygrpc.CompletionQueue()
        server_call_driver = _QueueDriver(server_call_condition,
                                          server_call_completion_queue,
                                          server_call_due)
        server_call_driver.start()

        server_rpc_tag = 'server_rpc_tag'
        request_call_result = server.request_call(server_call_completion_queue,
                                                  server_completion_queue,
                                                  server_rpc_tag)

        client_receive_initial_metadata_tag = 'client_receive_initial_metadata_tag'
        client_complete_rpc_tag = 'client_complete_rpc_tag'
        client_call = channel.segregated_call(
            _EMPTY_FLAGS, b'/twinkies', None, None, _EMPTY_METADATA, None, (
                (
                    [
                        cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),
                    ],
                    client_receive_initial_metadata_tag,
                ),
                (
                    [
                        cygrpc.SendInitialMetadataOperation(
                            _EMPTY_METADATA, _EMPTY_FLAGS),
                        cygrpc.SendCloseFromClientOperation(_EMPTY_FLAGS),
                        cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
                    ],
                    client_complete_rpc_tag,
                ),
            ))
        client_receive_initial_metadata_event_future = test_utilities.SimpleFuture(
            client_call.next_event)

        server_rpc_event = server_driver.first_event()

        with server_call_condition:
            server_send_initial_metadata_start_batch_result = (
                server_rpc_event.call.start_server_batch([
                    cygrpc.SendInitialMetadataOperation(_EMPTY_METADATA,
                                                        _EMPTY_FLAGS),
                ], server_send_initial_metadata_tag))
            server_send_first_message_start_batch_result = (
                server_rpc_event.call.start_server_batch([
                    cygrpc.SendMessageOperation(b'\x07', _EMPTY_FLAGS),
                ], server_send_first_message_tag))
        server_send_initial_metadata_event = server_call_driver.event_with_tag(
            server_send_initial_metadata_tag)
        server_send_first_message_event = server_call_driver.event_with_tag(
            server_send_first_message_tag)
        with server_call_condition:
            server_send_second_message_start_batch_result = (
                server_rpc_event.call.start_server_batch([
                    cygrpc.SendMessageOperation(b'\x07', _EMPTY_FLAGS),
                ], server_send_second_message_tag))
            server_complete_rpc_start_batch_result = (
                server_rpc_event.call.start_server_batch([
                    cygrpc.ReceiveCloseOnServerOperation(_EMPTY_FLAGS),
                    cygrpc.SendStatusFromServerOperation(
                        (), cygrpc.StatusCode.ok, b'test details',
                        _EMPTY_FLAGS),
                ], server_complete_rpc_tag))
        server_send_second_message_event = server_call_driver.event_with_tag(
            server_send_second_message_tag)
        server_complete_rpc_event = server_call_driver.event_with_tag(
            server_complete_rpc_tag)
        server_call_driver.events()

        client_recieve_initial_metadata_event = client_receive_initial_metadata_event_future.result(
        )

        client_receive_first_message_tag = 'client_receive_first_message_tag'
        client_call.operate([
            cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
        ], client_receive_first_message_tag)
        client_receive_first_message_event = client_call.next_event()

        client_call_cancel_result = client_call.cancel(
            cygrpc.StatusCode.cancelled, 'Cancelled during test!')
        client_complete_rpc_event = client_call.next_event()

        channel.close(cygrpc.StatusCode.unknown, 'Channel closed!')
        server.shutdown(server_completion_queue, server_shutdown_tag)
        server.cancel_all_calls()
        server_driver.events()

        self.assertEqual(cygrpc.CallError.ok, request_call_result)
        self.assertEqual(cygrpc.CallError.ok,
                         server_send_initial_metadata_start_batch_result)
        self.assertIs(server_rpc_tag, server_rpc_event.tag)
        self.assertEqual(cygrpc.CompletionType.operation_complete,
                         server_rpc_event.completion_type)
        self.assertIsInstance(server_rpc_event.call, cygrpc.Call)


if __name__ == '__main__':
    unittest.main(verbosity=2)
