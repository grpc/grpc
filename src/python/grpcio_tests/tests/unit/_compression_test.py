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
"""Tests server and client side compression."""

import unittest

import grpc
from grpc import _grpcio_metadata
from grpc.framework.foundation import logging_pool

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_UNARY_UNARY = '/test/UnaryUnary'
_STREAM_STREAM = '/test/StreamStream'


def handle_unary(request, servicer_context):
    servicer_context.send_initial_metadata(
        [('grpc-internal-encoding-request', 'gzip')])
    return request


def handle_stream(request_iterator, servicer_context):
    # TODO(issue:#6891) We should be able to remove this loop,
    # and replace with return; yield
    servicer_context.send_initial_metadata(
        [('grpc-internal-encoding-request', 'gzip')])
    for request in request_iterator:
        yield request


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, request_streaming, response_streaming):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self.stream_stream = None
        if self.request_streaming and self.response_streaming:
            self.stream_stream = lambda x, y: handle_stream(x, y)
        elif not self.request_streaming and not self.response_streaming:
            self.unary_unary = lambda x, y: handle_unary(x, y)


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(False, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True)
        else:
            return None


class CompressionTest(unittest.TestCase):

    def setUp(self):
        self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        self._server = grpc.server(
            self._server_pool, handlers=(_GenericHandler(),))
        self._port = self._server.add_insecure_port('[::]:0')
        self._server.start()

    def testUnary(self):
        request = b'\x00' * 100

        # Client -> server compressed through default client channel compression
        # settings. Server -> client compressed via server-side metadata setting.
        # TODO(https://github.com/grpc/grpc/issues/4078): replace the "1" integer
        # literal with proper use of the public API.
        compressed_channel = grpc.insecure_channel(
            'localhost:%d' % self._port,
            options=[('grpc.default_compression_algorithm', 1)])
        multi_callable = compressed_channel.unary_unary(_UNARY_UNARY)
        response = multi_callable(request)
        self.assertEqual(request, response)

        # Client -> server compressed through client metadata setting. Server ->
        # client compressed via server-side metadata setting.
        # TODO(https://github.com/grpc/grpc/issues/4078): replace the "0" integer
        # literal with proper use of the public API.
        uncompressed_channel = grpc.insecure_channel(
            'localhost:%d' % self._port,
            options=[('grpc.default_compression_algorithm', 0)])
        multi_callable = compressed_channel.unary_unary(_UNARY_UNARY)
        response = multi_callable(
            request, metadata=[('grpc-internal-encoding-request', 'gzip')])
        self.assertEqual(request, response)

    def testStreaming(self):
        request = b'\x00' * 100

        # TODO(https://github.com/grpc/grpc/issues/4078): replace the "1" integer
        # literal with proper use of the public API.
        compressed_channel = grpc.insecure_channel(
            'localhost:%d' % self._port,
            options=[('grpc.default_compression_algorithm', 1)])
        multi_callable = compressed_channel.stream_stream(_STREAM_STREAM)
        call = multi_callable(iter([request] * test_constants.STREAM_LENGTH))
        for response in call:
            self.assertEqual(request, response)


if __name__ == '__main__':
    unittest.main(verbosity=2)
