# Copyright 2020 The gRPC Authors
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
"""Test helpers for RPC invocation tests."""

import datetime
import threading

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit import test_common
from tests.unit import thread_pool
from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control

_SERIALIZE_REQUEST = lambda bytestring: bytestring * 2
_DESERIALIZE_REQUEST = lambda bytestring: bytestring[len(bytestring) // 2:]
_SERIALIZE_RESPONSE = lambda bytestring: bytestring * 3
_DESERIALIZE_RESPONSE = lambda bytestring: bytestring[:len(bytestring) // 3]

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_UNARY_STREAM_NON_BLOCKING = '/test/UnaryStreamNonBlocking'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'
_STREAM_STREAM_NON_BLOCKING = '/test/StreamStreamNonBlocking'

TIMEOUT_SHORT = datetime.timedelta(seconds=1).total_seconds()


class Callback(object):

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

    def __init__(self, control, thread_pool):
        self._control = control
        self._thread_pool = thread_pool
        non_blocking_functions = (self.handle_unary_stream_non_blocking,
                                  self.handle_stream_stream_non_blocking)
        for non_blocking_function in non_blocking_functions:
            non_blocking_function.__func__.experimental_non_blocking = True
            non_blocking_function.__func__.experimental_thread_pool = self._thread_pool

    def handle_unary_unary(self, request, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
            # TODO(https://github.com/grpc/grpc/issues/8483): test the values
            # returned by these methods rather than only "smoke" testing that
            # the return after having been called.
            servicer_context.is_active()
            servicer_context.time_remaining()
        return request

    def handle_unary_stream(self, request, servicer_context):
        for _ in range(test_constants.STREAM_LENGTH):
            self._control.control()
            yield request
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))

    def handle_unary_stream_non_blocking(self, request, servicer_context,
                                         on_next):
        for _ in range(test_constants.STREAM_LENGTH):
            self._control.control()
            on_next(request)
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
        on_next(None)

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
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
        return b''.join(response_elements)

    def handle_stream_stream(self, request_iterator, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
        for request in request_iterator:
            self._control.control()
            yield request
        self._control.control()

    def handle_stream_stream_non_blocking(self, request_iterator,
                                          servicer_context, on_next):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(((
                'testkey',
                'testvalue',
            ),))
        for request in request_iterator:
            self._control.control()
            on_next(request)
        self._control.control()
        on_next(None)


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
        elif handler_call_details.method == _UNARY_STREAM_NON_BLOCKING:
            return _MethodHandler(
                False, True, _DESERIALIZE_REQUEST, _SERIALIZE_RESPONSE, None,
                self._handler.handle_unary_stream_non_blocking, None, None)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(True, False, _DESERIALIZE_REQUEST,
                                  _SERIALIZE_RESPONSE, None, None,
                                  self._handler.handle_stream_unary, None)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True, None, None, None, None, None,
                                  self._handler.handle_stream_stream)
        elif handler_call_details.method == _STREAM_STREAM_NON_BLOCKING:
            return _MethodHandler(
                True, True, None, None, None, None, None,
                self._handler.handle_stream_stream_non_blocking)
        else:
            return None


def unary_unary_multi_callable(channel):
    return channel.unary_unary(_UNARY_UNARY)


def unary_stream_multi_callable(channel):
    return channel.unary_stream(_UNARY_STREAM,
                                request_serializer=_SERIALIZE_REQUEST,
                                response_deserializer=_DESERIALIZE_RESPONSE)


def unary_stream_non_blocking_multi_callable(channel):
    return channel.unary_stream(_UNARY_STREAM_NON_BLOCKING,
                                request_serializer=_SERIALIZE_REQUEST,
                                response_deserializer=_DESERIALIZE_RESPONSE)


def stream_unary_multi_callable(channel):
    return channel.stream_unary(_STREAM_UNARY,
                                request_serializer=_SERIALIZE_REQUEST,
                                response_deserializer=_DESERIALIZE_RESPONSE)


def stream_stream_multi_callable(channel):
    return channel.stream_stream(_STREAM_STREAM)


def stream_stream_non_blocking_multi_callable(channel):
    return channel.stream_stream(_STREAM_STREAM_NON_BLOCKING)


class BaseRPCTest(object):

    def setUp(self):
        self._control = test_control.PauseFailControl()
        self._thread_pool = thread_pool.RecordingThreadPool(max_workers=None)
        self._handler = _Handler(self._control, self._thread_pool)

        self._server = test_common.test_server()
        port = self._server.add_insecure_port('[::]:0')
        self._server.add_generic_rpc_handlers((_GenericHandler(self._handler),))
        self._server.start()

        self._channel = grpc.insecure_channel('localhost:%d' % port)

    def tearDown(self):
        self._server.stop(None)
        self._channel.close()

    def _consume_one_stream_response_unary_request(self, multi_callable):
        request = b'\x57\x38'

        response_iterator = multi_callable(
            request,
            metadata=(('test', 'ConsumingOneStreamResponseUnaryRequest'),))
        next(response_iterator)

    def _consume_some_but_not_all_stream_responses_unary_request(
            self, multi_callable):
        request = b'\x57\x38'

        response_iterator = multi_callable(
            request,
            metadata=(('test',
                       'ConsumingSomeButNotAllStreamResponsesUnaryRequest'),))
        for _ in range(test_constants.STREAM_LENGTH // 2):
            next(response_iterator)

    def _consume_some_but_not_all_stream_responses_stream_request(
            self, multi_callable):
        requests = tuple(
            b'\x67\x88' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        response_iterator = multi_callable(
            request_iterator,
            metadata=(('test',
                       'ConsumingSomeButNotAllStreamResponsesStreamRequest'),))
        for _ in range(test_constants.STREAM_LENGTH // 2):
            next(response_iterator)

    def _consume_too_many_stream_responses_stream_request(self, multi_callable):
        requests = tuple(
            b'\x67\x88' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        response_iterator = multi_callable(
            request_iterator,
            metadata=(('test',
                       'ConsumingTooManyStreamResponsesStreamRequest'),))
        for _ in range(test_constants.STREAM_LENGTH):
            next(response_iterator)
        for _ in range(test_constants.STREAM_LENGTH):
            with self.assertRaises(StopIteration):
                next(response_iterator)

        self.assertIsNotNone(response_iterator.initial_metadata())
        self.assertIs(grpc.StatusCode.OK, response_iterator.code())
        self.assertIsNotNone(response_iterator.details())
        self.assertIsNotNone(response_iterator.trailing_metadata())

    def _cancelled_unary_request_stream_response(self, multi_callable):
        request = b'\x07\x19'

        with self._control.pause():
            response_iterator = multi_callable(
                request,
                metadata=(('test', 'CancelledUnaryRequestStreamResponse'),))
            self._control.block_until_paused()
            response_iterator.cancel()

        with self.assertRaises(grpc.RpcError) as exception_context:
            next(response_iterator)
        self.assertIs(grpc.StatusCode.CANCELLED,
                      exception_context.exception.code())
        self.assertIsNotNone(response_iterator.initial_metadata())
        self.assertIs(grpc.StatusCode.CANCELLED, response_iterator.code())
        self.assertIsNotNone(response_iterator.details())
        self.assertIsNotNone(response_iterator.trailing_metadata())

    def _cancelled_stream_request_stream_response(self, multi_callable):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        with self._control.pause():
            response_iterator = multi_callable(
                request_iterator,
                metadata=(('test', 'CancelledStreamRequestStreamResponse'),))
            response_iterator.cancel()

        with self.assertRaises(grpc.RpcError):
            next(response_iterator)
        self.assertIsNotNone(response_iterator.initial_metadata())
        self.assertIs(grpc.StatusCode.CANCELLED, response_iterator.code())
        self.assertIsNotNone(response_iterator.details())
        self.assertIsNotNone(response_iterator.trailing_metadata())

    def _expired_unary_request_stream_response(self, multi_callable):
        request = b'\x07\x19'

        with self._control.pause():
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_iterator = multi_callable(
                    request,
                    timeout=test_constants.SHORT_TIMEOUT,
                    metadata=(('test', 'ExpiredUnaryRequestStreamResponse'),))
                next(response_iterator)

        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      response_iterator.code())

    def _expired_stream_request_stream_response(self, multi_callable):
        requests = tuple(
            b'\x67\x18' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        with self._control.pause():
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_iterator = multi_callable(
                    request_iterator,
                    timeout=test_constants.SHORT_TIMEOUT,
                    metadata=(('test', 'ExpiredStreamRequestStreamResponse'),))
                next(response_iterator)

        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      response_iterator.code())

    def _failed_unary_request_stream_response(self, multi_callable):
        request = b'\x37\x17'

        with self.assertRaises(grpc.RpcError) as exception_context:
            with self._control.fail():
                response_iterator = multi_callable(
                    request,
                    metadata=(('test', 'FailedUnaryRequestStreamResponse'),))
                next(response_iterator)

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())

    def _failed_stream_request_stream_response(self, multi_callable):
        requests = tuple(
            b'\x67\x88' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_iterator = multi_callable(
                    request_iterator,
                    metadata=(('test', 'FailedStreamRequestStreamResponse'),))
                tuple(response_iterator)

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())
        self.assertIs(grpc.StatusCode.UNKNOWN, response_iterator.code())

    def _ignored_unary_stream_request_future_unary_response(
            self, multi_callable):
        request = b'\x37\x17'

        multi_callable(request,
                       metadata=(('test',
                                  'IgnoredUnaryRequestStreamResponse'),))

    def _ignored_stream_request_stream_response(self, multi_callable):
        requests = tuple(
            b'\x67\x88' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable(request_iterator,
                       metadata=(('test',
                                  'IgnoredStreamRequestStreamResponse'),))
