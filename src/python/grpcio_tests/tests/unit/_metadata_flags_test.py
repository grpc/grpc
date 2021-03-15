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
"""Tests metadata flags feature by testing wait-for-ready semantics"""

import time
import weakref
import unittest
import threading
import logging
import socket
from six.moves import queue

import grpc

from tests.unit import test_common
from tests.unit.framework.common import test_constants
import tests.unit.framework.common
from tests.unit.framework.common import get_socket

_UNARY_UNARY = '/test/UnaryUnary'
_UNARY_STREAM = '/test/UnaryStream'
_STREAM_UNARY = '/test/StreamUnary'
_STREAM_STREAM = '/test/StreamStream'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'


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
            self.stream_stream = lambda req, ctx: handle_stream_stream(
                test, req, ctx)
        elif self.request_streaming:
            self.stream_unary = lambda req, ctx: handle_stream_unary(
                test, req, ctx)
        elif self.response_streaming:
            self.unary_stream = lambda req, ctx: handle_unary_stream(
                test, req, ctx)
        else:
            self.unary_unary = lambda req, ctx: handle_unary_unary(
                test, req, ctx)


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


def create_phony_channel():
    """Creating phony channels is a workaround for retries"""
    host, port, sock = get_socket(sock_options=(socket.SO_REUSEADDR,))
    sock.close()
    return grpc.insecure_channel('{}:{}'.format(host, port))


def perform_unary_unary_call(channel, wait_for_ready=None):
    channel.unary_unary(_UNARY_UNARY).__call__(
        _REQUEST,
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready)


def perform_unary_unary_with_call(channel, wait_for_ready=None):
    channel.unary_unary(_UNARY_UNARY).with_call(
        _REQUEST,
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready)


def perform_unary_unary_future(channel, wait_for_ready=None):
    channel.unary_unary(_UNARY_UNARY).future(
        _REQUEST,
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready).result(
            timeout=test_constants.LONG_TIMEOUT)


def perform_unary_stream_call(channel, wait_for_ready=None):
    response_iterator = channel.unary_stream(_UNARY_STREAM).__call__(
        _REQUEST,
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready)
    for _ in response_iterator:
        pass


def perform_stream_unary_call(channel, wait_for_ready=None):
    channel.stream_unary(_STREAM_UNARY).__call__(
        iter([_REQUEST] * test_constants.STREAM_LENGTH),
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready)


def perform_stream_unary_with_call(channel, wait_for_ready=None):
    channel.stream_unary(_STREAM_UNARY).with_call(
        iter([_REQUEST] * test_constants.STREAM_LENGTH),
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready)


def perform_stream_unary_future(channel, wait_for_ready=None):
    channel.stream_unary(_STREAM_UNARY).future(
        iter([_REQUEST] * test_constants.STREAM_LENGTH),
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready).result(
            timeout=test_constants.LONG_TIMEOUT)


def perform_stream_stream_call(channel, wait_for_ready=None):
    response_iterator = channel.stream_stream(_STREAM_STREAM).__call__(
        iter([_REQUEST] * test_constants.STREAM_LENGTH),
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready)
    for _ in response_iterator:
        pass


_ALL_CALL_CASES = [
    perform_unary_unary_call, perform_unary_unary_with_call,
    perform_unary_unary_future, perform_unary_stream_call,
    perform_stream_unary_call, perform_stream_unary_with_call,
    perform_stream_unary_future, perform_stream_stream_call
]


class MetadataFlagsTest(unittest.TestCase):

    def check_connection_does_failfast(self, fn, channel, wait_for_ready=None):
        try:
            fn(channel, wait_for_ready)
            self.fail("The Call should fail")
        except BaseException as e:  # pylint: disable=broad-except
            self.assertIs(grpc.StatusCode.UNAVAILABLE, e.code())

    def test_call_wait_for_ready_default(self):
        for perform_call in _ALL_CALL_CASES:
            with create_phony_channel() as channel:
                self.check_connection_does_failfast(perform_call, channel)

    def test_call_wait_for_ready_disabled(self):
        for perform_call in _ALL_CALL_CASES:
            with create_phony_channel() as channel:
                self.check_connection_does_failfast(perform_call,
                                                    channel,
                                                    wait_for_ready=False)

    def test_call_wait_for_ready_enabled(self):
        # To test the wait mechanism, Python thread is required to make
        #   client set up first without handling them case by case.
        # Also, Python thread don't pass the unhandled exceptions to
        #   main thread. So, it need another method to store the
        #   exceptions and raise them again in main thread.
        unhandled_exceptions = queue.Queue()

        # We just need an unused TCP port
        host, port, sock = get_socket(sock_options=(socket.SO_REUSEADDR,))
        sock.close()

        addr = '{}:{}'.format(host, port)
        wg = test_common.WaitGroup(len(_ALL_CALL_CASES))

        def wait_for_transient_failure(channel_connectivity):
            if channel_connectivity == grpc.ChannelConnectivity.TRANSIENT_FAILURE:
                wg.done()

        def test_call(perform_call):
            with grpc.insecure_channel(addr) as channel:
                try:
                    channel.subscribe(wait_for_transient_failure)
                    perform_call(channel, wait_for_ready=True)
                except BaseException as e:  # pylint: disable=broad-except
                    # If the call failed, the thread would be destroyed. The
                    # channel object can be collected before calling the
                    # callback, which will result in a deadlock.
                    wg.done()
                    unhandled_exceptions.put(e, True)

        test_threads = []
        for perform_call in _ALL_CALL_CASES:
            test_thread = threading.Thread(target=test_call,
                                           args=(perform_call,))
            test_thread.daemon = True
            test_thread.exception = None
            test_thread.start()
            test_threads.append(test_thread)

        # Start the server after the connections are waiting
        wg.wait()
        server = test_common.test_server(reuse_port=True)
        server.add_generic_rpc_handlers((_GenericHandler(weakref.proxy(self)),))
        server.add_insecure_port(addr)
        server.start()

        for test_thread in test_threads:
            test_thread.join()

        # Stop the server to make test end properly
        server.stop(0)

        if not unhandled_exceptions.empty():
            raise unhandled_exceptions.get(True)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
