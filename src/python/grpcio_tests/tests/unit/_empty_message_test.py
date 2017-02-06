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

import unittest

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import test_constants

_REQUEST = b''
_RESPONSE = b''

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'


def handle_unary_unary(request, servicer_context):
    return _RESPONSE


def handle_unary_stream(request, servicer_context):
    for _ in range(test_constants.STREAM_LENGTH):
        yield _RESPONSE


def handle_stream_unary(request_iterator, servicer_context):
    for request in request_iterator:
        pass
    return _RESPONSE


def handle_stream_stream(request_iterator, servicer_context):
    for request in request_iterator:
        yield _RESPONSE


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
            self.stream_stream = handle_stream_stream
        elif self.request_streaming:
            self.stream_unary = handle_stream_unary
        elif self.response_streaming:
            self.unary_stream = handle_unary_stream
        else:
            self.unary_unary = handle_unary_unary


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(False, False)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(False, True)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(True, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True)
        else:
            return None


class EmptyMessageTest(unittest.TestCase):

    def setUp(self):
        self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        self._server = grpc.server(
            self._server_pool, handlers=(_GenericHandler(),))
        port = self._server.add_insecure_port('[::]:0')
        self._server.start()
        self._channel = grpc.insecure_channel('localhost:%d' % port)

    def tearDown(self):
        self._server.stop(0)

    def testUnaryUnary(self):
        response = self._channel.unary_unary(_UNARY_UNARY)(_REQUEST)
        self.assertEqual(_RESPONSE, response)

    def testUnaryStream(self):
        response_iterator = self._channel.unary_stream(_UNARY_STREAM)(_REQUEST)
        self.assertSequenceEqual([_RESPONSE] * test_constants.STREAM_LENGTH,
                                 list(response_iterator))

    def testStreamUnary(self):
        response = self._channel.stream_unary(_STREAM_UNARY)(
            iter([_REQUEST] * test_constants.STREAM_LENGTH))
        self.assertEqual(_RESPONSE, response)

    def testStreamStream(self):
        response_iterator = self._channel.stream_stream(_STREAM_STREAM)(
            iter([_REQUEST] * test_constants.STREAM_LENGTH))
        self.assertSequenceEqual([_RESPONSE] * test_constants.STREAM_LENGTH,
                                 list(response_iterator))


if __name__ == '__main__':
    unittest.main(verbosity=2)
