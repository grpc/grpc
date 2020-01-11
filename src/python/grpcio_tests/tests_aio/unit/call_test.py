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
_UNREACHABLE_TARGET = '0.1:1111'

_INFINITE_INTERVAL_US = 2**31 - 1


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
        async with aio.insecure_channel(_UNREACHABLE_TARGET) as channel:
            hi = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString,
            )

            call = hi(messages_pb2.SimpleRequest(), timeout=0.1)

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

            self.assertTrue(call.cancel())
            self.assertFalse(call.cancel())

            with self.assertRaises(asyncio.CancelledError):
                await call

            # The info in the RpcError should match the info in Call object.
            self.assertTrue(call.cancelled())
            self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
            self.assertEqual(await call.details(),
                             'Locally cancelled by application!')

    async def test_cancel_unary_unary_in_task(self):
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            coro_started = asyncio.Event()
            call = stub.EmptyCall(messages_pb2.SimpleRequest())

            async def another_coro():
                coro_started.set()
                await call

            task = self.loop.create_task(another_coro())
            await coro_started.wait()

            self.assertFalse(task.done())
            task.cancel()

            self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

            with self.assertRaises(asyncio.CancelledError):
                await task


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
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

            self.assertTrue(call.cancel())
            self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())
            self.assertEqual(_LOCAL_CANCEL_DETAILS_EXPECTATION, await
                             call.details())
            self.assertFalse(call.cancel())

            with self.assertRaises(asyncio.CancelledError):
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
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

            self.assertTrue(call.cancel())
            self.assertFalse(call.cancel())
            self.assertFalse(call.cancel())
            self.assertFalse(call.cancel())

            with self.assertRaises(asyncio.CancelledError):
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

            with self.assertRaises(asyncio.CancelledError):
                await call.read()

            self.assertTrue(call.cancelled())

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
                self.assertIs(type(response),
                              messages_pb2.StreamingOutputCallResponse)
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
                self.assertIs(type(response),
                              messages_pb2.StreamingOutputCallResponse)
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
                self.assertIs(type(response),
                              messages_pb2.StreamingOutputCallResponse)
                self.assertEqual(_RESPONSE_PAYLOAD_SIZE,
                                 len(response.payload.body))

            self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_cancel_unary_stream_in_task_using_read(self):
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            coro_started = asyncio.Event()

            # Configs the server method to block forever
            request = messages_pb2.StreamingOutputCallRequest()
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                    interval_us=_INFINITE_INTERVAL_US,
                ))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)

            async def another_coro():
                coro_started.set()
                await call.read()

            task = self.loop.create_task(another_coro())
            await coro_started.wait()

            self.assertFalse(task.done())
            task.cancel()

            self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

            with self.assertRaises(asyncio.CancelledError):
                await task

    async def test_cancel_unary_stream_in_task_using_async_for(self):
        async with aio.insecure_channel(self._server_target) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            coro_started = asyncio.Event()

            # Configs the server method to block forever
            request = messages_pb2.StreamingOutputCallRequest()
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                    interval_us=_INFINITE_INTERVAL_US,
                ))

            # Invokes the actual RPC
            call = stub.StreamingOutputCall(request)

            async def another_coro():
                coro_started.set()
                async for _ in call:
                    pass

            task = self.loop.create_task(another_coro())
            await coro_started.wait()

            self.assertFalse(task.done())
            task.cancel()

            self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

            with self.assertRaises(asyncio.CancelledError):
                await task


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
