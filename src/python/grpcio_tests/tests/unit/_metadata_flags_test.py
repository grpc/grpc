from google3.testing.pybase import googletest
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
"""Tests of grpc.channel_ready_future."""

import time
import weakref
import unittest
import threading

import grpc

from grpc.tests.unit import test_common
from grpc.tests.unit.framework.common import test_constants

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'

_ALL_CALL_CASES = [
    ('unary_unary', _UNARY_UNARY, False, '__call__', False),
    ('unary_unary', _UNARY_UNARY, False, 'with_call', False),
    ('unary_unary', _UNARY_UNARY, False, 'future', False),
    ('unary_stream', _UNARY_STREAM, False, '__call__', True),
    ('stream_unary', _STREAM_UNARY, True, '__call__', False),
    ('stream_unary', _STREAM_UNARY, True, 'with_call', False),
    ('stream_unary', _STREAM_UNARY, True, 'future', False),
    ('stream_stream', _STREAM_STREAM, True, '__call__', True),
]

# Only with unary type input can fail fast, otherwise
#   C-Core will wait and can't be disabled.
_FAIL_FAST_CASES = _ALL_CALL_CASES[:4]


def handle_unary_unary(test, request, servicer_context):
    return _RESPONSE


def handle_unary_stream(test, request, servicer_context):
    for _ in range(test_constants.STREAM_LENGTH):
        yield _RESPONSE


def handle_stream_unary(test, request_iterator, servicer_context):
    for _ in request_iterator:
        pass
    return _RESPONSE


def handle_stream_stream(test, request_iterator, servicer_context):
    for _ in request_iterator:
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


def execute_call(channel,
                 method_name,
                 server_method,
                 is_stream_input,
                 invoke_method,
                 is_stream_output,
                 wait_for_ready=None):
    multi_callable = getattr(channel, method_name)(server_method)
    if not is_stream_input:
        req = _REQUEST
    else:
        req = iter([_REQUEST] * test_constants.STREAM_LENGTH)
    response = getattr(multi_callable, invoke_method)(
        req,
        timeout=test_constants.SHORT_TIMEOUT,
        wait_for_ready=wait_for_ready)
    if is_stream_output:
        for _ in response:
            pass
    if invoke_method == 'future':
        response = response.result(timeout=test_constants.SHORT_TIMEOUT)
    return response


class MetadataFlagsTest(unittest.TestCase):

    def test_channel_wait_for_ready_disabled(self):
        channel = grpc.insecure_channel('localhost:12345', wait_for_ready=False)

        def test_call(*args):
            try:
                execute_call(channel, *args)
            except grpc.RpcError as rpc_error:
                self.assertIn(rpc_error.message, 'StatusCode.UNAVAILABLE')
            except Exception as exception:
                raise exception

        for case in _FAIL_FAST_CASES:
            test_call(*case)

    def test_call_wait_for_ready_disabled(self):
        channel = grpc.insecure_channel('localhost:42424', wait_for_ready=True)

        def test_call(*args):
            try:
                execute_call(channel, *args, wait_for_ready=False)
            except grpc.RpcError as rpc_error:
                self.assertIn(rpc_error.message, 'StatusCode.UNAVAILABLE')
            except Exception as exception:
                raise exception

        for case in _FAIL_FAST_CASES:
            test_call(*case)

    def test_channel_wait_for_ready_enabled(self):
        # To test the wait mechanism, Python thread is required to make
        #   client set up first without handling them case by case.
        # Also, Python thread don't pass the unhandled exceptions to
        #   main thread. So, it need another method to store the
        #   exceptions and raise them again in main thread.
        self.unhandled_exceptions = []

        def test_call(method_name, server_method, is_stream_input,
                      invoke_method, is_stream_output):
            try:
                # Pre-initiated server port will not fail "wait for ready"
                #   properly, because the channel can be connected. So,
                #   even if the "wait for ready" is not turned on, the
                #   server's default behavior will make the requests
                #   wait for response forever.
                channel = grpc.insecure_channel(
                    'localhost:54321', wait_for_ready=True)
                multi_callable = getattr(channel, method_name)(server_method)
                if not is_stream_input:
                    req = _REQUEST
                else:
                    req = iter([_REQUEST] * test_constants.STREAM_LENGTH)
                response = getattr(multi_callable, invoke_method)(
                    req, timeout=test_constants.SHORT_TIMEOUT)
                if is_stream_output:
                    for _ in response:
                        pass
                if invoke_method == 'future':
                    response = response.result(
                        timeout=test_constants.SHORT_TIMEOUT)
            except Exception as exception:
                self.unhandled_exceptions.append(exception)

        test_threads = []
        for case in _ALL_CALL_CASES:
            test_thread = threading.Thread(target=test_call, args=case)
            test_thread.start()
            test_threads.append(test_thread)

        # Start the server after the connections are waiting
        time.sleep(1)
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers((_GenericHandler(
            weakref.proxy(self)),))
        self._server.add_insecure_port('[::]:54321')
        self._server.start()

        for test_thread in test_threads:
            test_thread.join()

        if len(self.unhandled_exceptions) != 0:
            for exception in self.unhandled_exceptions:
                raise exception


if __name__ == '__main__':
    googletest.main(verbosity=2)
