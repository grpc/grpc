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
"""Tests behavior of the Call classes."""

import asyncio
from concurrent import futures
import datetime
import logging
import random
import threading
import time
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests_aio.unit._constants import UNREACHABLE_TARGET
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_SHORT_TIMEOUT_S = datetime.timedelta(seconds=1).total_seconds()

_NUM_STREAM_RESPONSES = 5
_RESPONSE_PAYLOAD_SIZE = 42
_REQUEST_PAYLOAD_SIZE = 7
_LOCAL_CANCEL_DETAILS_EXPECTATION = "Locally cancelled by application!"
_RESPONSE_INTERVAL_US = int(_SHORT_TIMEOUT_S * 1000 * 1000)
_INFINITE_INTERVAL_US = 2**31 - 1

_NONDETERMINISTIC_ITERATIONS = 50
_NONDETERMINISTIC_SERVER_SLEEP_MAX_US = 1000


class _MulticallableTestMixin:
    async def setUp(self):
        address, self._server = await start_test_server()
        self._channel = aio.insecure_channel(address)
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)


class TestUnaryUnaryCall(_MulticallableTestMixin, AioTestBase):
    async def test_call_to_string(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

        self.assertTrue(str(call) is not None)
        self.assertTrue(repr(call) is not None)

        await call

        self.assertTrue(str(call) is not None)
        self.assertTrue(repr(call) is not None)

    async def test_call_ok(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

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
        async with aio.insecure_channel(UNREACHABLE_TARGET) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            call = stub.UnaryCall(messages_pb2.SimpleRequest())

            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call

            self.assertEqual(
                grpc.StatusCode.UNAVAILABLE, exception_context.exception.code()
            )

            self.assertTrue(call.done())
            self.assertEqual(grpc.StatusCode.UNAVAILABLE, await call.code())

    async def test_call_code_awaitable(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_call_details_awaitable(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        self.assertEqual("", await call.details())

    async def test_call_initial_metadata_awaitable(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        self.assertEqual(aio.Metadata(), await call.initial_metadata())

    async def test_call_trailing_metadata_awaitable(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        self.assertEqual(aio.Metadata(), await call.trailing_metadata())

    async def test_call_initial_metadata_cancelable(self):
        coro_started = asyncio.Event()
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

        async def coro():
            coro_started.set()
            await call.initial_metadata()

        task = self.loop.create_task(coro())
        await coro_started.wait()
        task.cancel()

        # Test that initial metadata can still be asked thought
        # a cancellation happened with the previous task
        self.assertEqual(aio.Metadata(), await call.initial_metadata())

    async def test_call_initial_metadata_multiple_waiters(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

        async def coro():
            return await call.initial_metadata()

        task1 = self.loop.create_task(coro())
        task2 = self.loop.create_task(coro())

        await call
        expected = [aio.Metadata() for _ in range(2)]
        self.assertEqual(expected, await asyncio.gather(*[task1, task2]))

    async def test_call_code_cancelable(self):
        coro_started = asyncio.Event()
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

        async def coro():
            coro_started.set()
            await call.code()

        task = self.loop.create_task(coro())
        await coro_started.wait()
        task.cancel()

        # Test that code can still be asked thought
        # a cancellation happened with the previous task
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_call_code_multiple_waiters(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

        async def coro():
            return await call.code()

        task1 = self.loop.create_task(coro())
        task2 = self.loop.create_task(coro())

        await call

        self.assertEqual(
            [grpc.StatusCode.OK, grpc.StatusCode.OK],
            await asyncio.gather(task1, task2),
        )

    async def test_cancel_unary_unary(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())

        self.assertFalse(call.cancelled())

        self.assertTrue(call.cancel())
        self.assertFalse(call.cancel())

        with self.assertRaises(asyncio.CancelledError):
            await call

        # The info in the RpcError should match the info in Call object.
        self.assertTrue(call.cancelled())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(
            await call.details(), "Locally cancelled by application!"
        )

    async def test_cancel_unary_unary_in_task(self):
        coro_started = asyncio.Event()
        call = self._stub.EmptyCall(messages_pb2.SimpleRequest())

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

    async def test_passing_credentials_fails_over_insecure_channel(self):
        call_credentials = grpc.composite_call_credentials(
            grpc.access_token_call_credentials("abc"),
            grpc.access_token_call_credentials("def"),
        )
        with self.assertRaisesRegex(
            aio.UsageError, "Call credentials are only valid on secure channels"
        ):
            self._stub.UnaryCall(
                messages_pb2.SimpleRequest(), credentials=call_credentials
            )


class TestUnaryStreamCall(_MulticallableTestMixin, AioTestBase):
    async def test_call_rpc_error(self):
        channel = aio.insecure_channel(UNREACHABLE_TARGET)
        request = messages_pb2.StreamingOutputCallRequest()
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            async for response in call:
                pass

        self.assertEqual(
            grpc.StatusCode.UNAVAILABLE, exception_context.exception.code()
        )

        self.assertTrue(call.done())
        self.assertEqual(grpc.StatusCode.UNAVAILABLE, await call.code())
        await channel.close()

    async def test_cancel_unary_stream(self):
        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                    interval_us=_RESPONSE_INTERVAL_US,
                )
            )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)
        self.assertFalse(call.cancelled())

        response = await call.read()
        self.assertIs(type(response), messages_pb2.StreamingOutputCallResponse)
        self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertTrue(call.cancel())
        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())
        self.assertEqual(
            _LOCAL_CANCEL_DETAILS_EXPECTATION, await call.details()
        )
        self.assertFalse(call.cancel())

        with self.assertRaises(asyncio.CancelledError):
            await call.read()
        self.assertTrue(call.cancelled())

    async def test_multiple_cancel_unary_stream(self):
        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                    interval_us=_RESPONSE_INTERVAL_US,
                )
            )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)
        self.assertFalse(call.cancelled())

        response = await call.read()
        self.assertIs(type(response), messages_pb2.StreamingOutputCallResponse)
        self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertTrue(call.cancel())
        self.assertFalse(call.cancel())
        self.assertFalse(call.cancel())
        self.assertFalse(call.cancel())

        with self.assertRaises(asyncio.CancelledError):
            await call.read()

    async def test_early_cancel_unary_stream(self):
        """Test cancellation before receiving messages."""
        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                    interval_us=_RESPONSE_INTERVAL_US,
                )
            )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)

        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertFalse(call.cancel())

        with self.assertRaises(asyncio.CancelledError):
            await call.read()

        self.assertTrue(call.cancelled())

        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())
        self.assertEqual(
            _LOCAL_CANCEL_DETAILS_EXPECTATION, await call.details()
        )

    async def test_late_cancel_unary_stream(self):
        """Test cancellation after received all messages."""
        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                )
            )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)

        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertIs(
                type(response), messages_pb2.StreamingOutputCallResponse
            )
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        # After all messages received, it is possible that the final state
        # is received or on its way. It's basically a data race, so our
        # expectation here is do not crash :)
        call.cancel()
        self.assertIn(
            await call.code(), [grpc.StatusCode.OK, grpc.StatusCode.CANCELLED]
        )

    async def test_too_many_reads_unary_stream(self):
        """Test calling read after received all messages fails."""
        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                )
            )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)

        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertIs(
                type(response), messages_pb2.StreamingOutputCallResponse
            )
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))
        self.assertIs(await call.read(), aio.EOF)

        # After the RPC is finished, further reads will lead to exception.
        self.assertEqual(await call.code(), grpc.StatusCode.OK)
        self.assertIs(await call.read(), aio.EOF)

    async def test_unary_stream_async_generator(self):
        """Sunny day test case for unary_stream."""
        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                )
            )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)
        self.assertFalse(call.cancelled())

        async for response in call:
            self.assertIs(
                type(response), messages_pb2.StreamingOutputCallResponse
            )
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_cancel_unary_stream_with_many_interleavings(self):
        """A cheap alternative to a structured fuzzer.

        Certain classes of error only appear for very specific interleavings of
        coroutines. Rather than inserting semi-private asyncio.Events throughout
        the implementation on which to coordinate and explicitly waiting on those
        in tests, we instead search for bugs over the space of interleavings by
        stochastically varying the durations of certain events within the test.
        """

        # We range over several orders of magnitude to ensure that switching platforms
        # (i.e. to slow CI machines) does not result in this test becoming a no-op.
        sleep_ranges = (10.0**-i for i in range(1, 4))
        for sleep_range in sleep_ranges:
            for _ in range(_NONDETERMINISTIC_ITERATIONS):
                interval_us = random.randrange(
                    _NONDETERMINISTIC_SERVER_SLEEP_MAX_US
                )
                sleep_secs = sleep_range * random.random()

                coro_started = asyncio.Event()

                # Configs the server method to block forever
                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(
                        size=1,
                        interval_us=interval_us,
                    )
                )

                # Invokes the actual RPC
                call = self._stub.StreamingOutputCall(request)

                unhandled_error = False

                async def another_coro():
                    nonlocal unhandled_error
                    coro_started.set()
                    try:
                        await call.read()
                    except asyncio.CancelledError:
                        pass
                    except Exception as e:
                        unhandled_error = True
                        raise

                task = self.loop.create_task(another_coro())
                await coro_started.wait()
                await asyncio.sleep(sleep_secs)

                task.cancel()

                try:
                    await task
                except asyncio.CancelledError:
                    pass

                self.assertFalse(unhandled_error)

    async def test_cancel_unary_stream_in_task_using_read(self):
        coro_started = asyncio.Event()

        # Configs the server method to block forever
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(
                size=_RESPONSE_PAYLOAD_SIZE,
                interval_us=_INFINITE_INTERVAL_US,
            )
        )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)

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
        coro_started = asyncio.Event()

        # Configs the server method to block forever
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(
                size=_RESPONSE_PAYLOAD_SIZE,
                interval_us=_INFINITE_INTERVAL_US,
            )
        )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)

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

    async def test_time_remaining(self):
        request = messages_pb2.StreamingOutputCallRequest()
        # First message comes back immediately
        request.response_parameters.append(
            messages_pb2.ResponseParameters(
                size=_RESPONSE_PAYLOAD_SIZE,
            )
        )
        # Second message comes back after a unit of wait time
        request.response_parameters.append(
            messages_pb2.ResponseParameters(
                size=_RESPONSE_PAYLOAD_SIZE,
                interval_us=_RESPONSE_INTERVAL_US,
            )
        )

        call = self._stub.StreamingOutputCall(
            request, timeout=_SHORT_TIMEOUT_S * 2
        )

        response = await call.read()
        self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        # Should be around the same as the timeout
        remained_time = call.time_remaining()
        self.assertGreater(remained_time, _SHORT_TIMEOUT_S * 3 / 2)
        self.assertLess(remained_time, _SHORT_TIMEOUT_S * 5 / 2)

        response = await call.read()
        self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        # Should be around the timeout minus a unit of wait time
        remained_time = call.time_remaining()
        self.assertGreater(remained_time, _SHORT_TIMEOUT_S / 2)
        self.assertLess(remained_time, _SHORT_TIMEOUT_S * 3 / 2)

        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_empty_responses(self):
        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters()
            )

        # Invokes the actual RPC
        call = self._stub.StreamingOutputCall(request)

        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertIs(
                type(response), messages_pb2.StreamingOutputCallResponse
            )
            self.assertEqual(b"", response.SerializeToString())

        self.assertEqual(grpc.StatusCode.OK, await call.code())


class TestStreamUnaryCall(_MulticallableTestMixin, AioTestBase):
    async def test_cancel_stream_unary(self):
        call = self._stub.StreamingInputCall()

        # Prepares the request
        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        # Sends out requests
        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(request)

        # Cancels the RPC
        self.assertFalse(call.done())
        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertTrue(call.cancelled())

        await call.done_writing()

        with self.assertRaises(asyncio.CancelledError):
            await call

    async def test_early_cancel_stream_unary(self):
        call = self._stub.StreamingInputCall()

        # Cancels the RPC
        self.assertFalse(call.done())
        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertTrue(call.cancelled())

        with self.assertRaises(asyncio.InvalidStateError):
            await call.write(messages_pb2.StreamingInputCallRequest())

        # Should be no-op
        await call.done_writing()

        with self.assertRaises(asyncio.CancelledError):
            await call

    async def test_write_after_done_writing(self):
        call = self._stub.StreamingInputCall()

        # Prepares the request
        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        # Sends out requests
        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(request)

        # Should be no-op
        await call.done_writing()

        with self.assertRaises(asyncio.InvalidStateError):
            await call.write(messages_pb2.StreamingInputCallRequest())

        response = await call
        self.assertIsInstance(response, messages_pb2.StreamingInputCallResponse)
        self.assertEqual(
            _NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
            response.aggregated_payload_size,
        )

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_error_in_async_generator(self):
        # Server will pause between responses
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(
                size=_RESPONSE_PAYLOAD_SIZE,
                interval_us=_RESPONSE_INTERVAL_US,
            )
        )

        # We expect the request iterator to receive the exception
        request_iterator_received_the_exception = asyncio.Event()

        async def request_iterator():
            with self.assertRaises(asyncio.CancelledError):
                for _ in range(_NUM_STREAM_RESPONSES):
                    yield request
                    await asyncio.sleep(_SHORT_TIMEOUT_S)
            request_iterator_received_the_exception.set()

        call = self._stub.StreamingInputCall(request_iterator())

        # Cancel the RPC after at least one response
        async def cancel_later():
            await asyncio.sleep(_SHORT_TIMEOUT_S * 2)
            call.cancel()

        cancel_later_task = self.loop.create_task(cancel_later())

        with self.assertRaises(asyncio.CancelledError):
            await call

        await request_iterator_received_the_exception.wait()

        # No failures in the cancel later task!
        await cancel_later_task

    async def test_normal_iterable_requests(self):
        # Prepares the request
        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)
        requests = [request] * _NUM_STREAM_RESPONSES

        # Sends out requests
        call = self._stub.StreamingInputCall(requests)

        # RPC should succeed
        response = await call
        self.assertIsInstance(response, messages_pb2.StreamingInputCallResponse)
        self.assertEqual(
            _NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
            response.aggregated_payload_size,
        )

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_call_rpc_error(self):
        async with aio.insecure_channel(UNREACHABLE_TARGET) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            # The error should be raised automatically without any traffic.
            call = stub.StreamingInputCall()
            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call

            self.assertEqual(
                grpc.StatusCode.UNAVAILABLE, exception_context.exception.code()
            )

            self.assertTrue(call.done())
            self.assertEqual(grpc.StatusCode.UNAVAILABLE, await call.code())

    async def test_timeout(self):
        call = self._stub.StreamingInputCall(timeout=_SHORT_TIMEOUT_S)

        # The error should be raised automatically without any traffic.
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call

        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, rpc_error.code())
        self.assertTrue(call.done())
        self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, await call.code())


# Prepares the request that stream in a ping-pong manner.
_STREAM_OUTPUT_REQUEST_ONE_RESPONSE = messages_pb2.StreamingOutputCallRequest()
_STREAM_OUTPUT_REQUEST_ONE_RESPONSE.response_parameters.append(
    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
)
_STREAM_OUTPUT_REQUEST_ONE_EMPTY_RESPONSE = (
    messages_pb2.StreamingOutputCallRequest()
)
_STREAM_OUTPUT_REQUEST_ONE_EMPTY_RESPONSE.response_parameters.append(
    messages_pb2.ResponseParameters()
)


class TestStreamStreamCall(_MulticallableTestMixin, AioTestBase):
    async def test_cancel(self):
        # Invokes the actual RPC
        call = self._stub.FullDuplexCall()

        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(_STREAM_OUTPUT_REQUEST_ONE_RESPONSE)
            response = await call.read()
            self.assertIsInstance(
                response, messages_pb2.StreamingOutputCallResponse
            )
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        # Cancels the RPC
        self.assertFalse(call.done())
        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertTrue(call.cancelled())
        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

    async def test_cancel_with_pending_read(self):
        call = self._stub.FullDuplexCall()

        await call.write(_STREAM_OUTPUT_REQUEST_ONE_RESPONSE)

        # Cancels the RPC
        self.assertFalse(call.done())
        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertTrue(call.cancelled())
        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

    async def test_cancel_with_ongoing_read(self):
        call = self._stub.FullDuplexCall()
        coro_started = asyncio.Event()

        async def read_coro():
            coro_started.set()
            await call.read()

        read_task = self.loop.create_task(read_coro())
        await coro_started.wait()
        self.assertFalse(read_task.done())

        # Cancels the RPC
        self.assertFalse(call.done())
        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertTrue(call.cancelled())
        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

    async def test_early_cancel(self):
        call = self._stub.FullDuplexCall()

        # Cancels the RPC
        self.assertFalse(call.done())
        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertTrue(call.cancelled())
        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

    async def test_cancel_after_done_writing(self):
        call = self._stub.FullDuplexCall()
        request_with_delay = messages_pb2.StreamingOutputCallRequest()
        request_with_delay.response_parameters.append(
            messages_pb2.ResponseParameters(interval_us=10000)
        )
        await call.write(request_with_delay)
        await call.write(request_with_delay)
        await call.done_writing()

        # Cancels the RPC
        self.assertFalse(call.cancelled())
        self.assertTrue(call.cancel())
        self.assertTrue(call.cancelled())
        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

    async def test_late_cancel(self):
        call = self._stub.FullDuplexCall()
        await call.done_writing()
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        # Cancels the RPC
        self.assertTrue(call.done())
        self.assertFalse(call.cancelled())
        self.assertFalse(call.cancel())
        self.assertFalse(call.cancelled())

        # Status is still OK
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_async_generator(self):
        async def request_generator():
            yield _STREAM_OUTPUT_REQUEST_ONE_RESPONSE
            yield _STREAM_OUTPUT_REQUEST_ONE_RESPONSE

        call = self._stub.FullDuplexCall(request_generator())
        async for response in call:
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_too_many_reads(self):
        async def request_generator():
            for _ in range(_NUM_STREAM_RESPONSES):
                yield _STREAM_OUTPUT_REQUEST_ONE_RESPONSE

        call = self._stub.FullDuplexCall(request_generator())
        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))
        self.assertIs(await call.read(), aio.EOF)

        self.assertEqual(await call.code(), grpc.StatusCode.OK)
        # After the RPC finished, the read should also produce EOF
        self.assertIs(await call.read(), aio.EOF)

    async def test_read_write_after_done_writing(self):
        call = self._stub.FullDuplexCall()

        # Writes two requests, and pending two requests
        await call.write(_STREAM_OUTPUT_REQUEST_ONE_RESPONSE)
        await call.write(_STREAM_OUTPUT_REQUEST_ONE_RESPONSE)
        await call.done_writing()

        # Further write should fail
        with self.assertRaises(asyncio.InvalidStateError):
            await call.write(_STREAM_OUTPUT_REQUEST_ONE_RESPONSE)

        # But read should be unaffected
        response = await call.read()
        self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))
        response = await call.read()
        self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_error_in_async_generator(self):
        # Server will pause between responses
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(
                size=_RESPONSE_PAYLOAD_SIZE,
                interval_us=_RESPONSE_INTERVAL_US,
            )
        )

        # We expect the request iterator to receive the exception
        request_iterator_received_the_exception = asyncio.Event()

        async def request_iterator():
            with self.assertRaises(asyncio.CancelledError):
                for _ in range(_NUM_STREAM_RESPONSES):
                    yield request
                    await asyncio.sleep(_SHORT_TIMEOUT_S)
            request_iterator_received_the_exception.set()

        call = self._stub.FullDuplexCall(request_iterator())

        # Cancel the RPC after at least one response
        async def cancel_later():
            await asyncio.sleep(_SHORT_TIMEOUT_S * 2)
            call.cancel()

        cancel_later_task = self.loop.create_task(cancel_later())

        with self.assertRaises(asyncio.CancelledError):
            async for response in call:
                self.assertEqual(
                    _RESPONSE_PAYLOAD_SIZE, len(response.payload.body)
                )

        await request_iterator_received_the_exception.wait()

        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())
        # No failures in the cancel later task!
        await cancel_later_task

    async def test_normal_iterable_requests(self):
        requests = [_STREAM_OUTPUT_REQUEST_ONE_RESPONSE] * _NUM_STREAM_RESPONSES

        call = self._stub.FullDuplexCall(iter(requests))
        async for response in call:
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_empty_ping_pong(self):
        call = self._stub.FullDuplexCall()
        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(_STREAM_OUTPUT_REQUEST_ONE_EMPTY_RESPONSE)
            response = await call.read()
            self.assertEqual(b"", response.SerializeToString())
        await call.done_writing()
        self.assertEqual(await call.code(), grpc.StatusCode.OK)


_EARLY_STATUS_CODE = grpc.StatusCode.INVALID_ARGUMENT
_EARLY_STATUS_DETAILS = "Aborted after the first request"
_EARLY_RESPONSE = b"\1" * 16
_SMALL_REQUEST = b"\0" * 16
# Larger than the HTTP/2 flow-control window, so a send of it cannot complete
# until the server reads it.
_OVERSIZED_REQUEST = b"\0" * (8 * 1024 * 1024)
# Long enough for Core, on its own threads, to process a frame and for the
# completion-queue poller to enqueue the resulting completion, while the event
# loop is held.
_HOLD_LOOP_S = 0.2
_STREAM_UNARY_PENDING_SEND = "/test/StreamUnaryPendingSend"
_STREAM_UNARY_CLOSED_STREAM = "/test/StreamUnaryClosedStream"
_STREAM_UNARY_CLOSED_STREAM_OK = "/test/StreamUnaryClosedStreamOk"
_STREAM_STREAM_PENDING_SEND = "/test/StreamStreamPendingSend"
_STREAM_STREAM_CLOSED_STREAM = "/test/StreamStreamClosedStream"


class _EarlyStatusServer:
    """A sync server on its own threads, coordinated by threading events.

    The aio completion-queue poller and its wake-up socket are shared by every
    aio loop in the process, so an aio server here would compete with the
    client's loop for the client's own completions and forward some of them
    with call_soon_threadsafe, at arbitrary latency. A sync server uses its
    own completion queues, leaving the client's loop as the sole consumer.
    Being off the client's loop also lets a test hold that loop while the
    server acts, which is what pins down the order of the completions the
    client observes.
    """

    def __init__(self):
        # A test sets this once the pending-send handlers may abort.
        self.release = threading.Event()
        # A test sets this once its loop is about to block; the closed-stream
        # handlers end the RPC only after it.
        self.client_blocked = threading.Event()
        # The closed-stream handlers set this just before ending the RPC; the
        # status follows on the wire as soon as the handler returns.
        self.ending = threading.Event()
        # The stream-stream handlers are the same functions as the stream-unary
        # ones: they always abort, so the server never asks them for a
        # response iterator.
        handlers = {
            _STREAM_UNARY_PENDING_SEND: grpc.stream_unary_rpc_method_handler(
                self._abort_after_release
            ),
            _STREAM_UNARY_CLOSED_STREAM: grpc.stream_unary_rpc_method_handler(
                self._abort_once_client_blocked
            ),
            _STREAM_UNARY_CLOSED_STREAM_OK: grpc.stream_unary_rpc_method_handler(
                self._finish_once_client_blocked
            ),
            _STREAM_STREAM_PENDING_SEND: grpc.stream_stream_rpc_method_handler(
                self._abort_after_release
            ),
            _STREAM_STREAM_CLOSED_STREAM: grpc.stream_stream_rpc_method_handler(
                self._abort_once_client_blocked
            ),
        }
        self._server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=1),
            options=(("grpc.so_reuseport", 0),),
        )
        self._server.add_generic_rpc_handlers(
            (
                grpc.method_handlers_generic_handler(
                    "test",
                    {
                        path.rsplit("/", 1)[1]: handler
                        for path, handler in handlers.items()
                    },
                ),
            )
        )
        self._port = self._server.add_insecure_port("[::]:0")

    def start(self) -> str:
        self._server.start()
        return "localhost:%d" % self._port

    def stop(self):
        self._server.stop(None)

    def _abort_after_release(self, request_iterator, context):
        next(request_iterator)
        self.release.wait()
        context.abort(_EARLY_STATUS_CODE, _EARLY_STATUS_DETAILS)

    def _abort_once_client_blocked(self, request_iterator, context):
        # Initial metadata first, so that a stream-unary client has its
        # receive-status operation outstanding before the stream closes.
        context.send_initial_metadata(())
        next(request_iterator)
        self.client_blocked.wait()
        self.ending.set()
        context.abort(_EARLY_STATUS_CODE, _EARLY_STATUS_DETAILS)

    def _finish_once_client_blocked(self, request_iterator, context):
        # The same as above, except that the RPC finishes with OK. The
        # client's next write then has to fail like any other write after
        # completion, and the response has to reach the caller.
        context.send_initial_metadata(())
        next(request_iterator)
        self.client_blocked.wait()
        self.ending.set()
        return _EARLY_RESPONSE


class TestEarlyServerStatus(AioTestBase):
    """The server ends a stream-request RPC while the client is still writing.

    Core delivers the server's status, and the client must report that rather
    than an INTERNAL error for a write that could not be delivered
    (https://github.com/grpc/grpc/issues/36066). There are two cases, and each
    test forces one of them:

    * A send is pending in Core when the stream closes. For a NO_ERROR reset
      after trailers, Core completes it cleanly, so the write returns.
    * A send is issued after Core closed the stream but before the loop has
      processed the status. Core fails it synchronously, and the loop then
      processes the status and the failure together, in that order.

    Two tests end the RPC with OK instead of an abort. The write that lost
    the race then fails with InvalidStateError, as any write after completion
    does, and the response reaches the caller.
    """

    async def setUp(self):
        self._server = _EarlyStatusServer()
        self._channel = aio.insecure_channel(self._server.start())

    async def tearDown(self):
        await self._channel.close()
        self._server.stop()

    def _assert_early_status(self, rpc_error: aio.AioRpcError):
        self.assertEqual(_EARLY_STATUS_CODE, rpc_error.code())
        self.assertEqual(_EARLY_STATUS_DETAILS, rpc_error.details())

    async def _write_pending_send(self, call):
        await call.write(_SMALL_REQUEST)
        write_task = self.loop.create_task(call.write(_OVERSIZED_REQUEST))
        # Nothing in the write path suspends before grpc_call_start_batch, so
        # one yield is enough for the send to be pending in Core. It stays
        # pending because it exceeds the flow-control window and the server
        # is parked.
        await asyncio.sleep(0)
        self._server.release.set()
        # Core completes the pending send when the stream closes. If Core
        # failed the send instead, the write would have to report the
        # server's status rather than a synthesized one.
        try:
            await write_task
        except aio.AioRpcError as rpc_error:
            self._assert_early_status(rpc_error)

    async def test_stream_unary_pending_send(self):
        call = self._channel.stream_unary(_STREAM_UNARY_PENDING_SEND)()
        await self._write_pending_send(call)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        self._assert_early_status(exception_context.exception)

    async def test_stream_stream_pending_send(self):
        call = self._channel.stream_stream(_STREAM_STREAM_PENDING_SEND)()
        await self._write_pending_send(call)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.read()
        self._assert_early_status(exception_context.exception)

    async def _let_server_close_stream(self, call):
        """Returns with the status queued in Core but not yet processed.

        Completions reach the loop through a reader callback on a wake-up
        socket, which the loop schedules at the start of an iteration when a
        byte is readable. Handles scheduled while this step runs therefore
        come before the reader that will process the status.
        """
        await call.initial_metadata()
        # Let the loop consume any wake-up bytes left from earlier
        # completions, so that no reader is already scheduled.
        await asyncio.sleep(0.01)
        self._server.client_blocked.set()
        self._server.ending.wait()
        # Hold the loop while Core closes the stream and the poller enqueues
        # the status completion.
        time.sleep(_HOLD_LOOP_S)

    async def _requests_resuming_after_close(self, closed: asyncio.Event):
        yield _SMALL_REQUEST
        await closed.wait()
        yield _SMALL_REQUEST

    async def _write_after_close_async_generator(self, call, closed):
        await self._let_server_close_stream(call)
        # The consumer wakes and issues the write into the closed stream; Core
        # fails it synchronously. Hold the loop once more so the failure is
        # enqueued before the reader processes the status and the failure
        # together.
        closed.set()
        self.loop.call_soon(time.sleep, _HOLD_LOOP_S)

    async def test_stream_unary_write_after_close_async_generator(self):
        closed = asyncio.Event()
        call = self._channel.stream_unary(_STREAM_UNARY_CLOSED_STREAM)(
            self._requests_resuming_after_close(closed)
        )
        await self._write_after_close_async_generator(call, closed)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        self._assert_early_status(exception_context.exception)

    async def test_stream_stream_write_after_close_async_generator(self):
        closed = asyncio.Event()
        call = self._channel.stream_stream(_STREAM_STREAM_CLOSED_STREAM)(
            self._requests_resuming_after_close(closed)
        )
        # Start reading before the stream closes, so that the read's EOF is
        # queued ahead of the status and the read must wait for the status
        # to be processed before it can report it.
        read_task = self.loop.create_task(call.__aiter__().__anext__())
        await self._write_after_close_async_generator(call, closed)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await read_task
        self._assert_early_status(exception_context.exception)

    async def _write_after_close_reader_writer(self, call):
        await call.write(_SMALL_REQUEST)
        await self._let_server_close_stream(call)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.write(_SMALL_REQUEST)
        self._assert_early_status(exception_context.exception)

    async def test_stream_unary_write_after_close_reader_writer(self):
        call = self._channel.stream_unary(_STREAM_UNARY_CLOSED_STREAM)()
        await self._write_after_close_reader_writer(call)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        self._assert_early_status(exception_context.exception)

    async def test_stream_stream_write_after_close_reader_writer(self):
        call = self._channel.stream_stream(_STREAM_STREAM_CLOSED_STREAM)()
        await self._write_after_close_reader_writer(call)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.read()
        self._assert_early_status(exception_context.exception)

    async def test_stream_unary_write_after_close_ok_async_generator(self):
        closed = asyncio.Event()
        call = self._channel.stream_unary(_STREAM_UNARY_CLOSED_STREAM_OK)(
            self._requests_resuming_after_close(closed)
        )
        await self._write_after_close_async_generator(call, closed)
        self.assertEqual(_EARLY_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_stream_unary_write_after_close_ok_reader_writer(self):
        call = self._channel.stream_unary(_STREAM_UNARY_CLOSED_STREAM_OK)()
        await call.write(_SMALL_REQUEST)
        await self._let_server_close_stream(call)
        # The status this write would have masked is OK, so the write fails
        # as any write after completion does, and the response is intact.
        with self.assertRaises(asyncio.InvalidStateError):
            await call.write(_SMALL_REQUEST)
        self.assertEqual(_EARLY_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
