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

from grpc.experimental import aio
from tests_aio.unit import test_base
from src.proto.grpc.testing import messages_pb2


class TestChannel(test_base.AioTestBase):

    def test_async_context(self):

        async def coro():
            async with aio.insecure_channel(self.server_target) as channel:
                hi = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )
                await hi(messages_pb2.SimpleRequest())

        self.loop.run_until_complete(coro())

    def test_unary_unary(self):

        async def coro():
            channel = aio.insecure_channel(self.server_target)
            hi = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            response = await hi(messages_pb2.SimpleRequest())

            self.assertEqual(type(response), messages_pb2.SimpleResponse)

            await channel.close()

        self.loop.run_until_complete(coro())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
