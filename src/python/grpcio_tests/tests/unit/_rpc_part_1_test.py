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


class RPCPart1Test(BaseRPCTest, unittest.TestCase):

    def testExpiredStreamRequestBlockingUnaryResponse(self):
        requests = tuple(
            b'\x07\x08' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = stream_unary_multi_callable(self._channel)
        with self._control.pause():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable(
                    request_iterator,
                    timeout=TIMEOUT_SHORT,
                    metadata=(('test',
                               'ExpiredStreamRequestBlockingUnaryResponse'),))

        self.assertIsInstance(exception_context.exception, grpc.RpcError)
        self.assertIsInstance(exception_context.exception, grpc.Call)
        self.assertIsNotNone(exception_context.exception.initial_metadata())
        self.assertIs(grpc.StatusCode.DEADLINE_EXCEEDED,
                      exception_context.exception.code())
        self.assertIsNotNone(exception_context.exception.details())
        self.assertIsNotNone(exception_context.exception.trailing_metadata())

    def testExpiredStreamRequestFutureUnaryResponse(self):
        requests = tuple(
            b'\x07\x18' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)
        callback = Callback()

        multi_callable = stream_unary_multi_callable(self._channel)
        with self._control.pause():
            response_future = multi_callable.future(
                request_iterator,
                timeout=TIMEOUT_SHORT,
                metadata=(('test', 'ExpiredStreamRequestFutureUnaryResponse'),))
            with self.assertRaises(grpc.FutureTimeoutError):
                response_future.result(timeout=TIMEOUT_SHORT / 2.0)
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
        self._expired_stream_request_stream_response(
            stream_stream_multi_callable(self._channel))

    def testExpiredStreamRequestStreamResponseNonBlocking(self):
        self._expired_stream_request_stream_response(
            stream_stream_non_blocking_multi_callable(self._channel))

    def testFailedUnaryRequestBlockingUnaryResponse(self):
        request = b'\x37\x17'

        multi_callable = unary_unary_multi_callable(self._channel)
        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable.with_call(
                    request,
                    metadata=(('test',
                               'FailedUnaryRequestBlockingUnaryResponse'),))

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())
        # sanity checks on to make sure returned string contains default members
        # of the error
        debug_error_string = exception_context.exception.debug_error_string()
        self.assertIn('created', debug_error_string)
        self.assertIn('description', debug_error_string)
        self.assertIn('file', debug_error_string)
        self.assertIn('file_line', debug_error_string)

    def testFailedUnaryRequestFutureUnaryResponse(self):
        request = b'\x37\x17'
        callback = Callback()

        multi_callable = unary_unary_multi_callable(self._channel)
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
        self._failed_unary_request_stream_response(
            unary_stream_multi_callable(self._channel))

    def testFailedUnaryRequestStreamResponseNonBlocking(self):
        self._failed_unary_request_stream_response(
            unary_stream_non_blocking_multi_callable(self._channel))

    def testFailedStreamRequestBlockingUnaryResponse(self):
        requests = tuple(
            b'\x47\x58' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = stream_unary_multi_callable(self._channel)
        with self._control.fail():
            with self.assertRaises(grpc.RpcError) as exception_context:
                multi_callable(
                    request_iterator,
                    metadata=(('test',
                               'FailedStreamRequestBlockingUnaryResponse'),))

        self.assertIs(grpc.StatusCode.UNKNOWN,
                      exception_context.exception.code())

    def testFailedStreamRequestFutureUnaryResponse(self):
        requests = tuple(
            b'\x07\x18' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)
        callback = Callback()

        multi_callable = stream_unary_multi_callable(self._channel)
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
        self._failed_stream_request_stream_response(
            stream_stream_multi_callable(self._channel))

    def testFailedStreamRequestStreamResponseNonBlocking(self):
        self._failed_stream_request_stream_response(
            stream_stream_non_blocking_multi_callable(self._channel))

    def testIgnoredUnaryRequestFutureUnaryResponse(self):
        request = b'\x37\x17'

        multi_callable = unary_unary_multi_callable(self._channel)
        multi_callable.future(
            request,
            metadata=(('test', 'IgnoredUnaryRequestFutureUnaryResponse'),))

    def testIgnoredUnaryRequestStreamResponse(self):
        self._ignored_unary_stream_request_future_unary_response(
            unary_stream_multi_callable(self._channel))

    def testIgnoredUnaryRequestStreamResponseNonBlocking(self):
        self._ignored_unary_stream_request_future_unary_response(
            unary_stream_non_blocking_multi_callable(self._channel))

    def testIgnoredStreamRequestFutureUnaryResponse(self):
        requests = tuple(
            b'\x07\x18' for _ in range(test_constants.STREAM_LENGTH))
        request_iterator = iter(requests)

        multi_callable = stream_unary_multi_callable(self._channel)
        multi_callable.future(
            request_iterator,
            metadata=(('test', 'IgnoredStreamRequestFutureUnaryResponse'),))

    def testIgnoredStreamRequestStreamResponse(self):
        self._ignored_stream_request_stream_response(
            stream_stream_multi_callable(self._channel))

    def testIgnoredStreamRequestStreamResponseNonBlocking(self):
        self._ignored_stream_request_stream_response(
            stream_stream_non_blocking_multi_callable(self._channel))


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
