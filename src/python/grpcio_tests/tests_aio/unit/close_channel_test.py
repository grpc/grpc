# Copyright 2020 The gRPC Authors.
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
"""Tests behavior of closing a grpc.aio.Channel."""

import asyncio
import logging
import unittest

import grpc
from grpc.experimental import aio
from grpc.aio import _base_call

from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_UNARY_CALL_METHOD_WITH_SLEEP = '/grpc.testing.TestService/UnaryCallWithSleep'
_LONG_TIMEOUT_THAT_SHOULD_NOT_EXPIRE = 60


class TestCloseChannel(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_graceful_close(self):
        channel = aio.insecure_channel(self._server_target)
        UnaryCallWithSleep = channel.unary_unary(
            _UNARY_CALL_METHOD_WITH_SLEEP,
            request_serializer=messages_pb2.SimpleRequest.SerializeToString,
            response_deserializer=messages_pb2.SimpleResponse.FromString,
        )

        call = UnaryCallWithSleep(messages_pb2.SimpleRequest())

        await channel.close(grace=_LONG_TIMEOUT_THAT_SHOULD_NOT_EXPIRE)

        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_none_graceful_close(self):
        channel = aio.insecure_channel(self._server_target)
        UnaryCallWithSleep = channel.unary_unary(
            _UNARY_CALL_METHOD_WITH_SLEEP,
            request_serializer=messages_pb2.SimpleRequest.SerializeToString,
            response_deserializer=messages_pb2.SimpleResponse.FromString,
        )

        call = UnaryCallWithSleep(messages_pb2.SimpleRequest())

        await channel.close(None)

        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

    async def test_close_unary_unary(self):
        channel = aio.insecure_channel(self._server_target)
        stub = test_pb2_grpc.TestServiceStub(channel)

        calls = [stub.UnaryCall(messages_pb2.SimpleRequest()) for _ in range(2)]

        await channel.close()

        for call in calls:
            self.assertTrue(call.cancelled())

    async def test_close_unary_stream(self):
        channel = aio.insecure_channel(self._server_target)
        stub = test_pb2_grpc.TestServiceStub(channel)

        request = messages_pb2.StreamingOutputCallRequest()
        calls = [stub.StreamingOutputCall(request) for _ in range(2)]

        await channel.close()

        for call in calls:
            self.assertTrue(call.cancelled())

    async def test_close_stream_unary(self):
        channel = aio.insecure_channel(self._server_target)
        stub = test_pb2_grpc.TestServiceStub(channel)

        calls = [stub.StreamingInputCall() for _ in range(2)]

        await channel.close()

        for call in calls:
            self.assertTrue(call.cancelled())

    async def test_close_stream_stream(self):
        channel = aio.insecure_channel(self._server_target)
        stub = test_pb2_grpc.TestServiceStub(channel)

        calls = [stub.FullDuplexCall() for _ in range(2)]

        await channel.close()

        for call in calls:
            self.assertTrue(call.cancelled())

    async def test_close_async_context(self):
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            calls = [
                stub.UnaryCall(messages_pb2.SimpleRequest()) for _ in range(2)
            ]

        for call in calls:
            self.assertTrue(call.cancelled())

    async def test_channel_isolation(self):
        async with aio.insecure_channel(self._server_target) as channel1:
            async with aio.insecure_channel(self._server_target) as channel2:
                stub1 = test_pb2_grpc.TestServiceStub(channel1)
                stub2 = test_pb2_grpc.TestServiceStub(channel2)

                call1 = stub1.UnaryCall(messages_pb2.SimpleRequest())
                call2 = stub2.UnaryCall(messages_pb2.SimpleRequest())

            self.assertFalse(call1.cancelled())
            self.assertTrue(call2.cancelled())


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
