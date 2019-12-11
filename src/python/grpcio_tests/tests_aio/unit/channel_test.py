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
"""Tests behavior of the grpc.aio.Channel class."""

import logging
import threading
import unittest

import grpc

from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import test_constants
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase

_UNARY_CALL_METHOD = '/grpc.testing.TestService/UnaryCall'
_EMPTY_CALL_METHOD = '/grpc.testing.TestService/EmptyCall'
_STREAMING_OUTPUT_CALL_METHOD = '/grpc.testing.TestService/StreamingOutputCall'

_NUM_STREAM_RESPONSES = 5
_RESPONSE_PAYLOAD_SIZE = 42


class TestChannel(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_async_context(self):
        async with aio.insecure_channel(self._server_target) as channel:
            hi = channel.unary_unary(
                _UNARY_CALL_METHOD,
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            await hi(messages_pb2.SimpleRequest())

    async def test_unary_unary(self):
        async with aio.insecure_channel(self._server_target) as channel:
            channel = aio.insecure_channel(self._server_target)
            hi = channel.unary_unary(
                _UNARY_CALL_METHOD,
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            response = await hi(messages_pb2.SimpleRequest())

            self.assertIsInstance(response, messages_pb2.SimpleResponse)

    async def test_unary_call_times_out(self):
        async with aio.insecure_channel(self._server_target) as channel:
            empty_call_with_sleep = channel.unary_unary(
                _EMPTY_CALL_METHOD,
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString,
            )
            timeout = test_constants.SHORT_TIMEOUT / 2
            # TODO(https://github.com/grpc/grpc/issues/20869)
            # Update once the async server is ready, change the
            # synchronization mechanism by removing the sleep(<timeout>)
            # as both components (client & server) will be on the same
            # process.
            with self.assertRaises(grpc.RpcError) as exception_context:
                await empty_call_with_sleep(
                    messages_pb2.SimpleRequest(), timeout=timeout)

            _, details = grpc.StatusCode.DEADLINE_EXCEEDED.value  # pylint: disable=unused-variable
            self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED,
                             exception_context.exception.code())
            self.assertEqual(details.title(),
                             exception_context.exception.details())
            self.assertIsNotNone(exception_context.exception.initial_metadata())
            self.assertIsNotNone(
                exception_context.exception.trailing_metadata())

    @unittest.skip('https://github.com/grpc/grpc/issues/20818')
    async def test_call_to_the_void(self):
        channel = aio.insecure_channel('0.1.1.1:1111')
        hi = channel.unary_unary(
            _UNARY_CALL_METHOD,
            request_serializer=messages_pb2.SimpleRequest.SerializeToString,
            response_deserializer=messages_pb2.SimpleResponse.FromString)
        response = await hi(messages_pb2.SimpleRequest())

        self.assertIsInstance(response, messages_pb2.SimpleResponse)

        await channel.close()

    async def test_unary_stream(self):
        channel = aio.insecure_channel(self._server_target)
        stub = test_pb2_grpc.TestServiceStub(channel)

        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        # Invokes the actual RPC
        call = stub.StreamingOutputCall(request)

        # Validates the responses
        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertIs(
                type(response), messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(_NUM_STREAM_RESPONSES, response_cnt)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)
        await channel.close()


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
