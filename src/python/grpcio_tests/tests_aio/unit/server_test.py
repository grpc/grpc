# Copyright 2019 The gRPC Authors.
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

import asyncio
import logging
import unittest
import time
import gc

import grpc
from grpc.experimental import aio
from tests_aio.unit._test_base import AioTestBase
from tests.unit.framework.common import test_constants

_SIMPLE_UNARY_UNARY = '/test/SimpleUnaryUnary'
_BLOCK_FOREVER = '/test/BlockForever'
_BLOCK_BRIEFLY = '/test/BlockBriefly'
_UNARY_STREAM_ASYNC_GEN = '/test/UnaryStreamAsyncGen'
_UNARY_STREAM_READER_WRITER = '/test/UnaryStreamReaderWriter'
_UNARY_STREAM_EVILLY_MIXED = '/test/UnaryStreamEvillyMixed'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'
_NUM_STREAM_RESPONSES = 5


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self):
        self._called = asyncio.get_event_loop().create_future()

    @staticmethod
    async def _unary_unary(unused_request, unused_context):
        return _RESPONSE

    async def _block_forever(self, unused_request, unused_context):
        await asyncio.get_event_loop().create_future()

    async def _block_briefly(self, unused_request, unused_context):
        await asyncio.sleep(test_constants.SHORT_TIMEOUT / 2)
        return _RESPONSE

    async def _unary_stream_async_gen(self, unused_request, unused_context):
        for _ in range(_NUM_STREAM_RESPONSES):
            yield _RESPONSE

    async def _unary_stream_reader_writer(self, unused_request, context):
        for _ in range(_NUM_STREAM_RESPONSES):
            await context.write(_RESPONSE)

    async def _unary_stream_evilly_mixed(self, unused_request, context):
        yield _RESPONSE
        for _ in range(_NUM_STREAM_RESPONSES - 1):
            await context.write(_RESPONSE)

    def service(self, handler_details):
        self._called.set_result(None)
        if handler_details.method == _SIMPLE_UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(self._unary_unary)
        if handler_details.method == _BLOCK_FOREVER:
            return grpc.unary_unary_rpc_method_handler(self._block_forever)
        if handler_details.method == _BLOCK_BRIEFLY:
            return grpc.unary_unary_rpc_method_handler(self._block_briefly)
        if handler_details.method == _UNARY_STREAM_ASYNC_GEN:
            return grpc.unary_stream_rpc_method_handler(
                self._unary_stream_async_gen)
        if handler_details.method == _UNARY_STREAM_READER_WRITER:
            return grpc.unary_stream_rpc_method_handler(
                self._unary_stream_reader_writer)
        if handler_details.method == _UNARY_STREAM_EVILLY_MIXED:
            return grpc.unary_stream_rpc_method_handler(
                self._unary_stream_evilly_mixed)

    async def wait_for_call(self):
        await self._called


async def _start_test_server():
    server = aio.server()
    port = server.add_insecure_port('[::]:0')
    generic_handler = _GenericHandler()
    server.add_generic_rpc_handlers((generic_handler,))
    await server.start()
    return 'localhost:%d' % port, server, generic_handler


class TestServer(AioTestBase):

    async def setUp(self):
        self._server_target, self._server, self._generic_handler = await _start_test_server(
        )

    async def tearDown(self):
        await self._server.stop(None)

    async def test_unary_unary(self):
        async with aio.insecure_channel(self._server_target) as channel:
            unary_unary_call = channel.unary_unary(_SIMPLE_UNARY_UNARY)
            response = await unary_unary_call(_REQUEST)
            self.assertEqual(response, _RESPONSE)

    async def test_unary_stream_async_generator(self):
        async with aio.insecure_channel(self._server_target) as channel:
            unary_stream_call = channel.unary_stream(_UNARY_STREAM_ASYNC_GEN)
            call = unary_stream_call(_REQUEST)

            # Expecting the request message to reach server before retriving
            # any responses.
            await asyncio.wait_for(self._generic_handler.wait_for_call(),
                                   test_constants.SHORT_TIMEOUT)

            response_cnt = 0
            async for response in call:
                response_cnt += 1
                self.assertEqual(_RESPONSE, response)

            self.assertEqual(_NUM_STREAM_RESPONSES, response_cnt)
            self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_unary_stream_reader_writer(self):
        async with aio.insecure_channel(self._server_target) as channel:
            unary_stream_call = channel.unary_stream(
                _UNARY_STREAM_READER_WRITER)
            call = unary_stream_call(_REQUEST)

            # Expecting the request message to reach server before retriving
            # any responses.
            await asyncio.wait_for(self._generic_handler.wait_for_call(),
                                   test_constants.SHORT_TIMEOUT)

            for _ in range(_NUM_STREAM_RESPONSES):
                response = await call.read()
                self.assertEqual(_RESPONSE, response)

            self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_unary_stream_evilly_mixed(self):
        async with aio.insecure_channel(self._server_target) as channel:
            unary_stream_call = channel.unary_stream(_UNARY_STREAM_EVILLY_MIXED)
            call = unary_stream_call(_REQUEST)

            # Expecting the request message to reach server before retriving
            # any responses.
            await asyncio.wait_for(self._generic_handler.wait_for_call(),
                                   test_constants.SHORT_TIMEOUT)

            # Uses reader API
            self.assertEqual(_RESPONSE, await call.read())

            # Uses async generator API
            response_cnt = 0
            async for response in call:
                response_cnt += 1
                self.assertEqual(_RESPONSE, response)

            self.assertEqual(_NUM_STREAM_RESPONSES - 1, response_cnt)

            self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_shutdown(self):
        await self._server.stop(None)
        # Ensures no SIGSEGV triggered, and ends within timeout.

    async def test_shutdown_after_call(self):
        async with aio.insecure_channel(self._server_target) as channel:
            await channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)

        await self._server.stop(None)

    async def test_graceful_shutdown_success(self):
        channel = aio.insecure_channel(self._server_target)
        call = channel.unary_unary(_BLOCK_BRIEFLY)(_REQUEST)
        await self._generic_handler.wait_for_call()

        shutdown_start_time = time.time()
        await self._server.stop(test_constants.SHORT_TIMEOUT)
        grace_period_length = time.time() - shutdown_start_time
        self.assertGreater(grace_period_length,
                           test_constants.SHORT_TIMEOUT / 3)

        # Validates the states.
        await channel.close()
        self.assertEqual(_RESPONSE, await call)
        self.assertTrue(call.done())

    async def test_graceful_shutdown_failed(self):
        channel = aio.insecure_channel(self._server_target)
        call = channel.unary_unary(_BLOCK_FOREVER)(_REQUEST)
        await self._generic_handler.wait_for_call()

        await self._server.stop(test_constants.SHORT_TIMEOUT)

        with self.assertRaises(grpc.RpcError) as exception_context:
            await call
        self.assertEqual(grpc.StatusCode.UNAVAILABLE,
                         exception_context.exception.code())
        self.assertIn('GOAWAY', exception_context.exception.details())
        await channel.close()

    async def test_concurrent_graceful_shutdown(self):
        channel = aio.insecure_channel(self._server_target)
        call = channel.unary_unary(_BLOCK_BRIEFLY)(_REQUEST)
        await self._generic_handler.wait_for_call()

        # Expects the shortest grace period to be effective.
        shutdown_start_time = time.time()
        await asyncio.gather(
            self._server.stop(test_constants.LONG_TIMEOUT),
            self._server.stop(test_constants.SHORT_TIMEOUT),
            self._server.stop(test_constants.LONG_TIMEOUT),
        )
        grace_period_length = time.time() - shutdown_start_time
        self.assertGreater(grace_period_length,
                           test_constants.SHORT_TIMEOUT / 3)

        await channel.close()
        self.assertEqual(_RESPONSE, await call)
        self.assertTrue(call.done())

    async def test_concurrent_graceful_shutdown_immediate(self):
        channel = aio.insecure_channel(self._server_target)
        call = channel.unary_unary(_BLOCK_FOREVER)(_REQUEST)
        await self._generic_handler.wait_for_call()

        # Expects no grace period, due to the "server.stop(None)".
        await asyncio.gather(
            self._server.stop(test_constants.LONG_TIMEOUT),
            self._server.stop(None),
            self._server.stop(test_constants.SHORT_TIMEOUT),
            self._server.stop(test_constants.LONG_TIMEOUT),
        )

        with self.assertRaises(grpc.RpcError) as exception_context:
            await call
        self.assertEqual(grpc.StatusCode.UNAVAILABLE,
                         exception_context.exception.code())
        self.assertIn('GOAWAY', exception_context.exception.details())
        await channel.close()

    @unittest.skip('https://github.com/grpc/grpc/issues/20818')
    async def test_shutdown_before_call(self):
        server_target, server, _ = _start_test_server()
        await server.stop(None)

        # Ensures the server is cleaned up at this point.
        # Some proper exception should be raised.
        async with aio.insecure_channel('localhost:%d' % port) as channel:
            await channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
