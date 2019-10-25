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

_SIMPLE_UNARY_UNARY = '/test/SimpleUnaryUnary'
_BLOCK_FOREVER = '/test/BlockForever'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'


async def _unary_unary(unused_request, unused_context):
    return _RESPONSE


async def _block_forever(unused_request, unused_context):
    await asyncio.get_event_loop().create_future()


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_details):
        if handler_details.method == _SIMPLE_UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_unary_unary)
        if handler_details.method == _BLOCK_FOREVER:
            return grpc.unary_unary_rpc_method_handler(_block_forever)


class TestServer(AioTestBase):

    def test_unary_unary(self):

        async def test_unary_unary_body():
            server = aio.server()
            port = server.add_insecure_port('[::]:0')
            server.add_generic_rpc_handlers((_GenericHandler(),))
            await server.start()

            async with aio.insecure_channel('localhost:%d' % port) as channel:
                unary_call = channel.unary_unary(_SIMPLE_UNARY_UNARY)
                response = await unary_call(_REQUEST)
                self.assertEqual(response, _RESPONSE)

        self.loop.run_until_complete(test_unary_unary_body())
    
    def test_shutdown(self):

        async def test_shutdown_body():
            server = aio.server()
            port = server.add_insecure_port('[::]:0')
            await server.start()
            await server.stop(None)
        self.loop.run_until_complete(test_shutdown_body())

    def test_shutdown_after_call(self):

        async def test_shutdown_body():
            server = aio.server()
            port = server.add_insecure_port('[::]:0')
            server.add_generic_rpc_handlers((_GenericHandler(),))
            await server.start()

            async with aio.insecure_channel('localhost:%d' % port) as channel:
                await channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)

            await server.stop(None)
        self.loop.run_until_complete(test_shutdown_body())

    def test_shutdown_during_call(self):

        async def test_shutdown_body():
            server = aio.server()
            port = server.add_insecure_port('[::]:0')
            server.add_generic_rpc_handlers((_GenericHandler(),))
            await server.start()

            async with aio.insecure_channel('localhost:%d' % port) as channel:
                self.loop.create_task(channel.unary_unary(_BLOCK_FOREVER)(_REQUEST))
                await asyncio.sleep(0)

            await server.stop(None)
        self.loop.run_until_complete(test_shutdown_body())

    def test_shutdown_before_call(self):

        async def test_shutdown_body():
            server = aio.server()
            port = server.add_insecure_port('[::]:0')
            server.add_generic_rpc_handlers((_GenericHandler(),))
            await server.start()
            await server.stop(None)

            async with aio.insecure_channel('localhost:%d' % port) as channel:
                await channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)

        self.loop.run_until_complete(test_shutdown_body())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
