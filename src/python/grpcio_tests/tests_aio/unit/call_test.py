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
"""Tests behavior of the grpc.aio.UnaryUnaryCall class."""

import asyncio
import logging
import unittest
import datetime

import grpc

from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import test_constants
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase

_NUM_STREAM_RESPONSES = 5
_RESPONSE_PAYLOAD_SIZE = 42
_LOCAL_CANCEL_DETAILS_EXPECTATION = 'Locally cancelled by application!'
_RESPONSE_INTERVAL_US = test_constants.SHORT_TIMEOUT * 1000 * 1000


class TestUnaryUnaryCall(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_call_ok(self):
        async with aio.insecure_channel(self._server_target) as channel:
            hi = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = hi(messages_pb2.SimpleRequest())

            self.assertFalse(call.done())

            response = await call

            self.assertTrue(call.done())
            self.assertIsInstance(response, messages_pb2.SimpleResponse)
            self.assertEqual(await call.code(), grpc.StatusCode.OK)

            # Response is cached at call object level, reentrance
            # returns again the same response
            response_retry = await call
            self.assertIs(response, response_retry)

    async def test_call_rpc_error(self):
        async with aio.insecure_channel(self._server_target) as channel:
            empty_call_with_sleep = channel.unary_unary(
                "/grpc.testing.TestService/EmptyCall",
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString,
            )
            timeout = test_constants.SHORT_TIMEOUT / 2
            # TODO(https://github.com/grpc/grpc/issues/20869
            # Update once the async server is ready, change the
            # synchronization mechanism by removing the sleep(<timeout>)
            # as both components (client & server) will be on the same
            # process.
            call = empty_call_with_sleep(
                messages_pb2.SimpleRequest(), timeout=timeout)

            with self.assertRaises(grpc.RpcError) as exception_context:
                await call

            self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED,
                             exception_context.exception.code())

            self.assertTrue(call.done())
            self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, await
                             call.code())

            # Exception is cached at call object level, reentrance
            # returns again the same exception
            with self.assertRaises(grpc.RpcError) as exception_context_retry:
                await call

            self.assertIs(exception_context.exception,
                          exception_context_retry.exception)

    async def test_call_code_awaitable(self):
        async with aio.insecure_channel(self._server_target) as channel:
            hi = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = hi(messages_pb2.SimpleRequest())
            self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_call_details_awaitable(self):
        async with aio.insecure_channel(self._server_target) as channel:
            hi = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = hi(messages_pb2.SimpleRequest())
            self.assertEqual('', await call.details())

    async def test_cancel_unary_unary(self):
        async with aio.insecure_channel(self._server_target) as channel:
            hi = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = hi(messages_pb2.SimpleRequest())

            self.assertFalse(call.cancelled())

            # TODO(https://github.com/grpc/grpc/issues/20869) remove sleep.
            # Force the loop to execute the RPC task.
            await asyncio.sleep(0)

            self.assertTrue(call.cancel())
            self.assertFalse(call.cancel())

            with self.assertRaises(asyncio.CancelledError) as exception_context:
                await call

            self.assertTrue(call.cancelled())
            self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
            self.assertEqual(await call.details(),
                             'Locally cancelled by application!')

            # NOTE(lidiz) The CancelledError is almost always re-created,
            # so we might not want to use it to transmit data.
            # https://github.com/python/cpython/blob/edad4d89e357c92f70c0324b937845d652b20afd/Lib/asyncio/tasks.py#L785


class TestUnaryStreamCall(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_cancel_unary_stream(self):
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # Prepares the request
            request = messages_pb2.StreamingOutputCallRequest()
            for _ in range(_NUM_STREAM_RESPONSES):
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(
                        size=_RESPONSE_PAYLOAD_SIZE,
                        interval_us=_RESPONSE_INTERVAL_US,
                    ))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)
            self.assertFalse(call.cancelled())

            response = await call.read()
            self.assertIs(
                type(response), messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

            self.assertTrue(call.cancel())
            self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())
            self.assertEqual(_LOCAL_CANCEL_DETAILS_EXPECTATION, await
                             call.details())
            self.assertFalse(call.cancel())

            with self.assertRaises(grpc.RpcError) as exception_context:
                await call.read()
            self.assertTrue(call.cancelled())

    async def test_multiple_cancel_unary_stream(self):
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # Prepares the request
            request = messages_pb2.StreamingOutputCallRequest()
            for _ in range(_NUM_STREAM_RESPONSES):
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(
                        size=_RESPONSE_PAYLOAD_SIZE,
                        interval_us=_RESPONSE_INTERVAL_US,
                    ))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)
            self.assertFalse(call.cancelled())

            response = await call.read()
            self.assertIs(
                type(response), messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

            self.assertTrue(call.cancel())
            self.assertFalse(call.cancel())
            self.assertFalse(call.cancel())
            self.assertFalse(call.cancel())

            with self.assertRaises(grpc.RpcError) as exception_context:
                await call.read()

    async def test_early_cancel_unary_stream(self):
        """Test cancellation before receiving messages."""
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # Prepares the request
            request = messages_pb2.StreamingOutputCallRequest()
            for _ in range(_NUM_STREAM_RESPONSES):
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(
                        size=_RESPONSE_PAYLOAD_SIZE,
                        interval_us=_RESPONSE_INTERVAL_US,
                    ))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)

            self.assertFalse(call.cancelled())
            self.assertTrue(call.cancel())
            self.assertFalse(call.cancel())

            with self.assertRaises(grpc.RpcError) as exception_context:
                await call.read()

            self.assertTrue(call.cancelled())

            self.assertEqual(grpc.StatusCode.CANCELLED,
                             exception_context.exception.code())
            self.assertEqual(_LOCAL_CANCEL_DETAILS_EXPECTATION,
                             exception_context.exception.details())

            self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())
            self.assertEqual(_LOCAL_CANCEL_DETAILS_EXPECTATION, await
                             call.details())

    async def test_late_cancel_unary_stream(self):
        """Test cancellation after received all messages."""
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # Prepares the request
            request = messages_pb2.StreamingOutputCallRequest()
            for _ in range(_NUM_STREAM_RESPONSES):
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(
                        size=_RESPONSE_PAYLOAD_SIZE,))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)

            for _ in range(_NUM_STREAM_RESPONSES):
                response = await call.read()
                self.assertIs(
                    type(response), messages_pb2.StreamingOutputCallResponse)
                self.assertEqual(_RESPONSE_PAYLOAD_SIZE,
                                 len(response.payload.body))

            # After all messages received, it is possible that the final state
            # is received or on its way. It's basically a data race, so our
            # expectation here is do not crash :)
            call.cancel()
            self.assertIn(await call.code(),
                          [grpc.StatusCode.OK, grpc.StatusCode.CANCELLED])

    async def test_too_many_reads_unary_stream(self):
        """Test cancellation after received all messages."""
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # Prepares the request
            request = messages_pb2.StreamingOutputCallRequest()
            for _ in range(_NUM_STREAM_RESPONSES):
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(
                        size=_RESPONSE_PAYLOAD_SIZE,))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)

            for _ in range(_NUM_STREAM_RESPONSES):
                response = await call.read()
                self.assertIs(
                    type(response), messages_pb2.StreamingOutputCallResponse)
                self.assertEqual(_RESPONSE_PAYLOAD_SIZE,
                                 len(response.payload.body))

            # After the RPC is finished, further reads will lead to exception.
            self.assertEqual(await call.code(), grpc.StatusCode.OK)
            with self.assertRaises(asyncio.InvalidStateError):
                await call.read()

    async def test_unary_stream_async_generator(self):
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # Prepares the request
            request = messages_pb2.StreamingOutputCallRequest()
            for _ in range(_NUM_STREAM_RESPONSES):
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(
                        size=_RESPONSE_PAYLOAD_SIZE,))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)
            self.assertFalse(call.cancelled())

            async for response in call:
                self.assertIs(
                    type(response), messages_pb2.StreamingOutputCallResponse)
                self.assertEqual(_RESPONSE_PAYLOAD_SIZE,
                                 len(response.payload.body))

            self.assertEqual(await call.code(), grpc.StatusCode.OK)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
