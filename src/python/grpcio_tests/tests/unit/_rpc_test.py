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
"""Test of RPCs made against gRPC Python's application-layer API."""

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


class RPCTest(unittest.TestCase):

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
        self._server.stop(None)
        self._server_pool.shutdown(wait=True)

    def testUnrecognizedMethod(self):
        request = b'abc'

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary('NoSuchMethod')(request)

        self.assertEqual(grpc.StatusCode.UNIMPLEMENTED,
                         exception_context.exception.code())

    def testSuccessfulUnaryRequestBlockingUnaryResponse(self):
        request = b'\x07\x08'
        expected_response = self._handler.handle_unary_unary(request, None)

        multi_callable = _unary_unary_multi_callable(self._channel)
        response = multi_callable(
            request,
            metadata=(('test', 'SuccessfulUnaryRequestBlockingUnaryResponse'),))

        self.assertEqual(expected_response, response)

    def testSuccessfulUnaryRequestBlockingUnaryResponseWithCall(self):
        request = b'\x07\x08'
        expected_response = self._handler.handle_unary_unary(request, None)

        multi_callable = _unary_unary_multi_callable(self._channel)
        response, call = multi_callable.with_call(
            request,
            metadata=(('test',
                       'SuccessfulUnaryRequestBlockingUnaryResponseWithCall'),))

        self.assertEqual(expected_response, response)
        self.assertIs(grpc.StatusCode.OK, call.code())

    def testSuccessfulUnaryRequestFutureUnaryResponse(self):
        request = b'\x07\x08'
        expected_response = self._handler.handle_unary_unary(request, None)

        multi_callable = _unary_unary_multi_callable(self._channel)
        response_future = multi_callable.future(
            request,
            metadata=(('test', 'SuccessfulUnaryRequestFutureUnaryResponse'),))
        response = response_future.result()

        self.assertIsInstance(response_future, grpc.Future)
        self.assertIsInstance(response_future, grpc.Call)
        self.assertEqual(expected_response, response)
        self.assertIsNone(response_future.exception())
        self.assertIsNone(response_future.traceback())

    def testSuccessfulUnaryRequestStreamResponse(self):
        request = b'\x37\x58'
        expected_responses = tuple(
            self._handler.handle_unary_stream(request, None))

        multi_callable = _unary_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request,
            metadata=(('test', 'SuccessfulUnaryRequestStreamResponse'),))
        responses = tuple(response_iterator)

        self.assertSequenceEqual(expected_responses, responses)

    def testSuccessfulStreamRequestBlockingUnaryResponse(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        request_iterator = iter(requests)

        multi_callable = _stream_unary_multi_callable(self._channel)
        response = multi_callable(
            request_iterator,
            metadata=(
                ('test', 'SuccessfulStreamRequestBlockingUnaryResponse'),))

        self.assertEqual(expected_response, response)

    def testSuccessfulStreamRequestBlockingUnaryResponseWithCall(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        request_iterator = iter(requests)

        multi_callable = _stream_unary_multi_callable(self._channel)
        response, call = multi_callable.with_call(
            request_iterator,
            metadata=(
                ('test',
                 'SuccessfulStreamRequestBlockingUnaryResponseWithCall'),))

        self.assertEqual(expected_response, response)
        self.assertIs(grpc.StatusCode.OK, call.code())

    def testSuccessfulStreamRequestFutureUnaryResponse(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        request_iterator = iter(requests)

        multi_callable = _stream_unary_multi_callable(self._channel)
        response_future = multi_callable.future(
            request_iterator,
            metadata=(('test', 'SuccessfulStreamRequestFutureUnaryResponse'),))
        response = response_future.result()

        self.assertEqual(expected_response, response)
        self.assertIsNone(response_future.exception())
        self.assertIsNone(response_future.traceback())

    def testSuccessfulStreamRequestStreamResponse(self):
        requests = tuple(b'\x77\x58'
                         for _ in range(test_constants.STREAM_LENGTH))
        expected_responses = tuple(
            self._handler.handle_stream_stream(iter(requests), None))
        request_iterator = iter(requests)

        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request_iterator,
            metadata=(('test', 'SuccessfulStreamRequestStreamResponse'),))
        responses = tuple(response_iterator)

        self.assertSequenceEqual(expected_responses, responses)

    def testSequentialInvocations(self):
        first_request = b'\x07\x08'
        second_request = b'\x0809'
        expected_first_response = self._handler.handle_unary_unary(
            first_request, None)
        expected_second_response = self._handler.handle_unary_unary(
            second_request, None)

        multi_callable = _unary_unary_multi_callable(self._channel)
        first_response = multi_callable(
            first_request, metadata=(('test', 'SequentialInvocations'),))
        second_response = multi_callable(
            second_request, metadata=(('test', 'SequentialInvocations'),))

        self.assertEqual(expected_first_response, first_response)
        self.assertEqual(expected_second_response, second_response)

    def testConcurrentBlockingInvocations(self):
        pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        expected_responses = [expected_response
                             ] * test_constants.THREAD_CONCURRENCY
        response_futures = [None] * test_constants.THREAD_CONCURRENCY

        multi_callable = _stream_unary_multi_callable(self._channel)
        for index in range(test_constants.THREAD_CONCURRENCY):
            request_iterator = iter(requests)
            response_future = pool.submit(
                multi_callable,
                request_iterator,
                metadata=(('test', 'ConcurrentBlockingInvocations'),))
            response_futures[index] = response_future
        responses = tuple(response_future.result()
                          for response_future in response_futures)

        pool.shutdown(wait=True)
        self.assertSequenceEqual(expected_responses, responses)

    def testConcurrentFutureInvocations(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        expected_responses = [expected_response
                             ] * test_constants.THREAD_CONCURRENCY
        response_futures = [None] * test_constants.THREAD_CONCURRENCY

        multi_callable = _stream_unary_multi_callable(self._channel)
        for index in range(test_constants.THREAD_CONCURRENCY):
            request_iterator = iter(requests)
            response_future = multi_callable.future(
                request_iterator,
                metadata=(('test', 'ConcurrentFutureInvocations'),))
            response_futures[index] = response_future
        responses = tuple(response_future.result()
                          for response_future in response_futures)

        self.assertSequenceEqual(expected_responses, responses)

    def testWaitingForSomeButNotAllConcurrentFutureInvocations(self):
        pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        request = b'\x67\x68'
        expected_response = self._handler.handle_unary_unary(request, None)
        response_futures = [None] * test_constants.THREAD_CONCURRENCY
        lock = threading.Lock()
        test_is_running_cell = [True]

        def wrap_future(future):

            def wrap():
                try:
                    return future.result()
                except grpc.RpcError:
                    with lock:
                        if test_is_running_cell[0]:
                            raise
                    return None

            return wrap

        multi_callable = _unary_unary_multi_callable(self._channel)
        for index in range(test_constants.THREAD_CONCURRENCY):
            inner_response_future = multi_callable.future(
                request,
                metadata=(
                    ('test',
                     'WaitingForSomeButNotAllConcurrentFutureInvocations'),))
            outer_response_future = pool.submit(
                wrap_future(inner_response_future))
            response_futures[index] = outer_response_future

        some_completed_response_futures_iterator = itertools.islice(
            futures.as_completed(response_futures),
            test_constants.THREAD_CONCURRENCY // 2)
        for response_future in some_completed_response_futures_iterator:
            self.assertEqual(expected_response, response_future.result())
        with lock:
            test_is_running_cell[0] = False

    def testConsumingOneStreamResponseUnaryRequest(self):
        request = b'\x57\x38'

        multi_callable = _unary_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request,
            metadata=(('test', 'ConsumingOneStreamResponseUnaryRequest'),))
        next(response_iterator)

    def testConsumingSomeButNotAllStreamResponsesUnaryRequest(self):
        request = b'\x57\x38'

        multi_callable = _unary_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request,
            metadata=(
                ('test', 'ConsumingSomeButNotAllStreamResponsesUnaryRequest'),))
        for _ in range(test_constants.STREAM_LENGTH // 2):
            next(response_iterator)

    def testConsumingSomeButNotAllStreamResponsesStreamRequest(self):
        requests = tuple(b'\x67\x88'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request_iterator,
            metadata=(('test',
                       'ConsumingSomeButNotAllStreamResponsesStreamRequest'),))
        for _ in range(test_constants.STREAM_LENGTH // 2):
            next(response_iterator)

    def testConsumingTooManyStreamResponsesStreamRequest(self):
        requests = tuple(b'\x67\x88'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request_iterator,
            metadata=(
                ('test', 'ConsumingTooManyStreamResponsesStreamRequest'),))
        for _ in range(test_constants.STREAM_LENGTH):
            next(response_iterator)
        for _ in range(test_constants.STREAM_LENGTH):
            with self.assertRaises(StopIteration):
                next(response_iterator)

        self.assertIsNotNone(response_iterator.initial_metadata())
        self.assertIs(grpc.StatusCode.OK, response_iterator.code())
        self.assertIsNotNone(response_iterator.details())
        self.assertIsNotNone(response_iterator.trailing_metadata())

    def testCancelledUnaryRequestUnaryResponse(self):
        request = b'\x07\x17'

        multi_callable = _unary_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request,
                metadata=(('test', 'CancelledUnaryRequestUnaryResponse'),))
            response_future.cancel()

        self.assertTrue(response_future.cancelled())
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.result()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.exception()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.traceback()
        self.assertIs(grpc.StatusCode.CANCELLED, response_future.code())

    def testCancelledUnaryRequestStreamResponse(self):
        request = b'\x07\x19'

        multi_callable = _unary_stream_multi_callable(self._channel)
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

    def testCancelledStreamRequestUnaryResponse(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request_iterator,
                metadata=(('test', 'CancelledStreamRequestUnaryResponse'),))
            self._control.block_until_paused()
            response_future.cancel()

        self.assertTrue(response_future.cancelled())
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.result()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.exception()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.traceback()
        self.assertIsNotNone(response_future.initial_metadata())
        self.assertIs(grpc.StatusCode.CANCELLED, response_future.code())
        self.assertIsNotNone(response_future.details())
        self.assertIsNotNone(response_future.trailing_metadata())

    def testCancelledStreamRequestStreamResponse(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_stream_multi_callable(self._channel)
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

    def testExpiredUnaryRequestBlockingUnaryResponse(self):
        request = b'\x07\x17'

        multi_callable = _unary_unary_multi_callable(self._channel)
        with self._control.pause():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable.with_call(
                    request,
                    timeout=test_constants.SHORT_TIMEOUT,
                    metadata=(
                        ('test', 'ExpiredUnaryRequestBlockingUnaryResponse'),))

        self.assertIsInstance(exception_context.exception, grpc.Call)
        self.assertIsNotNone(exception_context.exception.initial_metadata())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIsNotNone(exception_context.exception.details())
        self.assertIsNotNone(exception_context.exception.trailing_metadata())

    def testExpiredUnaryRequestFutureUnaryResponse(self):
        request = b'\x07\x17'
        callback = _Callback()

        multi_callable = _unary_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request,
                timeout=test_constants.SHORT_TIMEOUT,
                metadata=(('test', 'ExpiredUnaryRequestFutureUnaryResponse'),))
            response_future.add_done_callback(callback)
            value_passed_to_callback = callback.value()

        self.assertIs(response_future, value_passed_to_callback)
        self.assertIsNotNone(response_future.initial_metadata())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED, response_future.code())
        self.assertIsNotNone(response_future.details())
        self.assertIsNotNone(response_future.trailing_metadata())
        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIsInstance(response_future.exception(), grpc.RpcError)
        self.assertIsNotNone(response_future.traceback())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      response_future.exception().code())

    def testExpiredUnaryRequestStreamResponse(self):
        request = b'\x07\x19'

        multi_callable = _unary_stream_multi_callable(self._channel)
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

    def testExpiredStreamRequestBlockingUnaryResponse(self):
        requests = tuple(b'\x07\x08'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_unary_multi_callable(self._channel)
        with self._control.pause():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable(
                    request_iterator,
                    timeout=test_constants.SHORT_TIMEOUT,
                    metadata=(
                        ('test', 'ExpiredStreamRequestBlockingUnaryResponse'),))

        self.assertIsInstance(exception_context.exception, grpc.RpcError)
        self.assertIsInstance(exception_context.exception, grpc.Call)
        self.assertIsNotNone(exception_context.exception.initial_metadata())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIsNotNone(exception_context.exception.details())
        self.assertIsNotNone(exception_context.exception.trailing_metadata())

    def testExpiredStreamRequestFutureUnaryResponse(self):
        requests = tuple(b'\x07\x18'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)
        callback = _Callback()

        multi_callable = _stream_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request_iterator,
                timeout=test_constants.SHORT_TIMEOUT,
                metadata=(('test', 'ExpiredStreamRequestFutureUnaryResponse'),))
            with self.assertRaises(grpc.FutureTimeoutError):
                response_future.result(timeout=test_constants.SHORT_TIMEOUT /
                                       2.0)
            response_future.add_done_callback(callback)
            value_passed_to_callback = callback.value()

        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED, response_future.code())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIsInstance(response_future.exception(), grpc.RpcError)
        self.assertIsNotNone(response_future.traceback())
        self.assertIs(response_future, value_passed_to_callback)
        self.assertIsNotNone(response_future.initial_metadata())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED, response_future.code())
        self.assertIsNotNone(response_future.details())
        self.assertIsNotNone(response_future.trailing_metadata())

    def testExpiredStreamRequestStreamResponse(self):
        requests = tuple(b'\x67\x18'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_stream_multi_callable(self._channel)
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

    def testFailedUnaryRequestBlockingUnaryResponse(self):
        request = b'\x37\x17'

        multi_callable = _unary_unary_multi_callable(self._channel)
        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable.with_call(
                    request,
                    metadata=(
                        ('test', 'FailedUnaryRequestBlockingUnaryResponse'),))

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())

    def testFailedUnaryRequestFutureUnaryResponse(self):
        request = b'\x37\x17'
        callback = _Callback()

        multi_callable = _unary_unary_multi_callable(self._channel)
        with self._control.fail():
            response_future = multi_callable.future(
                request,
                metadata=(('test', 'FailedUnaryRequestFutureUnaryResponse'),))
            response_future.add_done_callback(callback)
            value_passed_to_callback = callback.value()

        self.assertIsInstance(response_future, grpc.Future)
        self.assertIsInstance(response_future, grpc.Call)
        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()
        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())
        self.assertIsInstance(response_future.exception(), grpc.RpcError)
        self.assertIsNotNone(response_future.traceback())
        self.assertIs(grpc.StatusCode.UNKNOWN,
                      response_future.exception().code())
        self.assertIs(response_future, value_passed_to_callback)

    def testFailedUnaryRequestStreamResponse(self):
        request = b'\x37\x17'

        multi_callable = _unary_stream_multi_callable(self._channel)
        with self.assertRaises(grpc.RpcError) as exception_context:
            with self._control.fail():
                response_iterator = multi_callable(
                    request,
                    metadata=(('test', 'FailedUnaryRequestStreamResponse'),))
                next(response_iterator)

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())

    def testFailedStreamRequestBlockingUnaryResponse(self):
        requests = tuple(b'\x47\x58'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_unary_multi_callable(self._channel)
        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable(
                    request_iterator,
                    metadata=(
                        ('test', 'FailedStreamRequestBlockingUnaryResponse'),))

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())

    def testFailedStreamRequestFutureUnaryResponse(self):
        requests = tuple(b'\x07\x18'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)
        callback = _Callback()

        multi_callable = _stream_unary_multi_callable(self._channel)
        with self._control.fail():
            response_future = multi_callable.future(
                request_iterator,
                metadata=(('test', 'FailedStreamRequestFutureUnaryResponse'),))
            response_future.add_done_callback(callback)
            value_passed_to_callback = callback.value()

        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()
        self.assertIs(grpc.StatusCode.UNKNOWN, response_future.code())
        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())
        self.assertIsInstance(response_future.exception(), grpc.RpcError)
        self.assertIsNotNone(response_future.traceback())
        self.assertIs(response_future, value_passed_to_callback)

    def testFailedStreamRequestStreamResponse(self):
        requests = tuple(b'\x67\x88'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_stream_multi_callable(self._channel)
        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_iterator = multi_callable(
                    request_iterator,
                    metadata=(('test', 'FailedStreamRequestStreamResponse'),))
                tuple(response_iterator)

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())
        self.assertIs(grpc.StatusCode.UNKNOWN, response_iterator.code())

    def testIgnoredUnaryRequestFutureUnaryResponse(self):
        request = b'\x37\x17'

        multi_callable = _unary_unary_multi_callable(self._channel)
        multi_callable.future(
            request,
            metadata=(('test', 'IgnoredUnaryRequestFutureUnaryResponse'),))

    def testIgnoredUnaryRequestStreamResponse(self):
        request = b'\x37\x17'

        multi_callable = _unary_stream_multi_callable(self._channel)
        multi_callable(
            request, metadata=(('test', 'IgnoredUnaryRequestStreamResponse'),))

    def testIgnoredStreamRequestFutureUnaryResponse(self):
        requests = tuple(b'\x07\x18'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_unary_multi_callable(self._channel)
        multi_callable.future(
            request_iterator,
            metadata=(('test', 'IgnoredStreamRequestFutureUnaryResponse'),))

    def testIgnoredStreamRequestStreamResponse(self):
        requests = tuple(b'\x67\x88'
                         for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = _stream_stream_multi_callable(self._channel)
        multi_callable(
            request_iterator,
            metadata=(('test', 'IgnoredStreamRequestStreamResponse'),))


if __name__ == '__main__':
    unittest.main(verbosity=2)
