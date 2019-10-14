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

from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import benchmark_service_pb2_grpc


class BenchmarkServer(benchmark_service_pb2_grpc.BenchmarkServiceServicer):

    async def UnaryCall(self, request, context):
        payload = messages_pb2.Payload(body=b'\0' * request.response_size)
        return messages_pb2.SimpleResponse(payload=payload)


class TestServer(unittest.TestCase):

    def test_unary_unary(self):
        loop = asyncio.get_event_loop()

        async def test_unary_unary_body():
            server = aio.server()
            port = server.add_insecure_port(('[::]:0').encode('ASCII'))
            benchmark_service_pb2_grpc.add_BenchmarkServiceServicer_to_server(
                BenchmarkServer(), server)
            await server.start()

            async with aio.insecure_channel(f'localhost:{port}') as channel:
                unary_call = channel.unary_unary(
                    '/grpc.testing.BenchmarkService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )
                response = await unary_call(
                    messages_pb2.SimpleRequest(response_size=1))
                self.assertIsInstance(response, messages_pb2.SimpleResponse)
                self.assertEqual(1, len(response.payload.body))

        loop.run_until_complete(test_unary_unary_body())


if __name__ == '__main__':
    aio.init_grpc_aio()
    logging.basicConfig()
    unittest.main(verbosity=2)
