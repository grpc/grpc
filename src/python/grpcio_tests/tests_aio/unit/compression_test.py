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
"""Tests behavior around the compression mechanism."""

import asyncio
import logging
import platform
import random
import unittest

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit import _common

_GZIP_CHANNEL_ARGUMENT = ('grpc.default_compression_algorithm', 2)
_GZIP_DISABLED_CHANNEL_ARGUMENT = ('grpc.compression_enabled_algorithms_bitset',
                                   3)
_DEFLATE_DISABLED_CHANNEL_ARGUMENT = (
    'grpc.compression_enabled_algorithms_bitset', 5)

_TEST_UNARY_UNARY = '/test/TestUnaryUnary'
_TEST_SET_COMPRESSION = '/test/TestSetCompression'
_TEST_DISABLE_COMPRESSION_UNARY = '/test/TestDisableCompressionUnary'
_TEST_DISABLE_COMPRESSION_STREAM = '/test/TestDisableCompressionStream'

_REQUEST = b'\x01' * 100
_RESPONSE = b'\x02' * 100


async def _test_unary_unary(unused_request, unused_context):
    return _RESPONSE


async def _test_set_compression(unused_request_iterator, context):
    assert _REQUEST == await context.read()
    context.set_compression(grpc.Compression.Deflate)
    await context.write(_RESPONSE)
    try:
        context.set_compression(grpc.Compression.Deflate)
    except RuntimeError:
        # NOTE(lidiz) Testing if the servicer context raises exception when
        # the set_compression method is called after initial_metadata sent.
        # After the initial_metadata sent, the server-side has no control over
        # which compression algorithm it should use.
        pass
    else:
        raise ValueError(
            'Expecting exceptions if set_compression is not effective')


async def _test_disable_compression_unary(request, context):
    assert _REQUEST == request
    context.set_compression(grpc.Compression.Deflate)
    context.disable_next_message_compression()
    return _RESPONSE


async def _test_disable_compression_stream(unused_request_iterator, context):
    assert _REQUEST == await context.read()
    context.set_compression(grpc.Compression.Deflate)
    await context.write(_RESPONSE)
    context.disable_next_message_compression()
    await context.write(_RESPONSE)
    await context.write(_RESPONSE)


_ROUTING_TABLE = {
    _TEST_UNARY_UNARY:
        grpc.unary_unary_rpc_method_handler(_test_unary_unary),
    _TEST_SET_COMPRESSION:
        grpc.stream_stream_rpc_method_handler(_test_set_compression),
    _TEST_DISABLE_COMPRESSION_UNARY:
        grpc.unary_unary_rpc_method_handler(_test_disable_compression_unary),
    _TEST_DISABLE_COMPRESSION_STREAM:
        grpc.stream_stream_rpc_method_handler(_test_disable_compression_stream),
}


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        return _ROUTING_TABLE.get(handler_call_details.method)


async def _start_test_server(options=None):
    server = aio.server(options=options)
    port = server.add_insecure_port('[::]:0')
    server.add_generic_rpc_handlers((_GenericHandler(),))
    await server.start()
    return f'localhost:{port}', server


class TestCompression(AioTestBase):

    async def setUp(self):
        server_options = (_GZIP_DISABLED_CHANNEL_ARGUMENT,)
        self._address, self._server = await _start_test_server(server_options)
        self._channel = aio.insecure_channel(self._address)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def test_channel_level_compression_baned_compression(self):
        # GZIP is disabled, this call should fail
        async with aio.insecure_channel(
                self._address, compression=grpc.Compression.Gzip) as channel:
            multicallable = channel.unary_unary(_TEST_UNARY_UNARY)
            call = multicallable(_REQUEST)
            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call
            rpc_error = exception_context.exception
            self.assertEqual(grpc.StatusCode.UNIMPLEMENTED, rpc_error.code())

    async def test_channel_level_compression_allowed_compression(self):
        # Deflate is allowed, this call should succeed
        async with aio.insecure_channel(
                self._address, compression=grpc.Compression.Deflate) as channel:
            multicallable = channel.unary_unary(_TEST_UNARY_UNARY)
            call = multicallable(_REQUEST)
            self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_client_call_level_compression_baned_compression(self):
        multicallable = self._channel.unary_unary(_TEST_UNARY_UNARY)

        # GZIP is disabled, this call should fail
        call = multicallable(_REQUEST, compression=grpc.Compression.Gzip)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNIMPLEMENTED, rpc_error.code())

    async def test_client_call_level_compression_allowed_compression(self):
        multicallable = self._channel.unary_unary(_TEST_UNARY_UNARY)

        # Deflate is allowed, this call should succeed
        call = multicallable(_REQUEST, compression=grpc.Compression.Deflate)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_server_call_level_compression(self):
        multicallable = self._channel.stream_stream(_TEST_SET_COMPRESSION)
        call = multicallable()
        await call.write(_REQUEST)
        await call.done_writing()
        self.assertEqual(_RESPONSE, await call.read())
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_server_disable_compression_unary(self):
        multicallable = self._channel.unary_unary(
            _TEST_DISABLE_COMPRESSION_UNARY)
        call = multicallable(_REQUEST)
        self.assertEqual(_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_server_disable_compression_stream(self):
        multicallable = self._channel.stream_stream(
            _TEST_DISABLE_COMPRESSION_STREAM)
        call = multicallable()
        await call.write(_REQUEST)
        await call.done_writing()
        self.assertEqual(_RESPONSE, await call.read())
        self.assertEqual(_RESPONSE, await call.read())
        self.assertEqual(_RESPONSE, await call.read())
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_server_default_compression_algorithm(self):
        server = aio.server(compression=grpc.Compression.Deflate)
        port = server.add_insecure_port('[::]:0')
        server.add_generic_rpc_handlers((_GenericHandler(),))
        await server.start()

        async with aio.insecure_channel(f'localhost:{port}') as channel:
            multicallable = channel.unary_unary(_TEST_UNARY_UNARY)
            call = multicallable(_REQUEST)
            self.assertEqual(_RESPONSE, await call)
            self.assertEqual(grpc.StatusCode.OK, await call.code())

        await server.stop(None)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
