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
"""Tests client interceptors that implement multiple interceptor types."""

import logging
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 42


class MultiClientInterceptor(
    aio.UnaryUnaryClientInterceptor,
    aio.UnaryStreamClientInterceptor,
    aio.StreamUnaryClientInterceptor,
    aio.StreamStreamClientInterceptor,
):
    def __init__(self):
        self.intercepted_types = []

    async def intercept_unary_unary(
        self, continuation, client_call_details, request
    ):
        self.intercepted_types.append("unary_unary")
        return await continuation(client_call_details, request)

    async def intercept_unary_stream(
        self, continuation, client_call_details, request
    ):
        self.intercepted_types.append("unary_stream")
        return await continuation(client_call_details, request)

    async def intercept_stream_unary(
        self, continuation, client_call_details, request_iterator
    ):
        self.intercepted_types.append("stream_unary")
        return await continuation(client_call_details, request_iterator)

    async def intercept_stream_stream(
        self, continuation, client_call_details, request_iterator
    ):
        self.intercepted_types.append("stream_stream")
        return await continuation(client_call_details, request_iterator)


class TestMultiTypeClientInterceptor(AioTestBase):
    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_multi_type_client_interceptor(self):
        interceptor = MultiClientInterceptor()
        async with aio.insecure_channel(
            self._server_target, interceptors=[interceptor]
        ) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # 1. Unary-Unary
            unary_response = await stub.UnaryCall(messages_pb2.SimpleRequest())
            self.assertIsInstance(unary_response, messages_pb2.SimpleResponse)

            # 2. Unary-Stream
            stream_out_request = messages_pb2.StreamingOutputCallRequest()
            stream_out_request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
            )
            async for _ in stub.StreamingOutputCall(stream_out_request):
                pass

            # 3. Stream-Unary
            stream_in_call = stub.StreamingInputCall()
            await stream_in_call.write(
                messages_pb2.StreamingInputCallRequest(
                    payload=messages_pb2.Payload(
                        body=b"\0" * _REQUEST_PAYLOAD_SIZE
                    )
                )
            )
            await stream_in_call.done_writing()
            await stream_in_call

            # 4. Stream-Stream
            full_duplex_call = stub.FullDuplexCall()
            await full_duplex_call.write(stream_out_request)
            await full_duplex_call.read()
            await full_duplex_call.done_writing()
            self.assertEqual(grpc.StatusCode.OK, await full_duplex_call.code())

            self.assertEqual(
                interceptor.intercepted_types,
                [
                    "unary_unary",
                    "unary_stream",
                    "stream_unary",
                    "stream_stream",
                ],
            )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
