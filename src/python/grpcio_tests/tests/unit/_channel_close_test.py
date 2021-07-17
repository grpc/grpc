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
"""Tests server and client side compression."""

import itertools
import logging
import threading
import time
import unittest

import grpc

from tests.unit import test_common
from tests.unit.framework.common import test_constants

_BEAT = 0.5
_SOME_TIME = 5
_MORE_TIME = 10

_STREAM_URI = 'Meffod'
_UNARY_URI = 'MeffodMan'


class _StreamingMethodHandler(grpc.RpcMethodHandler):

    request_streaming = True
    response_streaming = True
    request_deserializer = None
    response_serializer = None

    def stream_stream(self, request_iterator, servicer_context):
        for request in request_iterator:
            yield request * 2


class _UnaryMethodHandler(grpc.RpcMethodHandler):

    request_streaming = False
    response_streaming = False
    request_deserializer = None
    response_serializer = None

    def unary_unary(self, request, servicer_context):
        return request * 2


_STREAMING_METHOD_HANDLER = _StreamingMethodHandler()
_UNARY_METHOD_HANDLER = _UnaryMethodHandler()


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        if handler_call_details.method == _STREAM_URI:
            return _STREAMING_METHOD_HANDLER
        else:
            return _UNARY_METHOD_HANDLER


_GENERIC_HANDLER = _GenericHandler()


class _Pipe(object):

    def __init__(self, values):
        self._condition = threading.Condition()
        self._values = list(values)
        self._open = True

    def __iter__(self):
        return self

    def _next(self):
        with self._condition:
            while not self._values and self._open:
                self._condition.wait()
            if self._values:
                return self._values.pop(0)
            else:
                raise StopIteration()

    def next(self):
        return self._next()

    def __next__(self):
        return self._next()

    def add(self, value):
        with self._condition:
            self._values.append(value)
            self._condition.notify()

    def close(self):
        with self._condition:
            self._open = False
            self._condition.notify()

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self.close()


class ChannelCloseTest(unittest.TestCase):

    def setUp(self):
        self._server = test_common.test_server(
            max_workers=test_constants.THREAD_CONCURRENCY)
        self._server.add_generic_rpc_handlers((_GENERIC_HANDLER,))
        self._port = self._server.add_insecure_port('[::]:0')
        self._server.start()

    def tearDown(self):
        self._server.stop(None)

    def test_close_immediately_after_call_invocation(self):
        channel = grpc.insecure_channel('localhost:{}'.format(self._port))
        multi_callable = channel.stream_stream(_STREAM_URI)
        request_iterator = _Pipe(())
        response_iterator = multi_callable(request_iterator)
        channel.close()
        request_iterator.close()

        self.assertIs(response_iterator.code(), grpc.StatusCode.CANCELLED)

    def test_close_while_call_active(self):
        channel = grpc.insecure_channel('localhost:{}'.format(self._port))
        multi_callable = channel.stream_stream(_STREAM_URI)
        request_iterator = _Pipe((b'abc',))
        response_iterator = multi_callable(request_iterator)
        next(response_iterator)
        channel.close()
        request_iterator.close()

        self.assertIs(response_iterator.code(), grpc.StatusCode.CANCELLED)

    def test_context_manager_close_while_call_active(self):
        with grpc.insecure_channel('localhost:{}'.format(
                self._port)) as channel:  # pylint: disable=bad-continuation
            multi_callable = channel.stream_stream(_STREAM_URI)
            request_iterator = _Pipe((b'abc',))
            response_iterator = multi_callable(request_iterator)
            next(response_iterator)
        request_iterator.close()

        self.assertIs(response_iterator.code(), grpc.StatusCode.CANCELLED)

    def test_context_manager_close_while_many_calls_active(self):
        with grpc.insecure_channel('localhost:{}'.format(
                self._port)) as channel:  # pylint: disable=bad-continuation
            multi_callable = channel.stream_stream(_STREAM_URI)
            request_iterators = tuple(
                _Pipe((b'abc',))
                for _ in range(test_constants.THREAD_CONCURRENCY))
            response_iterators = []
            for request_iterator in request_iterators:
                response_iterator = multi_callable(request_iterator)
                next(response_iterator)
                response_iterators.append(response_iterator)
        for request_iterator in request_iterators:
            request_iterator.close()

        for response_iterator in response_iterators:
            self.assertIs(response_iterator.code(), grpc.StatusCode.CANCELLED)

    def test_many_concurrent_closes(self):
        channel = grpc.insecure_channel('localhost:{}'.format(self._port))
        multi_callable = channel.stream_stream(_STREAM_URI)
        request_iterator = _Pipe((b'abc',))
        response_iterator = multi_callable(request_iterator)
        next(response_iterator)
        start = time.time()
        end = start + _MORE_TIME

        def sleep_some_time_then_close():
            time.sleep(_SOME_TIME)
            channel.close()

        for _ in range(test_constants.THREAD_CONCURRENCY):
            close_thread = threading.Thread(target=sleep_some_time_then_close)
            close_thread.start()
        while True:
            request_iterator.add(b'def')
            time.sleep(_BEAT)
            if end < time.time():
                break
        request_iterator.close()

        self.assertIs(response_iterator.code(), grpc.StatusCode.CANCELLED)

    def test_exception_in_callback(self):
        with grpc.insecure_channel('localhost:{}'.format(
                self._port)) as channel:
            stream_multi_callable = channel.stream_stream(_STREAM_URI)
            endless_iterator = itertools.repeat(b'abc')
            stream_response_iterator = stream_multi_callable(endless_iterator)
            future = channel.unary_unary(_UNARY_URI).future(b'abc')

            def on_done_callback(future):
                raise Exception("This should not cause a deadlock.")

            future.add_done_callback(on_done_callback)
            future.result()


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
