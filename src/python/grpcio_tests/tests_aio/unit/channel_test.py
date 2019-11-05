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
from tests.unit.framework.common import test_constants
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase


class TestChannel(AioTestBase):

    def test_async_context(self):

        async def coro():
            server_target, unused_server = await start_test_server()

            async with aio.insecure_channel(server_target) as channel:
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
            server_target, unused_server = await start_test_server()

            channel = aio.insecure_channel(server_target)
            hi = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            response = await hi(messages_pb2.SimpleRequest())

            self.assertEqual(type(response), messages_pb2.SimpleResponse)

            await channel.close()

        self.loop.run_until_complete(coro())

    def test_unary_call_times_out(self):

        async def coro():
            server_target, unused_server = await start_test_server()

            async with aio.insecure_channel(server_target) as channel:
                empty_call_with_sleep = channel.unary_unary(
                    "/grpc.testing.TestService/EmptyCall",
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.
                    FromString,
                )
                timeout = test_constants.SHORT_TIMEOUT / 2
                # TODO: Update once the async server is ready, change the synchronization mechanism by removing the
                # sleep(<timeout>) as both components (client & server) will be on the same process.
                with self.assertRaises(grpc.RpcError) as exception_context:
                    await empty_call_with_sleep(
                        messages_pb2.SimpleRequest(), timeout=timeout)

                status_code, details = grpc.StatusCode.DEADLINE_EXCEEDED.value
                self.assertEqual(exception_context.exception.code(),
                                 status_code)
                self.assertEqual(exception_context.exception.details(),
                                 details.title())
                self.assertIsNotNone(
                    exception_context.exception.initial_metadata())
                self.assertIsNotNone(
                    exception_context.exception.trailing_metadata())

        self.loop.run_until_complete(coro())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
