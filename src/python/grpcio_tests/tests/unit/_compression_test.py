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
"""Tests server and client side compression."""

from concurrent import futures
import contextlib
import functools
import itertools
import logging
import os
import unittest

import grpc
from grpc import _grpcio_metadata

from tests.unit import _tcp_proxy
from tests.unit.framework.common import test_constants

_SERVICE_NAME = "test"
_UNARY_UNARY = "UnaryUnary"
_UNARY_STREAM = "UnaryStream"
_STREAM_UNARY = "StreamUnary"
_STREAM_STREAM = "StreamStream"

# Cut down on test time.
_STREAM_LENGTH = test_constants.STREAM_LENGTH // 16

_HOST = "localhost"

_REQUEST = b"\x00" * 100
_COMPRESSION_RATIO_THRESHOLD = 0.05
_COMPRESSION_METHODS = (
    None,
    # Disabled for test tractability.
    # grpc.Compression.NoCompression,
    # grpc.Compression.Deflate,
    grpc.Compression.Gzip,
)
_COMPRESSION_NAMES = {
    None: "Uncompressed",
    grpc.Compression.NoCompression: "NoCompression",
    grpc.Compression.Deflate: "DeflateCompression",
    grpc.Compression.Gzip: "GzipCompression",
}

_TEST_OPTIONS = {
    "client_streaming": (True, False),
    "server_streaming": (True, False),
    "channel_compression": _COMPRESSION_METHODS,
    "multicallable_compression": _COMPRESSION_METHODS,
    "server_compression": _COMPRESSION_METHODS,
    "server_call_compression": _COMPRESSION_METHODS,
}


def _make_handle_unary_unary(pre_response_callback):
    def _handle_unary(request, servicer_context):
        if pre_response_callback:
            pre_response_callback(request, servicer_context)
        return request

    return _handle_unary


def _make_handle_unary_stream(pre_response_callback):
    def _handle_unary_stream(request, servicer_context):
        if pre_response_callback:
            pre_response_callback(request, servicer_context)
        for _ in range(_STREAM_LENGTH):
            yield request

    return _handle_unary_stream


def _make_handle_stream_unary(pre_response_callback):
    def _handle_stream_unary(request_iterator, servicer_context):
        if pre_response_callback:
            pre_response_callback(request_iterator, servicer_context)
        response = None
        for request in request_iterator:
            if not response:
                response = request
        return response

    return _handle_stream_unary


def _make_handle_stream_stream(pre_response_callback):
    def _handle_stream(request_iterator, servicer_context):
        # TODO(issue:#6891) We should be able to remove this loop,
        # and replace with return; yield
        for request in request_iterator:
            if pre_response_callback:
                pre_response_callback(request, servicer_context)
            yield request

    return _handle_stream


def set_call_compression(
    compression_method, request_or_iterator, servicer_context
):
    del request_or_iterator
    servicer_context.set_compression(compression_method)


def disable_next_compression(request, servicer_context):
    del request
    servicer_context.disable_next_message_compression()


def disable_first_compression(request, servicer_context):
    if int(request.decode("ascii")) == 0:
        servicer_context.disable_next_message_compression()


class _MethodHandler(grpc.RpcMethodHandler):
    def __init__(
        self, request_streaming, response_streaming, pre_response_callback
    ):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self.stream_stream = None

        if self.request_streaming and self.response_streaming:
            self.stream_stream = _make_handle_stream_stream(
                pre_response_callback
            )
        elif not self.request_streaming and not self.response_streaming:
            self.unary_unary = _make_handle_unary_unary(pre_response_callback)
        elif not self.request_streaming and self.response_streaming:
            self.unary_stream = _make_handle_unary_stream(pre_response_callback)
        else:
            self.stream_unary = _make_handle_stream_unary(pre_response_callback)


def get_method_handlers(pre_response_callback):
    return {
        _UNARY_UNARY: _MethodHandler(False, False, pre_response_callback),
        _UNARY_STREAM: _MethodHandler(False, True, pre_response_callback),
        _STREAM_UNARY: _MethodHandler(True, False, pre_response_callback),
        _STREAM_STREAM: _MethodHandler(True, True, pre_response_callback),
    }


@contextlib.contextmanager
def _instrumented_client_server_pair(
    channel_kwargs, server_kwargs, server_handler
):
    server = grpc.server(futures.ThreadPoolExecutor(), **server_kwargs)
    server.add_registered_method_handlers(_SERVICE_NAME, server_handler)
    server_port = server.add_insecure_port("{}:0".format(_HOST))
    server.start()
    with _tcp_proxy.TcpProxy(_HOST, _HOST, server_port) as proxy:
        proxy_port = proxy.get_port()
        with grpc.insecure_channel(
            "{}:{}".format(_HOST, proxy_port), **channel_kwargs
        ) as client_channel:
            try:
                yield client_channel, proxy, server
            finally:
                server.stop(None)


def _get_byte_counts(
    channel_kwargs,
    multicallable_kwargs,
    client_function,
    server_kwargs,
    server_handler,
    message,
):
    with _instrumented_client_server_pair(
        channel_kwargs, server_kwargs, server_handler
    ) as pipeline:
        client_channel, proxy, server = pipeline
        client_function(client_channel, multicallable_kwargs, message)
        return proxy.get_byte_count()


def _get_compression_ratios(
    client_function,
    first_channel_kwargs,
    first_multicallable_kwargs,
    first_server_kwargs,
    first_server_handler,
    second_channel_kwargs,
    second_multicallable_kwargs,
    second_server_kwargs,
    second_server_handler,
    message,
):
    first_bytes_sent, first_bytes_received = _get_byte_counts(
        first_channel_kwargs,
        first_multicallable_kwargs,
        client_function,
        first_server_kwargs,
        first_server_handler,
        message,
    )
    second_bytes_sent, second_bytes_received = _get_byte_counts(
        second_channel_kwargs,
        second_multicallable_kwargs,
        client_function,
        second_server_kwargs,
        second_server_handler,
        message,
    )
    return (
        (second_bytes_sent - first_bytes_sent) / float(first_bytes_sent),
        (second_bytes_received - first_bytes_received)
        / float(first_bytes_received),
    )


def _unary_unary_client(channel, multicallable_kwargs, message):
    multi_callable = channel.unary_unary(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_UNARY),
        _registered_method=True,
    )
    response = multi_callable(message, **multicallable_kwargs)
    if response != message:
        raise RuntimeError(
            "Request '{}' != Response '{}'".format(message, response)
        )


def _unary_stream_client(channel, multicallable_kwargs, message):
    multi_callable = channel.unary_stream(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_STREAM),
        _registered_method=True,
    )
    response_iterator = multi_callable(message, **multicallable_kwargs)
    for response in response_iterator:
        if response != message:
            raise RuntimeError(
                "Request '{}' != Response '{}'".format(message, response)
            )


def _stream_unary_client(channel, multicallable_kwargs, message):
    multi_callable = channel.stream_unary(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_UNARY),
        _registered_method=True,
    )
    requests = (_REQUEST for _ in range(_STREAM_LENGTH))
    response = multi_callable(requests, **multicallable_kwargs)
    if response != message:
        raise RuntimeError(
            "Request '{}' != Response '{}'".format(message, response)
        )


def _stream_stream_client(channel, multicallable_kwargs, message):
    multi_callable = channel.stream_stream(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_STREAM),
        _registered_method=True,
    )
    request_prefix = str(0).encode("ascii") * 100
    requests = (
        request_prefix + str(i).encode("ascii") for i in range(_STREAM_LENGTH)
    )
    response_iterator = multi_callable(requests, **multicallable_kwargs)
    for i, response in enumerate(response_iterator):
        if int(response.decode("ascii")) != i:
            raise RuntimeError(
                "Request '{}' != Response '{}'".format(i, response)
            )


class CompressionTest(unittest.TestCase):
    def assertCompressed(self, compression_ratio):
        self.assertLess(
            compression_ratio,
            -1.0 * _COMPRESSION_RATIO_THRESHOLD,
            msg="Actual compression ratio: {}".format(compression_ratio),
        )

    def assertNotCompressed(self, compression_ratio):
        self.assertGreaterEqual(
            compression_ratio,
            -1.0 * _COMPRESSION_RATIO_THRESHOLD,
            msg="Actual compression ratio: {}".format(compression_ratio),
        )

    def assertConfigurationCompressed(
        self,
        client_streaming,
        server_streaming,
        channel_compression,
        multicallable_compression,
        server_compression,
        server_call_compression,
    ):
        client_side_compressed = (
            channel_compression or multicallable_compression
        )
        server_side_compressed = server_compression or server_call_compression
        channel_kwargs = (
            {
                "compression": channel_compression,
            }
            if channel_compression
            else {}
        )
        multicallable_kwargs = (
            {
                "compression": multicallable_compression,
            }
            if multicallable_compression
            else {}
        )

        client_function = None
        if not client_streaming and not server_streaming:
            client_function = _unary_unary_client
        elif not client_streaming and server_streaming:
            client_function = _unary_stream_client
        elif client_streaming and not server_streaming:
            client_function = _stream_unary_client
        else:
            client_function = _stream_stream_client

        server_kwargs = (
            {
                "compression": server_compression,
            }
            if server_compression
            else {}
        )
        if server_call_compression:
            server_handler = get_method_handlers(
                functools.partial(set_call_compression, grpc.Compression.Gzip)
            )
        else:
            server_handler = get_method_handlers(None)

        _get_compression_ratios(
            client_function,
            {},
            {},
            {},
            get_method_handlers(None),
            channel_kwargs,
            multicallable_kwargs,
            server_kwargs,
            server_handler,
            _REQUEST,
        )

    def testDisableNextCompressionStreaming(self):
        server_kwargs = {
            "compression": grpc.Compression.Deflate,
        }
        _get_compression_ratios(
            _stream_stream_client,
            {},
            {},
            {},
            get_method_handlers(None),
            {},
            {},
            server_kwargs,
            get_method_handlers(disable_next_compression),
            _REQUEST,
        )

    def testDisableNextCompressionStreamingResets(self):
        server_kwargs = {
            "compression": grpc.Compression.Deflate,
        }
        _get_compression_ratios(
            _stream_stream_client,
            {},
            {},
            {},
            get_method_handlers(None),
            {},
            {},
            server_kwargs,
            get_method_handlers(disable_first_compression),
            _REQUEST,
        )


def _get_compression_str(name, value):
    return "{}{}".format(name, _COMPRESSION_NAMES[value])


def _get_compression_test_name(
    client_streaming,
    server_streaming,
    channel_compression,
    multicallable_compression,
    server_compression,
    server_call_compression,
):
    client_arity = "Stream" if client_streaming else "Unary"
    server_arity = "Stream" if server_streaming else "Unary"
    arity = "{}{}".format(client_arity, server_arity)
    channel_compression_str = _get_compression_str(
        "Channel", channel_compression
    )
    multicallable_compression_str = _get_compression_str(
        "Multicallable", multicallable_compression
    )
    server_compression_str = _get_compression_str("Server", server_compression)
    server_call_compression_str = _get_compression_str(
        "ServerCall", server_call_compression
    )
    return "test{}{}{}{}{}".format(
        arity,
        channel_compression_str,
        multicallable_compression_str,
        server_compression_str,
        server_call_compression_str,
    )


def _test_options():
    for test_parameters in itertools.product(*_TEST_OPTIONS.values()):
        yield dict(zip(_TEST_OPTIONS.keys(), test_parameters))


for options in _test_options():

    def test_compression(**kwargs):
        def _test_compression(self):
            self.assertConfigurationCompressed(**kwargs)

        return _test_compression

    setattr(
        CompressionTest,
        _get_compression_test_name(**options),
        test_compression(**options),
    )

if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
