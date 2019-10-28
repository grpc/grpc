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

import logging
import unittest

import grpc
from grpc.experimental import aio
from tests_aio.unit._test_base import AioTestBase
from tests.unit.framework.common import test_constants

_SIMPLE_UNARY_UNARY = '/test/SimpleUnaryUnary'
_BLOCK_FOREVER = '/test/BlockForever'
_BLOCK_SHORTLY = '/test/BlockShortly'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self):
        self._called = asyncio.get_event_loop().create_future()

    @staticmethod
    async def _unary_unary(unused_request, unused_context):
        return _RESPONSE

    async def _block_forever(self, unused_request, unused_context):
        await asyncio.get_event_loop().create_future()

    async def _block_shortly(self, unused_request, unused_context):
        await asyncio.sleep(test_constants.SHORT_TIMEOUT / 2)
        return _RESPONSE

    def service(self, handler_details):
        self._called.set_result(None)
        if handler_details.method == _SIMPLE_UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(self._unary_unary)
        if handler_details.method == _BLOCK_FOREVER:
            return grpc.unary_unary_rpc_method_handler(self._block_forever)
        if handler_details.method == _BLOCK_SHORTLY:
            return grpc.unary_unary_rpc_method_handler(self._block_shortly)

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

    def test_unary_unary(self):

        async def test_unary_unary_body():
            server_target, _, _ = await _start_test_server()

            async with aio.insecure_channel(server_target) as channel:
                unary_call = channel.unary_unary(_SIMPLE_UNARY_UNARY)
                response = await unary_call(_REQUEST)
                self.assertEqual(response, _RESPONSE)

        self.loop.run_until_complete(test_unary_unary_body())

    def test_shutdown(self):

        async def test_shutdown_body():
            _, server, _ = await _start_test_server()
            await server.stop(None)

        self.loop.run_until_complete(test_shutdown_body())

    def test_shutdown_after_call(self):

        async def test_shutdown_body():
            server_target, server, _ = await _start_test_server()

            async with aio.insecure_channel(server_target) as channel:
                await channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)

            await server.stop(None)

        self.loop.run_until_complete(test_shutdown_body())

    def test_graceful_shutdown_success(self):

        async def test_graceful_shutdown_success_body():
            server_target, server, generic_handler = await _start_test_server()

            channel = aio.insecure_channel(server_target)
            call_task = self.loop.create_task(
                channel.unary_unary(_BLOCK_SHORTLY)(_REQUEST))
            await generic_handler.wait_for_call()

            await server.stop(test_constants.SHORT_TIMEOUT)
            await channel.close()
            self.assertEqual(await call_task, _RESPONSE)
            self.assertTrue(call_task.done())

        self.loop.run_until_complete(test_graceful_shutdown_success_body())

    def test_graceful_shutdown_failed(self):

        async def test_graceful_shutdown_failed_body():
            server_target, server, generic_handler = await _start_test_server()

            channel = aio.insecure_channel(server_target)
            call_task = self.loop.create_task(
                channel.unary_unary(_BLOCK_FOREVER)(_REQUEST))
            await generic_handler.wait_for_call()

            await server.stop(test_constants.SHORT_TIMEOUT)

            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call_task
            self.assertEqual(exception_context.exception.code(),
                             grpc.StatusCode.UNAVAILABLE)
            self.assertIn('GOAWAY', exception_context.exception.details())
            await channel.close()

        self.loop.run_until_complete(test_graceful_shutdown_failed_body())

    @unittest.skip('https://github.com/grpc/grpc/issues/20818')
    def test_shutdown_before_call(self):

        async def test_shutdown_body():
            server_target, server, _ = _start_test_server()
            await server.stop(None)

            # Ensures the server is cleaned up at this point.
            # Some proper exception should be raised.
            async with aio.insecure_channel('localhost:%d' % port) as channel:
                await channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)

        self.loop.run_until_complete(test_shutdown_body())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
