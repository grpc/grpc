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
