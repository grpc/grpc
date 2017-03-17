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

import itertools
import threading
import unittest
from concurrent import futures

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control

_SERIALIZE_REQUEST = lambda bytestring: bytestring * 2
_DESERIALIZE_REQUEST = lambda bytestring: bytestring[len(bytestring) // 2:]
_SERIALIZE_RESPONSE = lambda bytestring: bytestring * 3
_DESERIALIZE_RESPONSE = lambda bytestring: bytestring[:len(bytestring) // 3]

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'


class _Callback(object):

    def __init__(self):
        self._condition = threading.Condition()
        self._value = None
        self._called = False

    def __call__(self, value):
        with self._condition:
            self._value = value
            self._called = True
            self._condition.notify_all()

    def value(self):
        with self._condition:
            while not self._called:
                self._condition.wait()
            return self._value


class _Handler(object):

    def __init__(self, control):
        self._control = control

    def handle_unary_unary(self, request, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata((('testkey', 'testvalue',),))
        return request

    def handle_unary_stream(self, request, servicer_context):
        for _ in range(test_constants.STREAM_LENGTH):
            self._control.control()
            yield request
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata((('testkey', 'testvalue',),))

    def handle_stream_unary(self, request_iterator, servicer_context):
        if servicer_context is not None:
            servicer_context.invocation_metadata()
        self._control.control()
        response_elements = []
        for request in request_iterator:
            self._control.control()
            response_elements.append(request)
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata((('testkey', 'testvalue',),))
        return b''.join(response_elements)

    def handle_stream_stream(self, request_iterator, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata((('testkey', 'testvalue',),))
        for request in request_iterator:
            self._control.control()
            yield request
        self._control.control()


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, request_streaming, response_streaming,
                 request_deserializer, response_serializer, unary_unary,
                 unary_stream, stream_unary, stream_stream):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = request_deserializer
        self.response_serializer = response_serializer
        self.unary_unary = unary_unary
        self.unary_stream = unary_stream
        self.stream_unary = stream_unary
        self.stream_stream = stream_stream


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self, handler):
        self._handler = handler

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(False, False, None, None,
                                  self._handler.handle_unary_unary, None, None,
                                  None)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(False, True, _DESERIALIZE_REQUEST,
                                  _SERIALIZE_RESPONSE, None,
                                  self._handler.handle_unary_stream, None, None)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(True, False, _DESERIALIZE_REQUEST,
                                  _SERIALIZE_RESPONSE, None, None,
                                  self._handler.handle_stream_unary, None)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True, None, None, None, None, None,
                                  self._handler.handle_stream_stream)
        else:
            return None


class FailAfterFewIterationsCounter(object):

    def __init__(self, high, bytestring):
        self._current = 0
        self._high = high
        self._bytestring = bytestring

    def __iter__(self):
        return self

    def __next__(self):
        if self._current >= self._high:
            raise Exception("This is a deliberate failure in a unit test.")
        else:
            self._current += 1
            return self._bytestring


def _unary_unary_multi_callable(channel):
    return channel.unary_unary(_UNARY_UNARY)


def _unary_stream_multi_callable(channel):
    return channel.unary_stream(
        _UNARY_STREAM,
        request_serializer=_SERIALIZE_REQUEST,
        response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_unary_multi_callable(channel):
    return channel.stream_unary(
        _STREAM_UNARY,
        request_serializer=_SERIALIZE_REQUEST,
        response_deserializer=_DESERIALIZE_RESPONSE)


def _stream_stream_multi_callable(channel):
    return channel.stream_stream(_STREAM_STREAM)


class InvocationDefectsTest(unittest.TestCase):

    def setUp(self):
        self._control = test_control.PauseFailControl()
        self._handler = _Handler(self._control)
        self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)

        self._server = grpc.server(self._server_pool)
        port = self._server.add_insecure_port('[::]:0')
        self._server.add_generic_rpc_handlers((_GenericHandler(self._handler),))
        self._server.start()

        self._channel = grpc.insecure_channel('localhost:%d' % port)

    def tearDown(self):
        self._server.stop(0)

    def testIterableStreamRequestBlockingUnaryResponse(self):
        requests = [b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH)]
        multi_callable = _stream_unary_multi_callable(self._channel)

        with self.assertRaises(grpc.RpcError):
            response = multi_callable(
                requests,
                metadata=(
                    ('test', 'IterableStreamRequestBlockingUnaryResponse'),))

    def testIterableStreamRequestFutureUnaryResponse(self):
        requests = [b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH)]
        multi_callable = _stream_unary_multi_callable(self._channel)
        response_future = multi_callable.future(
            requests,
            metadata=(('test', 'IterableStreamRequestFutureUnaryResponse'),))

        with self.assertRaises(grpc.RpcError):
            response = response_future.result()

    def testIterableStreamRequestStreamResponse(self):
        requests = [b'\x77\x58' for _ in range(test_constants.STREAM_LENGTH)]
        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            requests,
            metadata=(('test', 'IterableStreamRequestStreamResponse'),))

        with self.assertRaises(grpc.RpcError):
            next(response_iterator)

    def testIteratorStreamRequestStreamResponse(self):
        requests_iterator = FailAfterFewIterationsCounter(
            test_constants.STREAM_LENGTH // 2, b'\x07\x08')
        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            requests_iterator,
            metadata=(('test', 'IteratorStreamRequestStreamResponse'),))

        with self.assertRaises(grpc.RpcError):
            for _ in range(test_constants.STREAM_LENGTH // 2 + 1):
                next(response_iterator)


if __name__ == '__main__':
    unittest.main(verbosity=2)
