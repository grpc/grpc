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
"""Tests server and client side metadata API."""

import unittest
import weakref
import logging

import grpc
from grpc import _channel

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_CHANNEL_ARGS = (('grpc.primary_user_agent', 'primary-agent'),
                 ('grpc.secondary_user_agent', 'secondary-agent'))

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'

_INVOCATION_METADATA = (
    (
        b'invocation-md-key',
        u'invocation-md-value',
    ),
    (
        u'invocation-md-key-bin',
        b'\x00\x01',
    ),
)
_EXPECTED_INVOCATION_METADATA = (
    (
        'invocation-md-key',
        'invocation-md-value',
    ),
    (
        'invocation-md-key-bin',
        b'\x00\x01',
    ),
)

_INITIAL_METADATA = ((b'initial-md-key', u'initial-md-value'),
                     (u'initial-md-key-bin', b'\x00\x02'))
_EXPECTED_INITIAL_METADATA = (
    (
        'initial-md-key',
        'initial-md-value',
    ),
    (
        'initial-md-key-bin',
        b'\x00\x02',
    ),
)

_TRAILING_METADATA = (
    (
        'server-trailing-md-key',
        'server-trailing-md-value',
    ),
    (
        'server-trailing-md-key-bin',
        b'\x00\x03',
    ),
)
_EXPECTED_TRAILING_METADATA = _TRAILING_METADATA


def _user_agent(metadata):
    for key, val in metadata:
        if key == 'user-agent':
            return val
    raise KeyError('No user agent!')


def validate_client_metadata(test, servicer_context):
    invocation_metadata = servicer_context.invocation_metadata()
    test.assertTrue(
        test_common.metadata_transmitted(_EXPECTED_INVOCATION_METADATA,
                                         invocation_metadata))
    user_agent = _user_agent(invocation_metadata)
    test.assertTrue(
        user_agent.startswith('primary-agent ' + _channel._USER_AGENT))
    test.assertTrue(user_agent.endswith('secondary-agent'))


def handle_unary_unary(test, request, servicer_context):
    validate_client_metadata(test, servicer_context)
    servicer_context.send_initial_metadata(_INITIAL_METADATA)
    servicer_context.set_trailing_metadata(_TRAILING_METADATA)
    return _RESPONSE


def handle_unary_stream(test, request, servicer_context):
    validate_client_metadata(test, servicer_context)
    servicer_context.send_initial_metadata(_INITIAL_METADATA)
    servicer_context.set_trailing_metadata(_TRAILING_METADATA)
    for _ in range(test_constants.STREAM_LENGTH):
        yield _RESPONSE


def handle_stream_unary(test, request_iterator, servicer_context):
    validate_client_metadata(test, servicer_context)
    servicer_context.send_initial_metadata(_INITIAL_METADATA)
    servicer_context.set_trailing_metadata(_TRAILING_METADATA)
    # TODO(issue:#6891) We should be able to remove this loop
    for request in request_iterator:
        pass
    return _RESPONSE


def handle_stream_stream(test, request_iterator, servicer_context):
    validate_client_metadata(test, servicer_context)
    servicer_context.send_initial_metadata(_INITIAL_METADATA)
    servicer_context.set_trailing_metadata(_TRAILING_METADATA)
    # TODO(issue:#6891) We should be able to remove this loop,
    # and replace with return; yield
    for request in request_iterator:
        yield _RESPONSE


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, test, request_streaming, response_streaming):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self.stream_stream = None
        if self.request_streaming and self.response_streaming:
            self.stream_stream = lambda x, y: handle_stream_stream(test, x, y)
        elif self.request_streaming:
            self.stream_unary = lambda x, y: handle_stream_unary(test, x, y)
        elif self.response_streaming:
            self.unary_stream = lambda x, y: handle_unary_stream(test, x, y)
        else:
            self.unary_unary = lambda x, y: handle_unary_unary(test, x, y)


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self, test):
        self._test = test

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(self._test, False, False)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(self._test, False, True)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(self._test, True, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(self._test, True, True)
        else:
            return None


class MetadataTest(unittest.TestCase):

    def setUp(self):
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers(
            (_GenericHandler(weakref.proxy(self)),))
        port = self._server.add_insecure_port('[::]:0')
        self._server.start()
        self._channel = grpc.insecure_channel('localhost:%d' % port,
                                              options=_CHANNEL_ARGS)

    def tearDown(self):
        self._server.stop(0)
        self._channel.close()

    def testUnaryUnary(self):
        multi_callable = self._channel.unary_unary(_UNARY_UNARY)
        unused_response, call = multi_callable.with_call(
            _REQUEST, metadata=_INVOCATION_METADATA)
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_INITIAL_METADATA,
                                             call.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_TRAILING_METADATA,
                                             call.trailing_metadata()))

    def testUnaryStream(self):
        multi_callable = self._channel.unary_stream(_UNARY_STREAM)
        call = multi_callable(_REQUEST, metadata=_INVOCATION_METADATA)
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_INITIAL_METADATA,
                                             call.initial_metadata()))
        for _ in call:
            pass
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_TRAILING_METADATA,
                                             call.trailing_metadata()))

    def testStreamUnary(self):
        multi_callable = self._channel.stream_unary(_STREAM_UNARY)
        unused_response, call = multi_callable.with_call(
            iter([_REQUEST] * test_constants.STREAM_LENGTH),
            metadata=_INVOCATION_METADATA)
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_INITIAL_METADATA,
                                             call.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_TRAILING_METADATA,
                                             call.trailing_metadata()))

    def testStreamStream(self):
        multi_callable = self._channel.stream_stream(_STREAM_STREAM)
        call = multi_callable(iter([_REQUEST] * test_constants.STREAM_LENGTH),
                              metadata=_INVOCATION_METADATA)
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_INITIAL_METADATA,
                                             call.initial_metadata()))
        for _ in call:
            pass
        self.assertTrue(
            test_common.metadata_transmitted(_EXPECTED_TRAILING_METADATA,
                                             call.trailing_metadata()))


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
