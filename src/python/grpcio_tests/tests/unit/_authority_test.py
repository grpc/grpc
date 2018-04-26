# Copyright 2018 gRPC authors.
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
"""Tests server and client side authority API."""

import threading
import unittest

import grpc
from grpc import _channel

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_AUTHORITY_NAME = 'testauthority'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'


class _RequestAuthority(object):

    def __init__(self):
        self._event = threading.Event()
        self._authority = None

    def get(self):
        self._event.wait()
        return self._authority

    def set(self, authority):
        self._authority = authority
        self._event.set()


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, request_authority, request_streaming,
                 response_streaming):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self._authority = request_authority

    def unary_unary(self, request, servicer_context):
        self._authority.set(servicer_context.authority())
        return _RESPONSE

    def unary_stream(self, request, servicer_context):
        self._authority.set(servicer_context.authority())
        for _ in range(test_constants.STREAM_LENGTH):
            yield _RESPONSE

    def stream_unary(self, request_iterator, servicer_context):
        self._authority.set(servicer_context.authority())
        # TODO(issue:#6891) We should be able to remove this loop
        for request in request_iterator:
            pass
        return _RESPONSE

    def stream_stream(self, request_iterator, servicer_context):
        self._authority.set(servicer_context.authority())
        # TODO(issue:#6891) We should be able to remove this loop,
        # and replace with return; yield
        for request in request_iterator:
            yield _RESPONSE


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self, request_authority):
        self._request_authority = request_authority

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(self._request_authority, False, False)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(self._request_authority, False, True)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(self._request_authority, True, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(self._request_authority, True, True)
        else:
            return None


class AuthorityTest(unittest.TestCase):

    def setUp(self):
        self._server = test_common.test_server()
        self._request_authority = _RequestAuthority()
        self._handler = _GenericHandler(self._request_authority)
        self._server.add_generic_rpc_handlers((self._handler,))
        self._port = self._server.add_insecure_port('[::]:0')
        self._server.start()
        self._channel = grpc.insecure_channel('localhost:%d' % self._port)

    def tearDown(self):
        self._server.stop(None)

    def testUnaryUnary(self):
        multi_callable = self._channel.unary_unary(_UNARY_UNARY)
        unused_response = multi_callable(_REQUEST, authority=_AUTHORITY_NAME)
        self.assertEqual(_AUTHORITY_NAME, self._request_authority.get())

    def testUnaryStream(self):
        multi_callable = self._channel.unary_stream(_UNARY_STREAM)
        call = multi_callable(_REQUEST, authority=_AUTHORITY_NAME)
        for _ in call:
            pass
        self.assertEqual(_AUTHORITY_NAME, self._request_authority.get())

    def testStreamUnary(self):
        multi_callable = self._channel.stream_unary(_STREAM_UNARY)
        unused_response, call = multi_callable.with_call(
            iter([_REQUEST] * test_constants.STREAM_LENGTH),
            authority=_AUTHORITY_NAME)
        self.assertEqual(_AUTHORITY_NAME, self._request_authority.get())

    def testStreamStream(self):
        multi_callable = self._channel.stream_stream(_STREAM_STREAM)
        call = multi_callable(
            iter([_REQUEST] * test_constants.STREAM_LENGTH),
            authority=_AUTHORITY_NAME)
        for _ in call:
            pass
        self.assertEqual(_AUTHORITY_NAME, self._request_authority.get())

    def testNoAuthorityName(self):
        multi_callable = self._channel.unary_unary(_UNARY_UNARY)
        unused_response = multi_callable(_REQUEST)
        self.assertEqual('localhost:%d' % self._port,
                         self._request_authority.get())


if __name__ == '__main__':
    unittest.main(verbosity=2)
