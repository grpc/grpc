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

import grpc
from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import benchmark_service_pb2_grpc
from tests_aio.unit._test_base import AioTestBase

_TEST_METHOD_PATH = ''

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'


async def unary_unary(unused_request, unused_context):
    return _RESPONSE


class GenericHandler(grpc.GenericRpcHandler):

    def service(self, unused_handler_details):
        return grpc.unary_unary_rpc_method_handler(unary_unary)


class TestServer(AioTestBase):

    def test_unary_unary(self):

        async def test_unary_unary_body():
            server = aio.server()
            port = server.add_insecure_port('[::]:0')
            server.add_generic_rpc_handlers((GenericHandler(),))
            await server.start()

            async with aio.insecure_channel('localhost:%d' % port) as channel:
                unary_call = channel.unary_unary(_TEST_METHOD_PATH)
                response = await unary_call(_REQUEST)
                self.assertEqual(response, _RESPONSE)

        self.loop.run_until_complete(test_unary_unary_body())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
