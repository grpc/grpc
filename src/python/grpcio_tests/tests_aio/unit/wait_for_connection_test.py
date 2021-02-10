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
"""Tests behavior of the wait for connection API on client side."""

import asyncio
import logging
import unittest
import datetime
from typing import Callable, Tuple

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit import _common
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests_aio.unit._constants import UNREACHABLE_TARGET

_REQUEST = b'\x01\x02\x03'
_TEST_METHOD = '/test/Test'

_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 42


class TestWaitForConnection(AioTestBase):
    """Tests if wait_for_connection raises connectivity issue."""

    async def setUp(self):
        address, self._server = await start_test_server()
        self._channel = aio.insecure_channel(address)
        self._phony_channel = aio.insecure_channel(UNREACHABLE_TARGET)
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)

    async def tearDown(self):
        await self._phony_channel.close()
        await self._channel.close()
        await self._server.stop(None)

    async def test_unary_unary_ok(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

        # No exception raised and no message swallowed.
        await call.wait_for_connection()

        response = await call
        self.assertIsInstance(response, messages_pb2.SimpleResponse)

    async def test_unary_stream_ok(self):
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        call = self._stub.StreamingOutputCall(request)

        # No exception raised and no message swallowed.
        await call.wait_for_connection()

        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(_NUM_STREAM_RESPONSES, response_cnt)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_stream_unary_ok(self):
        call = self._stub.StreamingInputCall()

        # No exception raised and no message swallowed.
        await call.wait_for_connection()

        payload = messages_pb2.Payload(body=b'\0' * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(request)
        await call.done_writing()

        response = await call
        self.assertIsInstance(response, messages_pb2.StreamingInputCallResponse)
        self.assertEqual(_NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
                         response.aggregated_payload_size)

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_stream_stream_ok(self):
        call = self._stub.FullDuplexCall()

        # No exception raised and no message swallowed.
        await call.wait_for_connection()

        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(request)
            response = await call.read()
            self.assertIsInstance(response,
                                  messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        await call.done_writing()

        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_unary_unary_error(self):
        call = self._phony_channel.unary_unary(_TEST_METHOD)(_REQUEST)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.wait_for_connection()
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNAVAILABLE, rpc_error.code())

    async def test_unary_stream_error(self):
        call = self._phony_channel.unary_stream(_TEST_METHOD)(_REQUEST)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.wait_for_connection()
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNAVAILABLE, rpc_error.code())

    async def test_stream_unary_error(self):
        call = self._phony_channel.stream_unary(_TEST_METHOD)()

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.wait_for_connection()
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNAVAILABLE, rpc_error.code())

    async def test_stream_stream_error(self):
        call = self._phony_channel.stream_stream(_TEST_METHOD)()

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.wait_for_connection()
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNAVAILABLE, rpc_error.code())


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
