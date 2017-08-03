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
"""Tests server responding with RESOURCE_EXHAUSTED."""

import threading
import unittest

import grpc
from grpc import _channel
from grpc.framework.foundation import logging_pool

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'


class _TestTrigger(object):

    def __init__(self, total_call_count):
        self._total_call_count = total_call_count
        self._pending_calls = 0
        self._triggered = False
        self._finish_condition = threading.Condition()
        self._start_condition = threading.Condition()

    # Wait for all calls be be blocked in their handler
    def await_calls(self):
        with self._start_condition:
            while self._pending_calls < self._total_call_count:
                self._start_condition.wait()

    # Block in a response handler and wait for a trigger
    def await_trigger(self):
        with self._start_condition:
            self._pending_calls += 1
            self._start_condition.notify()

        with self._finish_condition:
            if not self._triggered:
                self._finish_condition.wait()

    # Finish all response handlers
    def trigger(self):
        with self._finish_condition:
            self._triggered = True
            self._finish_condition.notify_all()


def handle_unary_unary(trigger, request, servicer_context):
    trigger.await_trigger()
    return _RESPONSE


def handle_unary_stream(trigger, request, servicer_context):
    trigger.await_trigger()
    for _ in range(test_constants.STREAM_LENGTH):
        yield _RESPONSE


def handle_stream_unary(trigger, request_iterator, servicer_context):
    trigger.await_trigger()
    # TODO(issue:#6891) We should be able to remove this loop
    for request in request_iterator:
        pass
    return _RESPONSE


def handle_stream_stream(trigger, request_iterator, servicer_context):
    trigger.await_trigger()
    # TODO(issue:#6891) We should be able to remove this loop,
    # and replace with return; yield
    for request in request_iterator:
        yield _RESPONSE


class _MethodHandler(grpc.RpcMethodHandler):

    def __init__(self, trigger, request_streaming, response_streaming):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self.stream_stream = None
        if self.request_streaming and self.response_streaming:
            self.stream_stream = (
                lambda x, y: handle_stream_stream(trigger, x, y))
        elif self.request_streaming:
            self.stream_unary = lambda x, y: handle_stream_unary(trigger, x, y)
        elif self.response_streaming:
            self.unary_stream = lambda x, y: handle_unary_stream(trigger, x, y)
        else:
            self.unary_unary = lambda x, y: handle_unary_unary(trigger, x, y)


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self, trigger):
        self._trigger = trigger

    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(self._trigger, False, False)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(self._trigger, False, True)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(self._trigger, True, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(self._trigger, True, True)
        else:
            return None


class ResourceExhaustedTest(unittest.TestCase):

    def setUp(self):
        self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        self._trigger = _TestTrigger(test_constants.THREAD_CONCURRENCY)
        self._server = grpc.server(
            self._server_pool,
            handlers=(_GenericHandler(self._trigger),),
            maximum_concurrent_rpcs=test_constants.THREAD_CONCURRENCY)
        port = self._server.add_insecure_port('[::]:0')
        self._server.start()
        self._channel = grpc.insecure_channel('localhost:%d' % port)

    def tearDown(self):
        self._server.stop(0)

    def testUnaryUnary(self):
        multi_callable = self._channel.unary_unary(_UNARY_UNARY)
        futures = []
        for _ in range(test_constants.THREAD_CONCURRENCY):
            futures.append(multi_callable.future(_REQUEST))

        self._trigger.await_calls()

        with self.assertRaises(grpc.RpcError) as exception_context:
            multi_callable(_REQUEST)

        self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED,
                         exception_context.exception.code())

        future_exception = multi_callable.future(_REQUEST)
        self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED,
                         future_exception.exception().code())

        self._trigger.trigger()
        for future in futures:
            self.assertEqual(_RESPONSE, future.result())

        # Ensure a new request can be handled
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))

    def testUnaryStream(self):
        multi_callable = self._channel.unary_stream(_UNARY_STREAM)
        calls = []
        for _ in range(test_constants.THREAD_CONCURRENCY):
            calls.append(multi_callable(_REQUEST))

        self._trigger.await_calls()

        with self.assertRaises(grpc.RpcError) as exception_context:
            next(multi_callable(_REQUEST))

        self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED,
                         exception_context.exception.code())

        self._trigger.trigger()

        for call in calls:
            for response in call:
                self.assertEqual(_RESPONSE, response)

        # Ensure a new request can be handled
        new_call = multi_callable(_REQUEST)
        for response in new_call:
            self.assertEqual(_RESPONSE, response)

    def testStreamUnary(self):
        multi_callable = self._channel.stream_unary(_STREAM_UNARY)
        futures = []
        request = iter([_REQUEST] * test_constants.STREAM_LENGTH)
        for _ in range(test_constants.THREAD_CONCURRENCY):
            futures.append(multi_callable.future(request))

        self._trigger.await_calls()

        with self.assertRaises(grpc.RpcError) as exception_context:
            multi_callable(request)

        self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED,
                         exception_context.exception.code())

        future_exception = multi_callable.future(request)
        self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED,
                         future_exception.exception().code())

        self._trigger.trigger()

        for future in futures:
            self.assertEqual(_RESPONSE, future.result())

        # Ensure a new request can be handled
        self.assertEqual(_RESPONSE, multi_callable(request))

    def testStreamStream(self):
        multi_callable = self._channel.stream_stream(_STREAM_STREAM)
        calls = []
        request = iter([_REQUEST] * test_constants.STREAM_LENGTH)
        for _ in range(test_constants.THREAD_CONCURRENCY):
            calls.append(multi_callable(request))

        self._trigger.await_calls()

        with self.assertRaises(grpc.RpcError) as exception_context:
            next(multi_callable(request))

        self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED,
                         exception_context.exception.code())

        self._trigger.trigger()

        for call in calls:
            for response in call:
                self.assertEqual(_RESPONSE, response)

        # Ensure a new request can be handled
        new_call = multi_callable(request)
        for response in new_call:
            self.assertEqual(_RESPONSE, response)


if __name__ == '__main__':
    unittest.main(verbosity=2)
