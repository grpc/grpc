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
"""Test of RPCs made against gRPC Python's application-layer API."""

import itertools
import threading
import unittest
import logging
from concurrent import futures

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit._rpc_test_helpers import (
    TIMEOUT_SHORT, Callback, unary_unary_multi_callable,
    unary_stream_multi_callable, unary_stream_non_blocking_multi_callable,
    stream_unary_multi_callable, stream_stream_multi_callable,
    stream_stream_non_blocking_multi_callable, BaseRPCTest)
from tests.unit.framework.common import test_constants


class RPCPart2Test(BaseRPCTest, unittest.TestCase):

    def testDefaultThreadPoolIsUsed(self):
        self._consume_one_stream_response_unary_request(
            unary_stream_multi_callable(self._channel))
        self.assertFalse(self._thread_pool.was_used())

    def testExperimentalThreadPoolIsUsed(self):
        self._consume_one_stream_response_unary_request(
            unary_stream_non_blocking_multi_callable(self._channel))
        self.assertTrue(self._thread_pool.was_used())

    def testUnrecognizedMethod(self):
        request = b'abc'

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary('NoSuchMethod')(request)

        self.assertEqual(grpc.StatusCode.UNIMPLEMENTED,
                         exception_context.exception.code())

    def testSuccessfulUnaryRequestBlockingUnaryResponse(self):
        request = b'\x07\x08'
        expected_response = self._handler.handle_unary_unary(request, None)

        multi_callable = unary_unary_multi_callable(self._channel)
        response = multi_callable(
            request,
            metadata=(('test', 'SuccessfulUnaryRequestBlockingUnaryResponse'),))

        self.assertEqual(expected_response, response)

    def testSuccessfulUnaryRequestBlockingUnaryResponseWithCall(self):
        request = b'\x07\x08'
        expected_response = self._handler.handle_unary_unary(request, None)

        multi_callable = unary_unary_multi_callable(self._channel)
        response, call = multi_callable.with_call(
            request,
            metadata=(('test',
                       'SuccessfulUnaryRequestBlockingUnaryResponseWithCall'),))

        self.assertEqual(expected_response, response)
        self.assertIs(grpc.StatusCode.OK, call.code())
        self.assertEqual('', call.debug_error_string())

    def testSuccessfulUnaryRequestFutureUnaryResponse(self):
        request = b'\x07\x08'
        expected_response = self._handler.handle_unary_unary(request, None)

        multi_callable = unary_unary_multi_callable(self._channel)
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

        multi_callable = unary_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            request,
            metadata=(('test', 'SuccessfulUnaryRequestStreamResponse'),))
        responses = tuple(response_iterator)

        self.assertSequenceEqual(expected_responses, responses)

    def testSuccessfulStreamRequestBlockingUnaryResponse(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        request_iterator = iter(requests)

        multi_callable = stream_unary_multi_callable(self._channel)
        response = multi_callable(
            request_iterator,
            metadata=(('test',
                       'SuccessfulStreamRequestBlockingUnaryResponse'),))

        self.assertEqual(expected_response, response)

    def testSuccessfulStreamRequestBlockingUnaryResponseWithCall(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        request_iterator = iter(requests)

        multi_callable = stream_unary_multi_callable(self._channel)
        response, call = multi_callable.with_call(
            request_iterator,
            metadata=(
                ('test',
                 'SuccessfulStreamRequestBlockingUnaryResponseWithCall'),))

        self.assertEqual(expected_response, response)
        self.assertIs(grpc.StatusCode.OK, call.code())

    def testSuccessfulStreamRequestFutureUnaryResponse(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        request_iterator = iter(requests)

        multi_callable = stream_unary_multi_callable(self._channel)
        response_future = multi_callable.future(
            request_iterator,
            metadata=(('test', 'SuccessfulStreamRequestFutureUnaryResponse'),))
        response = response_future.result()

        self.assertEqual(expected_response, response)
        self.assertIsNone(response_future.exception())
        self.assertIsNone(response_future.traceback())

    def testSuccessfulStreamRequestStreamResponse(self):
        requests = tuple(
            b'\x77\x58' for _ in range(test_constants.STREAM_LENGTH))

        expected_responses = tuple(
            self._handler.handle_stream_stream(iter(requests), None))
        request_iterator = iter(requests)

        multi_callable = stream_stream_multi_callable(self._channel)
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

        multi_callable = unary_unary_multi_callable(self._channel)
        first_response = multi_callable(first_request,
                                        metadata=(('test',
                                                   'SequentialInvocations'),))
        second_response = multi_callable(second_request,
                                         metadata=(('test',
                                                    'SequentialInvocations'),))

        self.assertEqual(expected_first_response, first_response)
        self.assertEqual(expected_second_response, second_response)

    def testConcurrentBlockingInvocations(self):
        pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        expected_responses = [expected_response
                             ] * test_constants.THREAD_CONCURRENCY
        response_futures = [None] * test_constants.THREAD_CONCURRENCY

        multi_callable = stream_unary_multi_callable(self._channel)
        for index in range(test_constants.THREAD_CONCURRENCY):
            request_iterator = iter(requests)
            response_future = pool.submit(
                multi_callable,
                request_iterator,
                metadata=(('test', 'ConcurrentBlockingInvocations'),))
            response_futures[index] = response_future
        responses = tuple(
            response_future.result() for response_future in response_futures)

        pool.shutdown(wait=True)
        self.assertSequenceEqual(expected_responses, responses)

    def testConcurrentFutureInvocations(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        expected_response = self._handler.handle_stream_unary(
            iter(requests), None)
        expected_responses = [expected_response
                             ] * test_constants.THREAD_CONCURRENCY
        response_futures = [None] * test_constants.THREAD_CONCURRENCY

        multi_callable = stream_unary_multi_callable(self._channel)
        for index in range(test_constants.THREAD_CONCURRENCY):
            request_iterator = iter(requests)
            response_future = multi_callable.future(
                request_iterator,
                metadata=(('test', 'ConcurrentFutureInvocations'),))
            response_futures[index] = response_future
        responses = tuple(
            response_future.result() for response_future in response_futures)

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

        multi_callable = unary_unary_multi_callable(self._channel)
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
        self._consume_one_stream_response_unary_request(
            unary_stream_multi_callable(self._channel))

    def testConsumingOneStreamResponseUnaryRequestNonBlocking(self):
        self._consume_one_stream_response_unary_request(
            unary_stream_non_blocking_multi_callable(self._channel))

    def testConsumingSomeButNotAllStreamResponsesUnaryRequest(self):
        self._consume_some_but_not_all_stream_responses_unary_request(
            unary_stream_multi_callable(self._channel))

    def testConsumingSomeButNotAllStreamResponsesUnaryRequestNonBlocking(self):
        self._consume_some_but_not_all_stream_responses_unary_request(
            unary_stream_non_blocking_multi_callable(self._channel))

    def testConsumingSomeButNotAllStreamResponsesStreamRequest(self):
        self._consume_some_but_not_all_stream_responses_stream_request(
            stream_stream_multi_callable(self._channel))

    def testConsumingSomeButNotAllStreamResponsesStreamRequestNonBlocking(self):
        self._consume_some_but_not_all_stream_responses_stream_request(
            stream_stream_non_blocking_multi_callable(self._channel))

    def testConsumingTooManyStreamResponsesStreamRequest(self):
        self._consume_too_many_stream_responses_stream_request(
            stream_stream_multi_callable(self._channel))

    def testConsumingTooManyStreamResponsesStreamRequestNonBlocking(self):
        self._consume_too_many_stream_responses_stream_request(
            stream_stream_non_blocking_multi_callable(self._channel))

    def testCancelledUnaryRequestUnaryResponse(self):
        request = b'\x07\x17'

        multi_callable = unary_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request,
                metadata=(('test', 'CancelledUnaryRequestUnaryResponse'),))
            response_future.cancel()

        self.assertIs(grpc.StatusCode.CANCELLED, response_future.code())
        self.assertTrue(response_future.cancelled())
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.result()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.exception()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.traceback()

    def testCancelledUnaryRequestStreamResponse(self):
        self._cancelled_unary_request_stream_response(
            unary_stream_multi_callable(self._channel))

    def testCancelledUnaryRequestStreamResponseNonBlocking(self):
        self._cancelled_unary_request_stream_response(
            unary_stream_non_blocking_multi_callable(self._channel))

    def testCancelledStreamRequestUnaryResponse(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = stream_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request_iterator,
                metadata=(('test', 'CancelledStreamRequestUnaryResponse'),))
            self._control.block_until_paused()
            response_future.cancel()

        self.assertIs(grpc.StatusCode.CANCELLED, response_future.code())
        self.assertTrue(response_future.cancelled())
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.result()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.exception()
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.traceback()
        self.assertIsNotNone(response_future.initial_metadata())
        self.assertIsNotNone(response_future.details())
        self.assertIsNotNone(response_future.trailing_metadata())

    def testCancelledStreamRequestStreamResponse(self):
        self._cancelled_stream_request_stream_response(
            stream_stream_multi_callable(self._channel))

    def testCancelledStreamRequestStreamResponseNonBlocking(self):
        self._cancelled_stream_request_stream_response(
            stream_stream_non_blocking_multi_callable(self._channel))

    def testExpiredUnaryRequestBlockingUnaryResponse(self):
        request = b'\x07\x17'

        multi_callable = unary_unary_multi_callable(self._channel)
        with self._control.pause():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable.with_call(
                    request,
                    timeout=TIMEOUT_SHORT,
                    metadata=(('test',
                               'ExpiredUnaryRequestBlockingUnaryResponse'),))

        self.assertIsInstance(exception_context.exception, grpc.Call)
        self.assertIsNotNone(exception_context.exception.initial_metadata())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIsNotNone(exception_context.exception.details())
        self.assertIsNotNone(exception_context.exception.trailing_metadata())

    def testExpiredUnaryRequestFutureUnaryResponse(self):
        request = b'\x07\x17'
        callback = Callback()

        multi_callable = unary_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request,
                timeout=TIMEOUT_SHORT,
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
        self._expired_unary_request_stream_response(
            unary_stream_multi_callable(self._channel))

    def testExpiredUnaryRequestStreamResponseNonBlocking(self):
        self._expired_unary_request_stream_response(
            unary_stream_non_blocking_multi_callable(self._channel))


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
