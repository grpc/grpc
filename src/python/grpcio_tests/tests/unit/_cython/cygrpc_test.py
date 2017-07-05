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


def _metadata_plugin_callback(context, callback):
    callback(
        cygrpc.Metadata([
            cygrpc.Metadatum(_CALL_CREDENTIALS_METADATA_KEY,
                             _CALL_CREDENTIALS_METADATA_VALUE)
        ]), cygrpc.StatusCode.ok, b'')


class TypeSmokeTest(unittest.TestCase):

    def testStringsInUtilitiesUpDown(self):
        self.assertEqual(0, cygrpc.StatusCode.ok)
        metadatum = cygrpc.Metadatum(b'a', b'b')
        self.assertEqual(b'a', metadatum.key)
        self.assertEqual(b'b', metadatum.value)
        metadata = cygrpc.Metadata([metadatum])
        self.assertEqual(1, len(metadata))
        self.assertEqual(metadatum.key, metadata[0].key)

    def testMetadataIteration(self):
        metadata = cygrpc.Metadata(
            [cygrpc.Metadatum(b'a', b'b'), cygrpc.Metadatum(b'c', b'd')])
        iterator = iter(metadata)
        metadatum = next(iterator)
        self.assertIsInstance(metadatum, cygrpc.Metadatum)
        self.assertEqual(metadatum.key, b'a')
        self.assertEqual(metadatum.value, b'b')
        metadatum = next(iterator)
        self.assertIsInstance(metadatum, cygrpc.Metadatum)
        self.assertEqual(metadatum.key, b'c')
        self.assertEqual(metadatum.value, b'd')
        with self.assertRaises(StopIteration):
            next(iterator)

    def testOperationsIteration(self):
        operations = cygrpc.Operations(
            [cygrpc.operation_send_message(b'asdf', _EMPTY_FLAGS)])
        iterator = iter(operations)
        operation = next(iterator)
        self.assertIsInstance(operation, cygrpc.Operation)
        # `Operation`s are write-only structures; can't directly debug anything out
        # of them. Just check that we stop iterating.
        with self.assertRaises(StopIteration):
            next(iterator)

    def testOperationFlags(self):
        operation = cygrpc.operation_send_message(b'asdf',
                                                  cygrpc.WriteFlag.no_compress)
        self.assertEqual(cygrpc.WriteFlag.no_compress, operation.flags)

    def testTimespec(self):
        now = time.time()
        now_timespec_a = cygrpc.Timespec(now)
        now_timespec_b = cygrpc.Timespec(now)
        self.assertAlmostEqual(now, float(now_timespec_a), places=8)
        self.assertEqual(now_timespec_a, now_timespec_b)
        self.assertLess(cygrpc.Timespec(now - 1), cygrpc.Timespec(now))
        self.assertGreater(cygrpc.Timespec(now + 1), cygrpc.Timespec(now))
        self.assertGreaterEqual(cygrpc.Timespec(now + 1), cygrpc.Timespec(now))
        self.assertGreaterEqual(cygrpc.Timespec(now), cygrpc.Timespec(now))
        self.assertLessEqual(cygrpc.Timespec(now - 1), cygrpc.Timespec(now))
        self.assertLessEqual(cygrpc.Timespec(now), cygrpc.Timespec(now))
        self.assertNotEqual(cygrpc.Timespec(now - 1), cygrpc.Timespec(now))
        self.assertNotEqual(cygrpc.Timespec(now + 1), cygrpc.Timespec(now))

    def testCompletionQueueUpDown(self):
        completion_queue = cygrpc.CompletionQueue()
        del completion_queue

    def testServerUpDown(self):
        server = cygrpc.Server(cygrpc.ChannelArgs([]))
        del server

    def testChannelUpDown(self):
        channel = cygrpc.Channel(b'[::]:0', cygrpc.ChannelArgs([]))
        del channel

    def testCredentialsMetadataPluginUpDown(self):
        plugin = cygrpc.CredentialsMetadataPlugin(
            lambda ignored_a, ignored_b: None, b'')
        del plugin

    def testCallCredentialsFromPluginUpDown(self):
        plugin = cygrpc.CredentialsMetadataPlugin(_metadata_plugin_callback,
                                                  b'')
        call_credentials = cygrpc.call_credentials_metadata_plugin(plugin)
        del plugin
        del call_credentials

    def testServerStartNoExplicitShutdown(self):
        server = cygrpc.Server(cygrpc.ChannelArgs([]))
        completion_queue = cygrpc.CompletionQueue()
        server.register_completion_queue(completion_queue)
        port = server.add_http2_port(b'[::]:0')
        self.assertIsInstance(port, int)
        server.start()
        del server

    def testServerStartShutdown(self):
        completion_queue = cygrpc.CompletionQueue()
        server = cygrpc.Server(cygrpc.ChannelArgs([]))
        server.add_http2_port(b'[::]:0')
        server.register_completion_queue(completion_queue)
        server.start()
        shutdown_tag = object()
        server.shutdown(completion_queue, shutdown_tag)
        event = completion_queue.poll()
        self.assertEqual(cygrpc.CompletionType.operation_complete, event.type)
        self.assertIs(shutdown_tag, event.tag)
        del server
        del completion_queue


class ServerClientMixin(object):

    def setUpMixin(self, server_credentials, client_credentials, host_override):
        self.server_completion_queue = cygrpc.CompletionQueue()
        self.server = cygrpc.Server(cygrpc.ChannelArgs([]))
        self.server.register_completion_queue(self.server_completion_queue)
        if server_credentials:
            self.port = self.server.add_http2_port(b'[::]:0',
                                                   server_credentials)
        else:
            self.port = self.server.add_http2_port(b'[::]:0')
        self.server.start()
        self.client_completion_queue = cygrpc.CompletionQueue()
        if client_credentials:
            client_channel_arguments = cygrpc.ChannelArgs([
                cygrpc.ChannelArg(cygrpc.ChannelArgKey.ssl_target_name_override,
                                  host_override)
            ])
            self.client_channel = cygrpc.Channel(
                'localhost:{}'.format(self.port).encode(),
                client_channel_arguments, client_credentials)
        else:
            self.client_channel = cygrpc.Channel(
                'localhost:{}'.format(self.port).encode(),
                cygrpc.ChannelArgs([]))
        if host_override:
            self.host_argument = None  # default host
            self.expected_host = host_override
        else:
            # arbitrary host name necessitating no further identification
            self.host_argument = b'hostess'
            self.expected_host = self.host_argument

    def tearDownMixin(self):
        del self.server
        del self.client_completion_queue
        del self.server_completion_queue

    def _perform_operations(self, operations, call, queue, deadline,
                            description):
        """Perform the list of operations with given call, queue, and deadline.

    Invocation errors are reported with as an exception with `description` in
    the message. Performs the operations asynchronously, returning a future.
    """

        def performer():
            tag = object()
            try:
                call_result = call.start_client_batch(
                    cygrpc.Operations(operations), tag)
                self.assertEqual(cygrpc.CallError.ok, call_result)
                event = queue.poll(deadline)
                self.assertEqual(cygrpc.CompletionType.operation_complete,
                                 event.type)
                self.assertTrue(event.success)
                self.assertIs(tag, event.tag)
            except Exception as error:
                raise Exception(
                    "Error in '{}': {}".format(description, error.message))
            return event

        return test_utilities.SimpleFuture(performer)

    def testEcho(self):
        DEADLINE = time.time() + 5
        DEADLINE_TOLERANCE = 0.25
        CLIENT_METADATA_ASCII_KEY = b'key'
        CLIENT_METADATA_ASCII_VALUE = b'val'
        CLIENT_METADATA_BIN_KEY = b'key-bin'
        CLIENT_METADATA_BIN_VALUE = b'\0' * 1000
        SERVER_INITIAL_METADATA_KEY = b'init_me_me_me'
        SERVER_INITIAL_METADATA_VALUE = b'whodawha?'
        SERVER_TRAILING_METADATA_KEY = b'california_is_in_a_drought'
        SERVER_TRAILING_METADATA_VALUE = b'zomg it is'
        SERVER_STATUS_CODE = cygrpc.StatusCode.ok
        SERVER_STATUS_DETAILS = b'our work is never over'
        REQUEST = b'in death a member of project mayhem has a name'
        RESPONSE = b'his name is robert paulson'
        METHOD = b'twinkies'

        cygrpc_deadline = cygrpc.Timespec(DEADLINE)

        server_request_tag = object()
        request_call_result = self.server.request_call(
            self.server_completion_queue, self.server_completion_queue,
            server_request_tag)

        self.assertEqual(cygrpc.CallError.ok, request_call_result)

        client_call_tag = object()
        client_call = self.client_channel.create_call(
            None, 0, self.client_completion_queue, METHOD, self.host_argument,
            cygrpc_deadline)
        client_initial_metadata = cygrpc.Metadata([
            cygrpc.Metadatum(CLIENT_METADATA_ASCII_KEY,
                             CLIENT_METADATA_ASCII_VALUE),
            cygrpc.Metadatum(CLIENT_METADATA_BIN_KEY, CLIENT_METADATA_BIN_VALUE)
        ])
        client_start_batch_result = client_call.start_client_batch([
            cygrpc.operation_send_initial_metadata(client_initial_metadata,
                                                   _EMPTY_FLAGS),
            cygrpc.operation_send_message(REQUEST, _EMPTY_FLAGS),
            cygrpc.operation_send_close_from_client(_EMPTY_FLAGS),
            cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),
            cygrpc.operation_receive_message(_EMPTY_FLAGS),
            cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS)
        ], client_call_tag)
        self.assertEqual(cygrpc.CallError.ok, client_start_batch_result)
        client_event_future = test_utilities.CompletionQueuePollFuture(
            self.client_completion_queue, cygrpc_deadline)

        request_event = self.server_completion_queue.poll(cygrpc_deadline)
        self.assertEqual(cygrpc.CompletionType.operation_complete,
                         request_event.type)
        self.assertIsInstance(request_event.operation_call, cygrpc.Call)
        self.assertIs(server_request_tag, request_event.tag)
        self.assertEqual(0, len(request_event.batch_operations))
        self.assertTrue(
            test_common.metadata_transmitted(client_initial_metadata,
                                             request_event.request_metadata))
        self.assertEqual(METHOD, request_event.request_call_details.method)
        self.assertEqual(self.expected_host,
                         request_event.request_call_details.host)
        self.assertLess(
            abs(DEADLINE - float(request_event.request_call_details.deadline)),
            DEADLINE_TOLERANCE)

        server_call_tag = object()
        server_call = request_event.operation_call
        server_initial_metadata = cygrpc.Metadata([
            cygrpc.Metadatum(SERVER_INITIAL_METADATA_KEY,
                             SERVER_INITIAL_METADATA_VALUE)
        ])
        server_trailing_metadata = cygrpc.Metadata([
            cygrpc.Metadatum(SERVER_TRAILING_METADATA_KEY,
                             SERVER_TRAILING_METADATA_VALUE)
        ])
        server_start_batch_result = server_call.start_server_batch([
            cygrpc.operation_send_initial_metadata(
                server_initial_metadata,
                _EMPTY_FLAGS), cygrpc.operation_receive_message(_EMPTY_FLAGS),
            cygrpc.operation_send_message(RESPONSE, _EMPTY_FLAGS),
            cygrpc.operation_receive_close_on_server(_EMPTY_FLAGS),
            cygrpc.operation_send_status_from_server(
                server_trailing_metadata, SERVER_STATUS_CODE,
                SERVER_STATUS_DETAILS, _EMPTY_FLAGS)
        ], server_call_tag)
        self.assertEqual(cygrpc.CallError.ok, server_start_batch_result)

        server_event = self.server_completion_queue.poll(cygrpc_deadline)
        client_event = client_event_future.result()

        self.assertEqual(6, len(client_event.batch_operations))
        found_client_op_types = set()
        for client_result in client_event.batch_operations:
            # we expect each op type to be unique
            self.assertNotIn(client_result.type, found_client_op_types)
            found_client_op_types.add(client_result.type)
            if client_result.type == cygrpc.OperationType.receive_initial_metadata:
                self.assertTrue(
                    test_common.metadata_transmitted(
                        server_initial_metadata,
                        client_result.received_metadata))
            elif client_result.type == cygrpc.OperationType.receive_message:
                self.assertEqual(RESPONSE,
                                 client_result.received_message.bytes())
            elif client_result.type == cygrpc.OperationType.receive_status_on_client:
                self.assertTrue(
                    test_common.metadata_transmitted(
                        server_trailing_metadata,
                        client_result.received_metadata))
                self.assertEqual(SERVER_STATUS_DETAILS,
                                 client_result.received_status_details)
                self.assertEqual(SERVER_STATUS_CODE,
                                 client_result.received_status_code)
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
            self.assertNotIn(client_result.type, found_server_op_types)
            found_server_op_types.add(server_result.type)
            if server_result.type == cygrpc.OperationType.receive_message:
                self.assertEqual(REQUEST,
                                 server_result.received_message.bytes())
            elif server_result.type == cygrpc.OperationType.receive_close_on_server:
                self.assertFalse(server_result.received_cancelled)
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

    def test6522(self):
        DEADLINE = time.time() + 5
        DEADLINE_TOLERANCE = 0.25
        METHOD = b'twinkies'

        cygrpc_deadline = cygrpc.Timespec(DEADLINE)
        empty_metadata = cygrpc.Metadata([])

        server_request_tag = object()
        self.server.request_call(self.server_completion_queue,
                                 self.server_completion_queue,
                                 server_request_tag)
        client_call = self.client_channel.create_call(
            None, 0, self.client_completion_queue, METHOD, self.host_argument,
            cygrpc_deadline)

        # Prologue
        def perform_client_operations(operations, description):
            return self._perform_operations(operations, client_call,
                                            self.client_completion_queue,
                                            cygrpc_deadline, description)

        client_event_future = perform_client_operations([
            cygrpc.operation_send_initial_metadata(empty_metadata,
                                                   _EMPTY_FLAGS),
            cygrpc.operation_receive_initial_metadata(_EMPTY_FLAGS),
        ], "Client prologue")

        request_event = self.server_completion_queue.poll(cygrpc_deadline)
        server_call = request_event.operation_call

        def perform_server_operations(operations, description):
            return self._perform_operations(operations, server_call,
                                            self.server_completion_queue,
                                            cygrpc_deadline, description)

        server_event_future = perform_server_operations([
            cygrpc.operation_send_initial_metadata(empty_metadata,
                                                   _EMPTY_FLAGS),
        ], "Server prologue")

        client_event_future.result()  # force completion
        server_event_future.result()

        # Messaging
        for _ in range(10):
            client_event_future = perform_client_operations([
                cygrpc.operation_send_message(b'', _EMPTY_FLAGS),
                cygrpc.operation_receive_message(_EMPTY_FLAGS),
            ], "Client message")
            server_event_future = perform_server_operations([
                cygrpc.operation_send_message(b'', _EMPTY_FLAGS),
                cygrpc.operation_receive_message(_EMPTY_FLAGS),
            ], "Server receive")

            client_event_future.result()  # force completion
            server_event_future.result()

        # Epilogue
        client_event_future = perform_client_operations([
            cygrpc.operation_send_close_from_client(_EMPTY_FLAGS),
            cygrpc.operation_receive_status_on_client(_EMPTY_FLAGS)
        ], "Client epilogue")

        server_event_future = perform_server_operations([
            cygrpc.operation_receive_close_on_server(_EMPTY_FLAGS),
            cygrpc.operation_send_status_from_server(
                empty_metadata, cygrpc.StatusCode.ok, b'', _EMPTY_FLAGS)
        ], "Server epilogue")

        client_event_future.result()  # force completion
        server_event_future.result()


class InsecureServerInsecureClient(unittest.TestCase, ServerClientMixin):

    def setUp(self):
        self.setUpMixin(None, None, None)

    def tearDown(self):
        self.tearDownMixin()


class SecureServerSecureClient(unittest.TestCase, ServerClientMixin):

    def setUp(self):
        server_credentials = cygrpc.server_credentials_ssl(None, [
            cygrpc.SslPemKeyCertPair(resources.private_key(),
                                     resources.certificate_chain())
        ], False)
        client_credentials = cygrpc.channel_credentials_ssl(
            resources.test_root_certificates(), None)
        self.setUpMixin(server_credentials, client_credentials,
                        _SSL_HOST_OVERRIDE)

    def tearDown(self):
        self.tearDownMixin()


if __name__ == '__main__':
    unittest.main(verbosity=2)
