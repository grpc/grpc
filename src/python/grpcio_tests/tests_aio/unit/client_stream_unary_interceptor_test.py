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
import asyncio
import datetime
import logging
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import test_constants
from tests_aio.unit._common import CountingRequestIterator
from tests_aio.unit._common import inject_callbacks
from tests_aio.unit._constants import UNREACHABLE_TARGET
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_SHORT_TIMEOUT_S = 1.0

_NUM_STREAM_REQUESTS = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_INTERVAL_US = int(_SHORT_TIMEOUT_S * 1000 * 1000)


class _StreamUnaryInterceptorEmpty(aio.StreamUnaryClientInterceptor):
    async def intercept_stream_unary(
        self, continuation, client_call_details, request_iterator
    ):
        return await continuation(client_call_details, request_iterator)

    def assert_in_final_state(self, test: unittest.TestCase):
        pass


class _StreamUnaryInterceptorWithRequestIterator(
    aio.StreamUnaryClientInterceptor
):
    async def intercept_stream_unary(
        self, continuation, client_call_details, request_iterator
    ):
        self.request_iterator = CountingRequestIterator(request_iterator)
        call = await continuation(client_call_details, self.request_iterator)
        return call

    def assert_in_final_state(self, test: unittest.TestCase):
        test.assertEqual(
            _NUM_STREAM_REQUESTS, self.request_iterator.request_cnt
        )


class TestStreamUnaryClientInterceptor(AioTestBase):
    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_intercepts(self):
        for interceptor_class in (
            _StreamUnaryInterceptorEmpty,
            _StreamUnaryInterceptorWithRequestIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()
                channel = aio.insecure_channel(
                    self._server_target, interceptors=[interceptor]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                async def request_iterator():
                    for _ in range(_NUM_STREAM_REQUESTS):
                        yield request

                call = stub.StreamingInputCall(request_iterator())

                response = await call

                self.assertEqual(
                    _NUM_STREAM_REQUESTS * _REQUEST_PAYLOAD_SIZE,
                    response.aggregated_payload_size,
                )
                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), aio.Metadata())
                self.assertEqual(await call.trailing_metadata(), aio.Metadata())
                self.assertEqual(await call.details(), "")
                self.assertEqual(await call.debug_error_string(), "")
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                interceptor.assert_in_final_state(self)

                await channel.close()

    async def test_intercepts_using_write(self):
        for interceptor_class in (
            _StreamUnaryInterceptorEmpty,
            _StreamUnaryInterceptorWithRequestIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()
                channel = aio.insecure_channel(
                    self._server_target, interceptors=[interceptor]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                call = stub.StreamingInputCall()

                for _ in range(_NUM_STREAM_REQUESTS):
                    await call.write(request)

                await call.done_writing()

                response = await call

                self.assertEqual(
                    _NUM_STREAM_REQUESTS * _REQUEST_PAYLOAD_SIZE,
                    response.aggregated_payload_size,
                )
                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), aio.Metadata())
                self.assertEqual(await call.trailing_metadata(), aio.Metadata())
                self.assertEqual(await call.details(), "")
                self.assertEqual(await call.debug_error_string(), "")
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                interceptor.assert_in_final_state(self)

                await channel.close()

    async def test_add_done_callback_interceptor_task_not_finished(self):
        for interceptor_class in (
            _StreamUnaryInterceptorEmpty,
            _StreamUnaryInterceptorWithRequestIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                channel = aio.insecure_channel(
                    self._server_target, interceptors=[interceptor]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                async def request_iterator():
                    for _ in range(_NUM_STREAM_REQUESTS):
                        yield request

                call = stub.StreamingInputCall(request_iterator())

                validation = inject_callbacks(call)

                response = await call

                await validation

                await channel.close()

    async def test_add_done_callback_interceptor_task_finished(self):
        for interceptor_class in (
            _StreamUnaryInterceptorEmpty,
            _StreamUnaryInterceptorWithRequestIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                channel = aio.insecure_channel(
                    self._server_target, interceptors=[interceptor]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                async def request_iterator():
                    for _ in range(_NUM_STREAM_REQUESTS):
                        yield request

                call = stub.StreamingInputCall(request_iterator())

                response = await call

                validation = inject_callbacks(call)

                await validation

                await channel.close()

    async def test_multiple_interceptors_request_iterator(self):
        for interceptor_class in (
            _StreamUnaryInterceptorEmpty,
            _StreamUnaryInterceptorWithRequestIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptors = [interceptor_class(), interceptor_class()]
                channel = aio.insecure_channel(
                    self._server_target, interceptors=interceptors
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                async def request_iterator():
                    for _ in range(_NUM_STREAM_REQUESTS):
                        yield request

                call = stub.StreamingInputCall(request_iterator())

                response = await call

                self.assertEqual(
                    _NUM_STREAM_REQUESTS * _REQUEST_PAYLOAD_SIZE,
                    response.aggregated_payload_size,
                )
                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), aio.Metadata())
                self.assertEqual(await call.trailing_metadata(), aio.Metadata())
                self.assertEqual(await call.details(), "")
                self.assertEqual(await call.debug_error_string(), "")
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                for interceptor in interceptors:
                    interceptor.assert_in_final_state(self)

                await channel.close()

    async def test_intercepts_request_iterator_rpc_error(self):
        for interceptor_class in (
            _StreamUnaryInterceptorEmpty,
            _StreamUnaryInterceptorWithRequestIterator,
        ):
            with self.subTest(name=interceptor_class):
                channel = aio.insecure_channel(
                    UNREACHABLE_TARGET, interceptors=[interceptor_class()]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                # When there is an error the request iterator is no longer
                # consumed.
                async def request_iterator():
                    for _ in range(_NUM_STREAM_REQUESTS):
                        yield request

                call = stub.StreamingInputCall(request_iterator())

                with self.assertRaises(aio.AioRpcError) as exception_context:
                    await call

                self.assertEqual(
                    grpc.StatusCode.UNAVAILABLE,
                    exception_context.exception.code(),
                )
                self.assertTrue(call.done())
                self.assertEqual(grpc.StatusCode.UNAVAILABLE, await call.code())

                await channel.close()

    async def test_intercepts_request_iterator_rpc_error_using_write(self):
        for interceptor_class in (
            _StreamUnaryInterceptorEmpty,
            _StreamUnaryInterceptorWithRequestIterator,
        ):
            with self.subTest(name=interceptor_class):
                channel = aio.insecure_channel(
                    UNREACHABLE_TARGET, interceptors=[interceptor_class()]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                call = stub.StreamingInputCall()

                # When there is an error during the write, exception is raised.
                with self.assertRaises(asyncio.InvalidStateError):
                    for _ in range(_NUM_STREAM_REQUESTS):
                        await call.write(request)

                with self.assertRaises(aio.AioRpcError) as exception_context:
                    await call

                self.assertEqual(
                    grpc.StatusCode.UNAVAILABLE,
                    exception_context.exception.code(),
                )
                self.assertTrue(call.done())
                self.assertEqual(grpc.StatusCode.UNAVAILABLE, await call.code())

                await channel.close()

    async def test_cancel_before_rpc(self):
        interceptor_reached = asyncio.Event()
        wait_for_ever = self.loop.create_future()

        class Interceptor(aio.StreamUnaryClientInterceptor):
            async def intercept_stream_unary(
                self, continuation, client_call_details, request_iterator
            ):
                interceptor_reached.set()
                await wait_for_ever

        channel = aio.insecure_channel(
            self._server_target, interceptors=[Interceptor()]
        )
        stub = test_pb2_grpc.TestServiceStub(channel)

        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        call = stub.StreamingInputCall()

        self.assertFalse(call.cancelled())
        self.assertFalse(call.done())

        await interceptor_reached.wait()
        self.assertTrue(call.cancel())

        # When there is an error during the write, exception is raised.
        with self.assertRaises(asyncio.InvalidStateError):
            for _ in range(_NUM_STREAM_REQUESTS):
                await call.write(request)

        with self.assertRaises(asyncio.CancelledError):
            await call

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(await call.initial_metadata(), None)
        self.assertEqual(await call.trailing_metadata(), None)
        await channel.close()

    async def test_cancel_after_rpc(self):
        interceptor_reached = asyncio.Event()
        wait_for_ever = self.loop.create_future()

        class Interceptor(aio.StreamUnaryClientInterceptor):
            async def intercept_stream_unary(
                self, continuation, client_call_details, request_iterator
            ):
                call = await continuation(client_call_details, request_iterator)
                interceptor_reached.set()
                await wait_for_ever

        channel = aio.insecure_channel(
            self._server_target, interceptors=[Interceptor()]
        )
        stub = test_pb2_grpc.TestServiceStub(channel)

        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        call = stub.StreamingInputCall()

        self.assertFalse(call.cancelled())
        self.assertFalse(call.done())

        await interceptor_reached.wait()
        self.assertTrue(call.cancel())

        # When there is an error during the write, exception is raised.
        with self.assertRaises(asyncio.InvalidStateError):
            for _ in range(_NUM_STREAM_REQUESTS):
                await call.write(request)

        with self.assertRaises(asyncio.CancelledError):
            await call

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(await call.initial_metadata(), None)
        self.assertEqual(await call.trailing_metadata(), None)
        await channel.close()

    async def test_cancel_while_writing(self):
        # Test cancelation before making any write or after doing at least 1
        for num_writes_before_cancel in (0, 1):
            with self.subTest(
                name=f"Num writes before cancel: {num_writes_before_cancel}"
            ):
                channel = aio.insecure_channel(
                    UNREACHABLE_TARGET,
                    interceptors=[_StreamUnaryInterceptorWithRequestIterator()],
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                payload = messages_pb2.Payload(
                    body=b"\0" * _REQUEST_PAYLOAD_SIZE
                )
                request = messages_pb2.StreamingInputCallRequest(
                    payload=payload
                )

                call = stub.StreamingInputCall()

                with self.assertRaises(asyncio.InvalidStateError):
                    for i in range(_NUM_STREAM_REQUESTS):
                        if i == num_writes_before_cancel:
                            self.assertTrue(call.cancel())
                        await call.write(request)

                with self.assertRaises(asyncio.CancelledError):
                    await call

                self.assertTrue(call.cancelled())
                self.assertTrue(call.done())
                self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)

                await channel.close()

    async def test_cancel_by_the_interceptor(self):
        class Interceptor(aio.StreamUnaryClientInterceptor):
            async def intercept_stream_unary(
                self, continuation, client_call_details, request_iterator
            ):
                call = await continuation(client_call_details, request_iterator)
                call.cancel()
                return call

        channel = aio.insecure_channel(
            UNREACHABLE_TARGET, interceptors=[Interceptor()]
        )
        stub = test_pb2_grpc.TestServiceStub(channel)

        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        call = stub.StreamingInputCall()

        with self.assertRaises(asyncio.InvalidStateError):
            for i in range(_NUM_STREAM_REQUESTS):
                await call.write(request)

        with self.assertRaises(asyncio.CancelledError):
            await call

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)

        await channel.close()

    async def test_exception_raised_by_interceptor(self):
        class InterceptorException(Exception):
            pass

        class Interceptor(aio.StreamUnaryClientInterceptor):
            async def intercept_stream_unary(
                self, continuation, client_call_details, request_iterator
            ):
                raise InterceptorException

        channel = aio.insecure_channel(
            UNREACHABLE_TARGET, interceptors=[Interceptor()]
        )
        stub = test_pb2_grpc.TestServiceStub(channel)

        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        call = stub.StreamingInputCall()

        with self.assertRaises(InterceptorException):
            for i in range(_NUM_STREAM_REQUESTS):
                await call.write(request)

        with self.assertRaises(InterceptorException):
            await call

        await channel.close()

    async def test_intercepts_prohibit_mixing_style(self):
        channel = aio.insecure_channel(
            self._server_target, interceptors=[_StreamUnaryInterceptorEmpty()]
        )
        stub = test_pb2_grpc.TestServiceStub(channel)

        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        async def request_iterator():
            for _ in range(_NUM_STREAM_REQUESTS):
                yield request

        call = stub.StreamingInputCall(request_iterator())

        with self.assertRaises(grpc._cython.cygrpc.UsageError):
            await call.write(request)

        with self.assertRaises(grpc._cython.cygrpc.UsageError):
            await call.done_writing()

        await channel.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
