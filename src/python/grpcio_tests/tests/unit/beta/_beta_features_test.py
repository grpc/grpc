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
"""Tests Face interface compliance of the gRPC Python Beta API."""

import threading
import unittest

from grpc.beta import implementations
from grpc.beta import interfaces
from grpc.framework.common import cardinality
from grpc.framework.interfaces.face import utilities

from tests.unit import resources
from tests.unit.beta import test_utilities
from tests.unit.framework.common import test_constants

_SERVER_HOST_OVERRIDE = "foo.test.google.fr"

_PER_RPC_CREDENTIALS_METADATA_KEY = b"my-call-credentials-metadata-key"
_PER_RPC_CREDENTIALS_METADATA_VALUE = b"my-call-credentials-metadata-value"

_GROUP = "group"
_UNARY_UNARY = "unary-unary"
_UNARY_STREAM = "unary-stream"
_STREAM_UNARY = "stream-unary"
_STREAM_STREAM = "stream-stream"

_REQUEST = b"abc"
_RESPONSE = b"123"


class _Servicer(object):
    def __init__(self):
        self._condition = threading.Condition()
        self._peer = None
        self._serviced = False

    def unary_unary(self, request, context):
        with self._condition:
            self._request = request
            self._peer = context.protocol_context().peer()
            self._invocation_metadata = context.invocation_metadata()
            context.protocol_context().disable_next_response_compression()
            self._serviced = True
            self._condition.notify_all()
            return _RESPONSE

    def unary_stream(self, request, context):
        with self._condition:
            self._request = request
            self._peer = context.protocol_context().peer()
            self._invocation_metadata = context.invocation_metadata()
            context.protocol_context().disable_next_response_compression()
            self._serviced = True
            self._condition.notify_all()
            return
            yield  # pylint: disable=unreachable

    def stream_unary(self, request_iterator, context):
        for request in request_iterator:
            self._request = request
        with self._condition:
            self._peer = context.protocol_context().peer()
            self._invocation_metadata = context.invocation_metadata()
            context.protocol_context().disable_next_response_compression()
            self._serviced = True
            self._condition.notify_all()
            return _RESPONSE

    def stream_stream(self, request_iterator, context):
        for request in request_iterator:
            with self._condition:
                self._peer = context.protocol_context().peer()
                context.protocol_context().disable_next_response_compression()
                yield _RESPONSE
        with self._condition:
            self._invocation_metadata = context.invocation_metadata()
            self._serviced = True
            self._condition.notify_all()

    def peer(self):
        with self._condition:
            return self._peer

    def block_until_serviced(self):
        with self._condition:
            while not self._serviced:
                self._condition.wait()


class _BlockingIterator(object):
    def __init__(self, upstream):
        self._condition = threading.Condition()
        self._upstream = upstream
        self._allowed = []

    def __iter__(self):
        return self

    def __next__(self):
        return self.next()

    def next(self):
        with self._condition:
            while True:
                if self._allowed is None:
                    raise StopIteration()
                elif self._allowed:
                    return self._allowed.pop(0)
                else:
                    self._condition.wait()

    def allow(self):
        with self._condition:
            try:
                self._allowed.append(next(self._upstream))
            except StopIteration:
                self._allowed = None
            self._condition.notify_all()


def _metadata_plugin(context, callback):
    callback(
        [
            (
                _PER_RPC_CREDENTIALS_METADATA_KEY,
                _PER_RPC_CREDENTIALS_METADATA_VALUE,
            )
        ],
        None,
    )


class BetaFeaturesTest(unittest.TestCase):
    def setUp(self):
        self._servicer = _Servicer()
        method_implementations = {
            (_GROUP, _UNARY_UNARY): utilities.unary_unary_inline(
                self._servicer.unary_unary
            ),
            (_GROUP, _UNARY_STREAM): utilities.unary_stream_inline(
                self._servicer.unary_stream
            ),
            (_GROUP, _STREAM_UNARY): utilities.stream_unary_inline(
                self._servicer.stream_unary
            ),
            (_GROUP, _STREAM_STREAM): utilities.stream_stream_inline(
                self._servicer.stream_stream
            ),
        }

        cardinalities = {
            _UNARY_UNARY: cardinality.Cardinality.UNARY_UNARY,
            _UNARY_STREAM: cardinality.Cardinality.UNARY_STREAM,
            _STREAM_UNARY: cardinality.Cardinality.STREAM_UNARY,
            _STREAM_STREAM: cardinality.Cardinality.STREAM_STREAM,
        }

        server_options = implementations.server_options(
            thread_pool_size=test_constants.POOL_SIZE
        )
        self._server = implementations.server(
            method_implementations, options=server_options
        )
        server_credentials = implementations.ssl_server_credentials(
            [
                (
                    resources.private_key(),
                    resources.certificate_chain(),
                ),
            ]
        )
        port = self._server.add_secure_port("[::]:0", server_credentials)
        self._server.start()
        self._channel_credentials = implementations.ssl_channel_credentials(
            resources.test_root_certificates()
        )
        self._call_credentials = implementations.metadata_call_credentials(
            _metadata_plugin
        )
        channel = test_utilities.not_really_secure_channel(
            "localhost", port, self._channel_credentials, _SERVER_HOST_OVERRIDE
        )
        stub_options = implementations.stub_options(
            thread_pool_size=test_constants.POOL_SIZE
        )
        self._dynamic_stub = implementations.dynamic_stub(
            channel, _GROUP, cardinalities, options=stub_options
        )

    def tearDown(self):
        self._dynamic_stub = None
        self._server.stop(test_constants.SHORT_TIMEOUT).wait()

    def test_unary_unary(self):
        call_options = interfaces.grpc_call_options(
            disable_compression=True, credentials=self._call_credentials
        )
        response = getattr(self._dynamic_stub, _UNARY_UNARY)(
            _REQUEST, test_constants.LONG_TIMEOUT, protocol_options=call_options
        )
        self.assertEqual(_RESPONSE, response)
        self.assertIsNotNone(self._servicer.peer())
        invocation_metadata = [
            (metadatum.key, metadatum.value)
            for metadatum in self._servicer._invocation_metadata
        ]
        self.assertIn(
            (
                _PER_RPC_CREDENTIALS_METADATA_KEY,
                _PER_RPC_CREDENTIALS_METADATA_VALUE,
            ),
            invocation_metadata,
        )

    def test_unary_stream(self):
        call_options = interfaces.grpc_call_options(
            disable_compression=True, credentials=self._call_credentials
        )
        response_iterator = getattr(self._dynamic_stub, _UNARY_STREAM)(
            _REQUEST, test_constants.LONG_TIMEOUT, protocol_options=call_options
        )
        self._servicer.block_until_serviced()
        self.assertIsNotNone(self._servicer.peer())
        invocation_metadata = [
            (metadatum.key, metadatum.value)
            for metadatum in self._servicer._invocation_metadata
        ]
        self.assertIn(
            (
                _PER_RPC_CREDENTIALS_METADATA_KEY,
                _PER_RPC_CREDENTIALS_METADATA_VALUE,
            ),
            invocation_metadata,
        )

    def test_stream_unary(self):
        call_options = interfaces.grpc_call_options(
            credentials=self._call_credentials
        )
        request_iterator = _BlockingIterator(iter((_REQUEST,)))
        response_future = getattr(self._dynamic_stub, _STREAM_UNARY).future(
            request_iterator,
            test_constants.LONG_TIMEOUT,
            protocol_options=call_options,
        )
        response_future.protocol_context().disable_next_request_compression()
        request_iterator.allow()
        response_future.protocol_context().disable_next_request_compression()
        request_iterator.allow()
        self._servicer.block_until_serviced()
        self.assertIsNotNone(self._servicer.peer())
        self.assertEqual(_RESPONSE, response_future.result())
        invocation_metadata = [
            (metadatum.key, metadatum.value)
            for metadatum in self._servicer._invocation_metadata
        ]
        self.assertIn(
            (
                _PER_RPC_CREDENTIALS_METADATA_KEY,
                _PER_RPC_CREDENTIALS_METADATA_VALUE,
            ),
            invocation_metadata,
        )

    def test_stream_stream(self):
        call_options = interfaces.grpc_call_options(
            credentials=self._call_credentials
        )
        request_iterator = _BlockingIterator(iter((_REQUEST,)))
        response_iterator = getattr(self._dynamic_stub, _STREAM_STREAM)(
            request_iterator,
            test_constants.SHORT_TIMEOUT,
            protocol_options=call_options,
        )
        response_iterator.protocol_context().disable_next_request_compression()
        request_iterator.allow()
        response = next(response_iterator)
        response_iterator.protocol_context().disable_next_request_compression()
        request_iterator.allow()
        self._servicer.block_until_serviced()
        self.assertIsNotNone(self._servicer.peer())
        self.assertEqual(_RESPONSE, response)
        invocation_metadata = [
            (metadatum.key, metadatum.value)
            for metadatum in self._servicer._invocation_metadata
        ]
        self.assertIn(
            (
                _PER_RPC_CREDENTIALS_METADATA_KEY,
                _PER_RPC_CREDENTIALS_METADATA_VALUE,
            ),
            invocation_metadata,
        )


class ContextManagementAndLifecycleTest(unittest.TestCase):
    def setUp(self):
        self._servicer = _Servicer()
        self._method_implementations = {
            (_GROUP, _UNARY_UNARY): utilities.unary_unary_inline(
                self._servicer.unary_unary
            ),
            (_GROUP, _UNARY_STREAM): utilities.unary_stream_inline(
                self._servicer.unary_stream
            ),
            (_GROUP, _STREAM_UNARY): utilities.stream_unary_inline(
                self._servicer.stream_unary
            ),
            (_GROUP, _STREAM_STREAM): utilities.stream_stream_inline(
                self._servicer.stream_stream
            ),
        }

        self._cardinalities = {
            _UNARY_UNARY: cardinality.Cardinality.UNARY_UNARY,
            _UNARY_STREAM: cardinality.Cardinality.UNARY_STREAM,
            _STREAM_UNARY: cardinality.Cardinality.STREAM_UNARY,
            _STREAM_STREAM: cardinality.Cardinality.STREAM_STREAM,
        }

        self._server_options = implementations.server_options(
            thread_pool_size=test_constants.POOL_SIZE
        )
        self._server_credentials = implementations.ssl_server_credentials(
            [
                (
                    resources.private_key(),
                    resources.certificate_chain(),
                ),
            ]
        )
        self._channel_credentials = implementations.ssl_channel_credentials(
            resources.test_root_certificates()
        )
        self._stub_options = implementations.stub_options(
            thread_pool_size=test_constants.POOL_SIZE
        )

    def test_stub_context(self):
        server = implementations.server(
            self._method_implementations, options=self._server_options
        )
        port = server.add_secure_port("[::]:0", self._server_credentials)
        server.start()

        channel = test_utilities.not_really_secure_channel(
            "localhost", port, self._channel_credentials, _SERVER_HOST_OVERRIDE
        )
        dynamic_stub = implementations.dynamic_stub(
            channel, _GROUP, self._cardinalities, options=self._stub_options
        )
        for _ in range(100):
            with dynamic_stub:
                pass
        for _ in range(10):
            with dynamic_stub:
                call_options = interfaces.grpc_call_options(
                    disable_compression=True
                )
                response = getattr(dynamic_stub, _UNARY_UNARY)(
                    _REQUEST,
                    test_constants.LONG_TIMEOUT,
                    protocol_options=call_options,
                )
                self.assertEqual(_RESPONSE, response)
                self.assertIsNotNone(self._servicer.peer())

        server.stop(test_constants.SHORT_TIMEOUT).wait()

    def test_server_lifecycle(self):
        for _ in range(100):
            server = implementations.server(
                self._method_implementations, options=self._server_options
            )
            port = server.add_secure_port("[::]:0", self._server_credentials)
            server.start()
            server.stop(test_constants.SHORT_TIMEOUT).wait()
        for _ in range(100):
            server = implementations.server(
                self._method_implementations, options=self._server_options
            )
            server.add_secure_port("[::]:0", self._server_credentials)
            server.add_insecure_port("[::]:0")
            with server:
                server.stop(test_constants.SHORT_TIMEOUT)
            server.stop(test_constants.SHORT_TIMEOUT)


if __name__ == "__main__":
    unittest.main(verbosity=2)
