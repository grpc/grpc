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
"""Tests the behaviour of the Call classes under a secure channel."""

import unittest
import logging

import grpc
from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server
from tests.unit import resources

_SERVER_HOST_OVERRIDE = 'foo.test.google.fr'
_NUM_STREAM_RESPONSES = 5
_RESPONSE_PAYLOAD_SIZE = 42


class _SecureCallMixin:
    """A Mixin to run the call tests over a secure channel."""

    async def setUp(self):
        server_credentials = grpc.ssl_server_credentials([
            (resources.private_key(), resources.certificate_chain())
        ])
        channel_credentials = grpc.ssl_channel_credentials(
            resources.test_root_certificates())

        self._server_address, self._server = await start_test_server(
            secure=True, server_credentials=server_credentials)
        channel_options = ((
            'grpc.ssl_target_name_override',
            _SERVER_HOST_OVERRIDE,
        ),)
        self._channel = aio.secure_channel(self._server_address,
                                           channel_credentials, channel_options)
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)


class TestUnaryUnarySecureCall(_SecureCallMixin, AioTestBase):
    """unary_unary Calls made over a secure channel."""

    async def test_call_ok_over_secure_channel(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        response = await call
        self.assertIsInstance(response, messages_pb2.SimpleResponse)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_call_with_credentials(self):
        call_credentials = grpc.composite_call_credentials(
            grpc.access_token_call_credentials("abc"),
            grpc.access_token_call_credentials("def"),
        )
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest(),
                                    credentials=call_credentials)
        response = await call

        self.assertIsInstance(response, messages_pb2.SimpleResponse)


class TestUnaryStreamSecureCall(_SecureCallMixin, AioTestBase):
    """unary_stream calls over a secure channel"""

    async def test_unary_stream_async_generator_secure(self):
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.extend(
            messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE,)
            for _ in range(_NUM_STREAM_RESPONSES))
        call_credentials = grpc.composite_call_credentials(
            grpc.access_token_call_credentials("abc"),
            grpc.access_token_call_credentials("def"),
        )
        call = self._stub.StreamingOutputCall(request,
                                              credentials=call_credentials)

        async for response in call:
            self.assertIsInstance(response,
                                  messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(len(response.payload.body), _RESPONSE_PAYLOAD_SIZE)

        self.assertEqual(await call.code(), grpc.StatusCode.OK)


# Prepares the request that stream in a ping-pong manner.
_STREAM_OUTPUT_REQUEST_ONE_RESPONSE = messages_pb2.StreamingOutputCallRequest()
_STREAM_OUTPUT_REQUEST_ONE_RESPONSE.response_parameters.append(
    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))


class TestStreamStreamSecureCall(_SecureCallMixin, AioTestBase):
    _STREAM_ITERATIONS = 2

    async def test_async_generator_secure_channel(self):

        async def request_generator():
            for _ in range(self._STREAM_ITERATIONS):
                yield _STREAM_OUTPUT_REQUEST_ONE_RESPONSE

        call_credentials = grpc.composite_call_credentials(
            grpc.access_token_call_credentials("abc"),
            grpc.access_token_call_credentials("def"),
        )

        call = self._stub.FullDuplexCall(request_generator(),
                                         credentials=call_credentials)
        async for response in call:
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(await call.code(), grpc.StatusCode.OK)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
