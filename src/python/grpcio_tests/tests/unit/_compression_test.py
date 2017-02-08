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
