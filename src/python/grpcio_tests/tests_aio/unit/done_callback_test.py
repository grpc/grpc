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
"""Testing the done callbacks mechanism."""

import asyncio
import logging
import unittest
import time
import gc

import grpc
from grpc.experimental import aio
from tests_aio.unit._common import inject_callbacks
from tests_aio.unit._test_base import AioTestBase
from tests.unit.framework.common import test_constants
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests_aio.unit._test_server import start_test_server

_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 42


class TestDoneCallback(AioTestBase):

    async def setUp(self):
        address, self._server = await start_test_server()
        self._channel = aio.insecure_channel(address)
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def test_add_after_done(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        validation = inject_callbacks(call)
        await validation

    async def test_unary_unary(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        validation = inject_callbacks(call)

        self.assertEqual(grpc.StatusCode.OK, await call.code())

        await validation

    async def test_unary_stream(self):
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        call = self._stub.StreamingOutputCall(request)
        validation = inject_callbacks(call)

        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertIsInstance(response,
                                  messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(_NUM_STREAM_RESPONSES, response_cnt)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        await validation

    async def test_stream_unary(self):
        payload = messages_pb2.Payload(body=b'\0' * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        async def gen():
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request

        call = self._stub.StreamingInputCall(gen())
        validation = inject_callbacks(call)

        response = await call
        self.assertIsInstance(response, messages_pb2.StreamingInputCallResponse)
        self.assertEqual(_NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
                         response.aggregated_payload_size)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        await validation

    async def test_stream_stream(self):
        call = self._stub.FullDuplexCall()
        validation = inject_callbacks(call)

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
        await validation


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
