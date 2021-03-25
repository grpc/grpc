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
"""Test of RPCs made using local credentials."""

import unittest
import os
import logging
import grpc
from grpc.experimental import aio
from tests_aio.unit._test_base import AioTestBase


class _GenericHandler(grpc.GenericRpcHandler):

    @staticmethod
    async def _identical(request, unused_context):
        return request

    def service(self, handler_call_details):
        return grpc.unary_unary_rpc_method_handler(self._identical)


def _create_server():
    server = aio.server()
    server.add_generic_rpc_handlers((_GenericHandler(),))
    return server


class TestLocalCredentials(AioTestBase):

    async def test_local_tcp(self):
        server_addr = 'localhost:{}'
        channel_creds = grpc.local_channel_credentials(
            grpc.LocalConnectionType.LOCAL_TCP)
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.LOCAL_TCP)

        server = _create_server()
        port = server.add_secure_port(server_addr.format(0), server_creds)
        await server.start()

        async with aio.secure_channel(server_addr.format(port),
                                      channel_creds) as channel:
            self.assertEqual(
                b'abc', await channel.unary_unary('/test/method')
                (b'abc', wait_for_ready=True))

        await server.stop(None)

    async def test_non_local_tcp(self):
        server_addr = '0.0.0.0:{}'
        channel_creds = grpc.local_channel_credentials(
            grpc.LocalConnectionType.LOCAL_TCP)
        server_creds = grpc.local_server_credentials(
            grpc.LocalConnectionType.LOCAL_TCP)

        server = _create_server()
        port = server.add_secure_port(server_addr.format(0), server_creds)
        await server.start()

        async with aio.secure_channel(server_addr.format(port),
                                      channel_creds) as channel:
            with self.assertRaises(grpc.aio.AioRpcError):
                await channel.unary_unary('/test/method')(b'abc')

        await server.stop(None)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main()
