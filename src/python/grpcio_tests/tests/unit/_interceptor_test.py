# Copyright 2017 gRPC authors.
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
"""Test of gRPC Python interceptors."""

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


class _Interceptor(
        grpc.UnaryUnaryClientInterceptor, grpc.UnaryStreamClientInterceptor,
        grpc.StreamUnaryClientInterceptor, grpc.StreamStreamClientInterceptor,
        grpc.UnaryUnaryServerInterceptor, grpc.UnaryStreamServerInterceptor,
        grpc.StreamUnaryServerInterceptor, grpc.StreamStreamServerInterceptor):

    def __init__(self):
        self.intercepted = []

    def intercept_unary_unary_call(self, invoker, method, request, **kwargs):
        self.intercepted.append('unary_unary_call')
        return invoker(method, request, **kwargs)

    def intercept_unary_unary_future(self, invoker, method, request, **kwargs):
        self.intercepted.append('unary_unary_future')
        return invoker(method, request, **kwargs)

    def intercept_unary_stream_call(self, invoker, method, request, **kwargs):
        self.intercepted.append('unary_stream_call')
        return invoker(method, request, **kwargs)

    def intercept_stream_unary_call(self, invoker, method, request_iterator,
                                    **kwargs):
        self.intercepted.append('stream_unary_call')
        return invoker(method, request_iterator, **kwargs)

    def intercept_stream_unary_future(self, invoker, method, request_iterator,
                                      **kwargs):
        self.intercepted.append('stream_unary_future')
        return invoker(method, request_iterator, **kwargs)

    def intercept_stream_stream_call(self, invoker, method, request_iterator,
                                     **kwargs):
        self.intercepted.append('stream_stream_call')
        return invoker(method, request_iterator, **kwargs)

    def intercept_unary_unary_handler(self, handler, method, request,
                                      servicer_context):
        self.intercepted.append('unary_unary_handler')
        return handler(request, servicer_context)

    def intercept_unary_stream_handler(self, handler, method, request,
                                       servicer_context):
        self.intercepted.append('unary_stream_handler')
        return handler(request, servicer_context)

    def intercept_stream_unary_handler(self, handler, method, request_iterator,
                                       servicer_context):
        self.intercepted.append('stream_unary_handler')
        return handler(request_iterator, servicer_context)

    def intercept_stream_stream_handler(self, handler, method, request_iterator,
                                        servicer_context):
        self.intercepted.append('stream_stream_handler')
        return handler(request_iterator, servicer_context)


class InterceptorTest(unittest.TestCase):

    def setUp(self):
        self._control = test_control.PauseFailControl()
        self._handler = _Handler(self._control)
        self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)

        self._server_interceptor = _Interceptor()
        self._server = grpc.intercept_server(
            grpc.server(self._server_pool), self._server_interceptor)
        port = self._server.add_insecure_port('[::]:0')
        self._server.add_generic_rpc_handlers((_GenericHandler(self._handler),))
        self._server.start()

        self._client_interceptor = _Interceptor()
        self._channel = grpc.intercept_channel(
            grpc.insecure_channel('localhost:%d' % port),
            self._client_interceptor)

    def tearDown(self):
        self._server.stop(None)
        self._server_pool.shutdown(wait=True)

    def testInterceptedUnaryRequestBlockingUnaryResponse(self):
        request = b'\x07\x08'

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _unary_unary_multi_callable(self._channel)
        multi_callable(
            request,
            metadata=(
                ('test', 'InterceptedUnaryRequestBlockingUnaryResponse'),))

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['unary_unary_call'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['unary_unary_handler'])

    def testInterceptedUnaryRequestBlockingUnaryResponseWithCall(self):
        request = b'\x07\x08'

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _unary_unary_multi_callable(self._channel)
        multi_callable.with_call(
            request,
            metadata=(
                ('test',
                 'InterceptedUnaryRequestBlockingUnaryResponseWithCall'),))

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['unary_unary_call'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['unary_unary_handler'])

    def testInterceptedUnaryRequestFutureUnaryResponse(self):
        request = b'\x07\x08'

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _unary_unary_multi_callable(self._channel)
        response_future = multi_callable.future(
            request,
            metadata=(('test', 'InterceptedUnaryRequestFutureUnaryResponse'),))
        response_future.result()

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['unary_unary_future'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['unary_unary_handler'])

    def testInterceptedUnaryRequestStreamResponse(self):
        request = b'\x37\x58'

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _unary_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request,
            metadata=(('test', 'InterceptedUnaryRequestStreamResponse'),))
        tuple(response_iterator)

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['unary_stream_call'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['unary_stream_handler'])

    def testInterceptedStreamRequestBlockingUnaryResponse(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _stream_unary_multi_callable(self._channel)
        multi_callable(
            request_iterator,
            metadata=(
                ('test', 'InterceptedStreamRequestBlockingUnaryResponse'),))

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['stream_unary_call'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['stream_unary_handler'])

    def testInterceptedStreamRequestBlockingUnaryResponseWithCall(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _stream_unary_multi_callable(self._channel)
        multi_callable.with_call(
            request_iterator,
            metadata=(
                ('test',
                 'InterceptedStreamRequestBlockingUnaryResponseWithCall'),))

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['stream_unary_call'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['stream_unary_handler'])

    def testInterceptedStreamRequestFutureUnaryResponse(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _stream_unary_multi_callable(self._channel)
        response_future = multi_callable.future(
            request_iterator,
            metadata=(('test', 'InterceptedStreamRequestFutureUnaryResponse'),))
        response_future.result()

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['stream_unary_future'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['stream_unary_handler'])

    def testInterceptedStreamRequestStreamResponse(self):
        requests = tuple(b'\x77\x58'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        self._client_interceptor.intercepted = []
        self._server_interceptor.intercepted = []

        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request_iterator,
            metadata=(('test', 'InterceptedStreamRequestStreamResponse'),))
        tuple(response_iterator)

        self.assertSequenceEqual(self._client_interceptor.intercepted,
                                 ['stream_stream_call'])
        self.assertSequenceEqual(self._server_interceptor.intercepted,
                                 ['stream_stream_handler'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
