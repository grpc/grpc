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

import time
import threading
import unittest
import platform

from grpc._cython import cygrpc
from tests.unit._cython import test_utilities
from tests.unit import test_common
from tests.unit import resources

_SSL_HOST_OVERRIDE = b'foo.test.google.fr'
_CALL_CREDENTIALS_METADATA_KEY = 'call-creds-key'
_CALL_CREDENTIALS_METADATA_VALUE = 'call-creds-value'
_EMPTY_FLAGS = 0


def _metadata_plugin(context, callback):
    callback(((
        _CALL_CREDENTIALS_METADATA_KEY,
        _CALL_CREDENTIALS_METADATA_VALUE,
    ),), cygrpc.StatusCode.ok, b'')


class TypeSmokeTest(unittest.TestCase):

    def testCompletionQueueUpDown(self):
        completion_queue = cygrpc.CompletionQueue()
        del completion_queue

    def testServerUpDown(self):
        server = cygrpc.Server(set([
            (
                b'grpc.so_reuseport',
                0,
            ),
        ]))
        del server

    def testChannelUpDown(self):
        channel = cygrpc.Channel(b'[::]:0', None, None)
        channel.close(cygrpc.StatusCode.cancelled, 'Test method anyway!')

    def test_metadata_plugin_call_credentials_up_down(self):
        cygrpc.MetadataPluginCallCredentials(_metadata_plugin,
                                             b'test plugin name!')

    def testServerStartNoExplicitShutdown(self):
        server = cygrpc.Server([
            (
                b'grpc.so_reuseport',
                0,
            ),
        ])
        completion_queue = cygrpc.CompletionQueue()
        server.register_completion_queue(completion_queue)
        port = server.add_http2_port(b'[::]:0')
        self.assertIsInstance(port, int)
        server.start()
        del server

    def testServerStartShutdown(self):
        completion_queue = cygrpc.CompletionQueue()
        server = cygrpc.Server([
            (
                b'grpc.so_reuseport',
                0,
            ),
        ])
        server.add_http2_port(b'[::]:0')
        server.register_completion_queue(completion_queue)
        server.start()
        shutdown_tag = object()
        server.shutdown(completion_queue, shutdown_tag)
        event = completion_queue.poll()
        self.assertEqual(cygrpc.CompletionType.operation_complete,
                         event.completion_type)
        self.assertIs(shutdown_tag, event.tag)
        del server
        del completion_queue


class ServerClientMixin(object):

    def setUpMixin(self, server_credentials, client_credentials, host_override):
        self.server_completion_queue = cygrpc.CompletionQueue()
        self.server = cygrpc.Server([
            (
                b'grpc.so_reuseport',
                0,
            ),
        ])
        self.server.register_completion_queue(self.server_completion_queue)
        if server_credentials:
            self.port = self.server.add_http2_port(b'[::]:0',
                                                   server_credentials)
        else:
            self.port = self.server.add_http2_port(b'[::]:0')
        self.server.start()
        self.client_completion_queue = cygrpc.CompletionQueue()
        if client_credentials:
            client_channel_arguments = ((
                cygrpc.ChannelArgKey.ssl_target_name_override,
                host_override,
            ),)
            self.client_channel = cygrpc.Channel('localhost:{}'.format(
                self.port).encode(), client_channel_arguments,
                                                 client_credentials)
        else:
            self.client_channel = cygrpc.Channel('localhost:{}'.format(
                self.port).encode(), set(), None)
        if host_override:
            self.host_argument = None  # default host
            self.expected_host = host_override
        else:
            # arbitrary host name necessitating no further identification
            self.host_argument = b'hostess'
            self.expected_host = self.host_argument

    def tearDownMixin(self):
        self.client_channel.close(cygrpc.StatusCode.ok, 'test being torn down!')
        del self.client_channel
        del self.server
        del self.client_completion_queue
        del self.server_completion_queue

    def _perform_queue_operations(self, operations, call, queue, deadline,
                                  description):
        """Perform the operations with given call, queue, and deadline.

        Invocation errors are reported with as an exception with `description`
        in the message. Performs the operations asynchronously, returning a
        future.
        """

        def performer():
            tag = object()
            try:
                call_result = call.start_client_batch(operations, tag)
                self.assertEqual(cygrpc.CallError.ok, call_result)
                event = queue.poll(deadline=deadline)
                self.assertEqual(cygrpc.CompletionType.operation_complete,
                                 event.completion_type)
                self.assertTrue(event.success)
                self.assertIs(tag, event.tag)
            except Exception as error:
                raise Exception("Error in '{}': {}".format(
                    description, error.message))
            return event

        return test_utilities.SimpleFuture(performer)

    def test_echo(self):
        DEADLINE = time.time() + 5
        DEADLINE_TOLERANCE = 0.25
        CLIENT_METADATA_ASCII_KEY = 'key'
        CLIENT_METADATA_ASCII_VALUE = 'val'
        CLIENT_METADATA_BIN_KEY = 'key-bin'
        CLIENT_METADATA_BIN_VALUE = b'\0' * 1000
        SERVER_INITIAL_METADATA_KEY = 'init_me_me_me'
        SERVER_INITIAL_METADATA_VALUE = 'whodawha?'
        SERVER_TRAILING_METADATA_KEY = 'california_is_in_a_drought'
        SERVER_TRAILING_METADATA_VALUE = 'zomg it is'
        SERVER_STATUS_CODE = cygrpc.StatusCode.ok
        SERVER_STATUS_DETAILS = 'our work is never over'
        REQUEST = b'in death a member of project mayhem has a name'
        RESPONSE = b'his name is robert paulson'
        METHOD = b'twinkies'

        server_request_tag = object()
        request_call_result = self.server.request_call(
            self.server_completion_queue, self.server_completion_queue,
            server_request_tag)

        self.assertEqual(cygrpc.CallError.ok, request_call_result)

        client_call_tag = object()
        client_initial_metadata = (
            (
                CLIENT_METADATA_ASCII_KEY,
                CLIENT_METADATA_ASCII_VALUE,
            ),
            (
                CLIENT_METADATA_BIN_KEY,
                CLIENT_METADATA_BIN_VALUE,
            ),
        )
        client_call = self.client_channel.integrated_call(
            0, METHOD, self.host_argument, DEADLINE, client_initial_metadata,
            None, [
                (
                    [
                        cygrpc.SendInitialMetadataOperation(
                            client_initial_metadata, _EMPTY_FLAGS),
                        cygrpc.SendMessageOperation(REQUEST, _EMPTY_FLAGS),
                        cygrpc.SendCloseFromClientOperation(_EMPTY_FLAGS),
                        cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),
                        cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
                        cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
                    ],
                    client_call_tag,
                ),
            ])
        client_event_future = test_utilities.SimpleFuture(
            self.client_channel.next_call_event)

        request_event = self.server_completion_queue.poll(deadline=DEADLINE)
        self.assertEqual(cygrpc.CompletionType.operation_complete,
                         request_event.completion_type)
        self.assertIsInstance(request_event.call, cygrpc.Call)
        self.assertIs(server_request_tag, request_event.tag)
        self.assertTrue(
            test_common.metadata_transmitted(client_initial_metadata,
                                             request_event.invocation_metadata))
        self.assertEqual(METHOD, request_event.call_details.method)
        self.assertEqual(self.expected_host, request_event.call_details.host)
        self.assertLess(
            abs(DEADLINE - request_event.call_details.deadline),
            DEADLINE_TOLERANCE)

        server_call_tag = object()
        server_call = request_event.call
        server_initial_metadata = ((
            SERVER_INITIAL_METADATA_KEY,
            SERVER_INITIAL_METADATA_VALUE,
        ),)
        server_trailing_metadata = ((
            SERVER_TRAILING_METADATA_KEY,
            SERVER_TRAILING_METADATA_VALUE,
        ),)
        server_start_batch_result = server_call.start_server_batch([
            cygrpc.SendInitialMetadataOperation(server_initial_metadata,
                                                _EMPTY_FLAGS),
            cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
            cygrpc.SendMessageOperation(RESPONSE, _EMPTY_FLAGS),
            cygrpc.ReceiveCloseOnServerOperation(_EMPTY_FLAGS),
            cygrpc.SendStatusFromServerOperation(
                server_trailing_metadata, SERVER_STATUS_CODE,
                SERVER_STATUS_DETAILS, _EMPTY_FLAGS)
        ], server_call_tag)
        self.assertEqual(cygrpc.CallError.ok, server_start_batch_result)

        server_event = self.server_completion_queue.poll(deadline=DEADLINE)
        client_event = client_event_future.result()

        self.assertEqual(6, len(client_event.batch_operations))
        found_client_op_types = set()
        for client_result in client_event.batch_operations:
            # we expect each op type to be unique
            self.assertNotIn(client_result.type(), found_client_op_types)
            found_client_op_types.add(client_result.type())
            if client_result.type(
            ) == cygrpc.OperationType.receive_initial_metadata:
                self.assertTrue(
                    test_common.metadata_transmitted(
                        server_initial_metadata,
                        client_result.initial_metadata()))
            elif client_result.type() == cygrpc.OperationType.receive_message:
                self.assertEqual(RESPONSE, client_result.message())
            elif client_result.type(
            ) == cygrpc.OperationType.receive_status_on_client:
                self.assertTrue(
                    test_common.metadata_transmitted(
                        server_trailing_metadata,
                        client_result.trailing_metadata()))
                self.assertEqual(SERVER_STATUS_DETAILS, client_result.details())
                self.assertEqual(SERVER_STATUS_CODE, client_result.code())
        self.assertEqual(
            set([
                cygrpc.OperationType.send_initial_metadata,
                cygrpc.OperationType.send_message,
                cygrpc.OperationType.send_close_from_client,
                cygrpc.OperationType.receive_initial_metadata,
                cygrpc.OperationType.receive_message,
                cygrpc.OperationType.receive_status_on_client
            ]), found_client_op_types)

        self.assertEqual(5, len(server_event.batch_operations))
        found_server_op_types = set()
        for server_result in server_event.batch_operations:
            self.assertNotIn(server_result.type(), found_server_op_types)
            found_server_op_types.add(server_result.type())
            if server_result.type() == cygrpc.OperationType.receive_message:
                self.assertEqual(REQUEST, server_result.message())
            elif server_result.type(
            ) == cygrpc.OperationType.receive_close_on_server:
                self.assertFalse(server_result.cancelled())
        self.assertEqual(
            set([
                cygrpc.OperationType.send_initial_metadata,
                cygrpc.OperationType.receive_message,
                cygrpc.OperationType.send_message,
                cygrpc.OperationType.receive_close_on_server,
                cygrpc.OperationType.send_status_from_server
            ]), found_server_op_types)

        del client_call
        del server_call

    def test_6522(self):
        DEADLINE = time.time() + 5
        DEADLINE_TOLERANCE = 0.25
        METHOD = b'twinkies'

        empty_metadata = ()

        # Prologue
        server_request_tag = object()
        self.server.request_call(self.server_completion_queue,
                                 self.server_completion_queue,
                                 server_request_tag)
        client_call = self.client_channel.segregated_call(
            0, METHOD, self.host_argument, DEADLINE, None, None, ([(
                [
                    cygrpc.SendInitialMetadataOperation(empty_metadata,
                                                        _EMPTY_FLAGS),
                    cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS),
                ],
                object(),
            ), (
                [
                    cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
                ],
                object(),
            )]))

        client_initial_metadata_event_future = test_utilities.SimpleFuture(
            client_call.next_event)

        request_event = self.server_completion_queue.poll(deadline=DEADLINE)
        server_call = request_event.call

        def perform_server_operations(operations, description):
            return self._perform_queue_operations(operations, server_call,
                                                  self.server_completion_queue,
                                                  DEADLINE, description)

        server_event_future = perform_server_operations([
            cygrpc.SendInitialMetadataOperation(empty_metadata, _EMPTY_FLAGS),
        ], "Server prologue")

        client_initial_metadata_event_future.result()  # force completion
        server_event_future.result()

        # Messaging
        for _ in range(10):
            client_call.operate([
                cygrpc.SendMessageOperation(b'', _EMPTY_FLAGS),
                cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
            ], "Client message")
            client_message_event_future = test_utilities.SimpleFuture(
                client_call.next_event)
            server_event_future = perform_server_operations([
                cygrpc.SendMessageOperation(b'', _EMPTY_FLAGS),
                cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),
            ], "Server receive")

            client_message_event_future.result()  # force completion
            server_event_future.result()

        # Epilogue
        client_call.operate([
            cygrpc.SendCloseFromClientOperation(_EMPTY_FLAGS),
        ], "Client epilogue")
        # One for ReceiveStatusOnClient, one for SendCloseFromClient.
        client_events_future = test_utilities.SimpleFuture(
            lambda: {
                client_call.next_event(),
                client_call.next_event(),})

        server_event_future = perform_server_operations([
            cygrpc.ReceiveCloseOnServerOperation(_EMPTY_FLAGS),
            cygrpc.SendStatusFromServerOperation(
                empty_metadata, cygrpc.StatusCode.ok, b'', _EMPTY_FLAGS)
        ], "Server epilogue")

        client_events_future.result()  # force completion
        server_event_future.result()


class InsecureServerInsecureClient(unittest.TestCase, ServerClientMixin):

    def setUp(self):
        self.setUpMixin(None, None, None)

    def tearDown(self):
        self.tearDownMixin()


class SecureServerSecureClient(unittest.TestCase, ServerClientMixin):

    def setUp(self):
        server_credentials = cygrpc.server_credentials_ssl(
            None, [
                cygrpc.SslPemKeyCertPair(resources.private_key(),
                                         resources.certificate_chain())
            ], False)
        client_credentials = cygrpc.SSLChannelCredentials(
            resources.test_root_certificates(), None, None)
        self.setUpMixin(server_credentials, client_credentials,
                        _SSL_HOST_OVERRIDE)

    def tearDown(self):
        self.tearDownMixin()


if __name__ == '__main__':
    unittest.main(verbosity=2)
