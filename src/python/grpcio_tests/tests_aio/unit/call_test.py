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
import asyncio
import logging
import unittest

import grpc

from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2
from tests.unit.framework.common import test_constants
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase


class TestAioRpcError(unittest.TestCase):
    _TEST_INITIAL_METADATA = ("initial metadata",)
    _TEST_TRAILING_METADATA = ("trailing metadata",)

    def test_attributes(self):
        aio_rpc_error = aio.AioRpcError(
            grpc.StatusCode.CANCELLED,
            "details",
            initial_metadata=self._TEST_INITIAL_METADATA,
            trailing_metadata=self._TEST_TRAILING_METADATA)
        self.assertEqual(aio_rpc_error.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(aio_rpc_error.details(), "details")
        self.assertEqual(aio_rpc_error.initial_metadata(),
                         self._TEST_INITIAL_METADATA)
        self.assertEqual(aio_rpc_error.trailing_metadata(),
                         self._TEST_TRAILING_METADATA)


class TestCall(AioTestBase):

    def test_call_ok(self):

        async def coro():
            server_target, _ = await start_test_server()  # pylint: disable=unused-variable

            async with aio.insecure_channel(server_target) as channel:
                hi = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )
                call = hi(messages_pb2.SimpleRequest())

                self.assertFalse(call.done())

                response = await call

                self.assertTrue(call.done())
                self.assertEqual(type(response), messages_pb2.SimpleResponse)
                self.assertEqual(await call.code(), grpc.StatusCode.OK)

                # Response is cached at call object level, reentrance
                # returns again the same response
                response_retry = await call
                self.assertIs(response, response_retry)

        self.loop.run_until_complete(coro())

    def test_call_rpc_error(self):

        async def coro():
            server_target, _ = await start_test_server()  # pylint: disable=unused-variable

            async with aio.insecure_channel(server_target) as channel:
                empty_call_with_sleep = channel.unary_unary(
                    "/grpc.testing.TestService/EmptyCall",
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.
                    FromString,
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

                self.assertTrue(call.done())
                self.assertEqual(await call.code(),
                                 grpc.StatusCode.DEADLINE_EXCEEDED)

                # Exception is cached at call object level, reentrance
                # returns again the same exception
                with self.assertRaises(
                        grpc.RpcError) as exception_context_retry:
                    await call

                self.assertIs(exception_context.exception,
                              exception_context_retry.exception)

        self.loop.run_until_complete(coro())

    def test_call_code_awaitable(self):

        async def coro():
            server_target, _ = await start_test_server()  # pylint: disable=unused-variable

            async with aio.insecure_channel(server_target) as channel:
                hi = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )
                call = hi(messages_pb2.SimpleRequest())
                self.assertEqual(await call.code(), grpc.StatusCode.OK)

        self.loop.run_until_complete(coro())

    def test_call_details_awaitable(self):

        async def coro():
            server_target, _ = await start_test_server()  # pylint: disable=unused-variable

            async with aio.insecure_channel(server_target) as channel:
                hi = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )
                call = hi(messages_pb2.SimpleRequest())
                self.assertEqual(await call.details(), None)

        self.loop.run_until_complete(coro())

    def test_cancel(self):

        async def coro():
            server_target, _ = await start_test_server()  # pylint: disable=unused-variable

            async with aio.insecure_channel(server_target) as channel:
                hi = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )
                call = hi(messages_pb2.SimpleRequest())

                self.assertFalse(call.cancelled())

                # TODO(https://github.com/grpc/grpc/issues/20869) remove sleep.
                # Force the loop to execute the RPC task.
                await asyncio.sleep(0)

                self.assertTrue(call.cancel())
                self.assertTrue(call.cancelled())
                self.assertFalse(call.cancel())

                with self.assertRaises(
                        asyncio.CancelledError) as exception_context:
                    await call

                self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
                self.assertEqual(await call.details(),
                                 'Locally cancelled by application!')

                # Exception is cached at call object level, reentrance
                # returns again the same exception
                with self.assertRaises(
                        asyncio.CancelledError) as exception_context_retry:
                    await call

                self.assertIs(exception_context.exception,
                              exception_context_retry.exception)

        self.loop.run_until_complete(coro())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
