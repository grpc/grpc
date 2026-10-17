# Copyright 2026 gRPC authors.
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
"""Tests exposure of the :authority header on asynchronous servers."""

import logging
import unittest

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase

_REQUEST = b"\x01" * 100
_TEST_UNARY_UNARY = "/test/TestUnaryUnary"

async def _test_unary_unary(
        unused_request: bytes, servicer_context: aio.ServicerContext
):
    return servicer_context.experimental_authority().encode('utf-8')

_ROUTING_TABLE = {
    _TEST_UNARY_UNARY: grpc.unary_unary_rpc_method_handler(_test_unary_unary),
}

class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        return _ROUTING_TABLE.get(handler_call_details.method)

async def _start_test_server(options=None):
    server = aio.server(options=options)
    port = server.add_insecure_port("[::]:0")
    server.add_generic_rpc_handlers((_GenericHandler(),))
    await server.start()
    return f"localhost:{port}", server

class TestExperimentalAuthority(AioTestBase):
    async def setUp(self):
        self._address, self._server = await _start_test_server()
        self._channel = aio.insecure_channel(self._address)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def test_experimental_authority(self):
        multicallable = self._channel.unary_unary(_TEST_UNARY_UNARY)
        response = await multicallable(_REQUEST)
        self.assertEqual(self._address, response.decode('utf-8'))


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)

