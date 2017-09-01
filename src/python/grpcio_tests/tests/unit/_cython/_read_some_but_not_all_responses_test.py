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

_INFINITE_FUTURE = cygrpc.Timespec(float('+inf'))
_EMPTY_FLAGS = 0
_EMPTY_METADATA = cygrpc.Metadata(())


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
        server = cygrpc.Server(cygrpc.ChannelArgs([]))
        server.register_completion_queue(server_completion_queue)
        port = server.add_http2_port(b'[::]:0')
        server.start()
        channel = cygrpc.Channel('localhost:{}'.format(port).encode(),
                                 cygrpc.ChannelArgs([]))

        server_shutdown_tag = 'server_shutdown_tag'
        server_driver = _ServerDriver(server_completion_queue,
                                      server_shutdown_tag)
        server_driver.start()

        client_condition = threading.Condition()
        client_due = set()
        client_completion_queue = cygrpc.CompletionQueue()
        client_driver = _QueueDriver(client_condition, client_completion_queue,
                                     client_due)
        client_driver.start()

        server_call_condition = threading.Condition()
        server_send_initial_metadata_tag = 'server_send_initial_metadata_tag'
        server_send_first_message_tag = 'server_send_first_message_tag'
        server_send_second_message_tag = 'server_send_second_message_tag'
        server_complete_rpc_tag = 'server_complete_rpc_tag'
        server_call_due = set(
            (server_send_initial_metadata_tag, server_send_first_message_tag,
             server_send_second_message_tag, server_complete_rpc_tag,))
        server_call_completion_queue = cygrpc.CompletionQueue()
        server_call_driver = _QueueDriver(server_call_condition,
                                          server_call_completion_queue,
                                          server_call_due)
        server_call_driver.start()

        server_rpc_tag = 'server_rpc_tag'
        request_call_result = server.request_call(server_call_completion_queue,
                                                  server_completion_queue,
                                                  server_rpc_tag)

        client_call = channel.create_call(None, _EMPTY_FLAGS,
                                          client_completion_queue, b'/twinkies',
                                          None, _INFINITE_FUTURE)
        client_receive_initial_metadata_tag = 'client_receive_initial_metadata_tag'
        client_complete_rpc_tag = 'client_complete_rpc_tag'
        with client_condition:
            client_receive_initial_metadata_start_batch_result = (
                client_call.start_client_batch(
                    cygrpc.Operations([
                        cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),
                    ]), client_receive_initial_metadata_tag))
            client_due.add(client_receive_initial_metadata_tag)
            client_complete_rpc_start_batch_result = (
                client_call.start_client_batch(
                    cygrpc.Operations([
                        cygrpc.operation_send_initial_metadata(_EMPTY_METADATA,
                                                               _EMPTY_FLAGS),
                        cygrpc.operation_send_close_from_client(_EMPTY_FLAGS),
                        cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS),
                    ]), client_complete_rpc_tag))
            client_due.add(client_complete_rpc_tag)

        server_rpc_event = server_driver.first_event()

        with server_call_condition:
            server_send_initial_metadata_start_batch_result = (
                server_rpc_event.operation_call.start_server_batch([
                    cygrpc.operation_send_initial_metadata(_EMPTY_METADATA,
                                                           _EMPTY_FLAGS),
                ], server_send_initial_metadata_tag))
            server_send_first_message_start_batch_result = (
                server_rpc_event.operation_call.start_server_batch([
                    cygrpc.operation_send_message(b'\x07', _EMPTY_FLAGS),
                ], server_send_first_message_tag))
        server_send_initial_metadata_event = server_call_driver.event_with_tag(
            server_send_initial_metadata_tag)
        server_send_first_message_event = server_call_driver.event_with_tag(
            server_send_first_message_tag)
        with server_call_condition:
            server_send_second_message_start_batch_result = (
                server_rpc_event.operation_call.start_server_batch([
                    cygrpc.operation_send_message(b'\x07', _EMPTY_FLAGS),
                ], server_send_second_message_tag))
            server_complete_rpc_start_batch_result = (
                server_rpc_event.operation_call.start_server_batch([
                    cygrpc.operation_receive_close_on_server(_EMPTY_FLAGS),
                    cygrpc.operation_send_status_from_server(
                        cygrpc.Metadata(()), cygrpc.StatusCode.ok,
                        b'test details', _EMPTY_FLAGS),
                ], server_complete_rpc_tag))
        server_send_second_message_event = server_call_driver.event_with_tag(
            server_send_second_message_tag)
        server_complete_rpc_event = server_call_driver.event_with_tag(
            server_complete_rpc_tag)
        server_call_driver.events()

        with client_condition:
            client_receive_first_message_tag = 'client_receive_first_message_tag'
            client_receive_first_message_start_batch_result = (
                client_call.start_client_batch(
                    cygrpc.Operations([
                        cygrpc.operation_receive_message(_EMPTY_FLAGS),
                    ]), client_receive_first_message_tag))
            client_due.add(client_receive_first_message_tag)
        client_receive_first_message_event = client_driver.event_with_tag(
            client_receive_first_message_tag)

        client_call_cancel_result = client_call.cancel()
        client_driver.events()

        server.shutdown(server_completion_queue, server_shutdown_tag)
        server.cancel_all_calls()
        server_driver.events()

        self.assertEqual(cygrpc.CallError.ok, request_call_result)
        self.assertEqual(cygrpc.CallError.ok,
                         server_send_initial_metadata_start_batch_result)
        self.assertEqual(cygrpc.CallError.ok,
                         client_receive_initial_metadata_start_batch_result)
        self.assertEqual(cygrpc.CallError.ok,
                         client_complete_rpc_start_batch_result)
        self.assertEqual(cygrpc.CallError.ok, client_call_cancel_result)
        self.assertIs(server_rpc_tag, server_rpc_event.tag)
        self.assertEqual(cygrpc.CompletionType.operation_complete,
                         server_rpc_event.type)
        self.assertIsInstance(server_rpc_event.operation_call, cygrpc.Call)
        self.assertEqual(0, len(server_rpc_event.batch_operations))


if __name__ == '__main__':
    unittest.main(verbosity=2)
