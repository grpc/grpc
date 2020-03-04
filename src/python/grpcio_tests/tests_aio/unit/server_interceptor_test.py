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
import logging
import unittest
from typing import Callable, Awaitable, Any

import grpc

from grpc.experimental import aio

from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase
from src.proto.grpc.testing import messages_pb2


class _LoggingInterceptor(aio.ServerInterceptor):

    def __init__(self, tag: str, record: list) -> None:
        self.tag = tag
        self.record = record

    async def intercept_service(
            self, continuation: Callable[[grpc.HandlerCallDetails], Awaitable[
                grpc.RpcMethodHandler]],
            handler_call_details: grpc.HandlerCallDetails
    ) -> grpc.RpcMethodHandler:
        self.record.append(self.tag + ':intercept_service')
        return await continuation(handler_call_details)


class _GenericInterceptor(aio.ServerInterceptor):

    def __init__(self, fn: Callable[[
            Callable[[grpc.HandlerCallDetails], Awaitable[grpc.
                                                          RpcMethodHandler]],
            grpc.HandlerCallDetails
    ], Any]) -> None:
        self._fn = fn

    async def intercept_service(
            self, continuation: Callable[[grpc.HandlerCallDetails], Awaitable[
                grpc.RpcMethodHandler]],
            handler_call_details: grpc.HandlerCallDetails
    ) -> grpc.RpcMethodHandler:
        return await self._fn(continuation, handler_call_details)


def _filter_server_interceptor(condition: Callable,
                               interceptor: aio.ServerInterceptor
                              ) -> aio.ServerInterceptor:

    async def intercept_service(
            continuation: Callable[[grpc.HandlerCallDetails], Awaitable[
                grpc.RpcMethodHandler]],
            handler_call_details: grpc.HandlerCallDetails
    ) -> grpc.RpcMethodHandler:
        if condition(handler_call_details):
            return await interceptor.intercept_service(continuation,
                                                       handler_call_details)
        return await continuation(handler_call_details)

    return _GenericInterceptor(intercept_service)


class TestServerInterceptor(AioTestBase):

    async def test_invalid_interceptor(self):

        class InvalidInterceptor:
            """Just an invalid Interceptor"""

        with self.assertRaises(ValueError):
            server_target, _ = await start_test_server(
                interceptors=(InvalidInterceptor(),))

    async def test_executed_right_order(self):
        record = []
        server_target, _ = await start_test_server(interceptors=(
            _LoggingInterceptor('log1', record),
            _LoggingInterceptor('log2', record),
        ))

        async with aio.insecure_channel(server_target) as channel:
            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())
            response = await call

            # Check that all interceptors were executed, and were executed
            # in the right order.
            self.assertSequenceEqual([
                'log1:intercept_service',
                'log2:intercept_service',
            ], record)
            self.assertIsInstance(response, messages_pb2.SimpleResponse)

    async def test_response_ok(self):
        record = []
        server_target, _ = await start_test_server(
            interceptors=(_LoggingInterceptor('log1', record),))

        async with aio.insecure_channel(server_target) as channel:
            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())
            response = await call
            code = await call.code()

            self.assertSequenceEqual(['log1:intercept_service'], record)
            self.assertIsInstance(response, messages_pb2.SimpleResponse)
            self.assertEqual(code, grpc.StatusCode.OK)

    async def test_apply_different_interceptors_by_metadata(self):
        record = []
        conditional_interceptor = _filter_server_interceptor(
            lambda x: ('secret', '42') in x.invocation_metadata,
            _LoggingInterceptor('log3', record))
        server_target, _ = await start_test_server(interceptors=(
            _LoggingInterceptor('log1', record),
            conditional_interceptor,
            _LoggingInterceptor('log2', record),
        ))

        async with aio.insecure_channel(server_target) as channel:
            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)

            metadata = (('key', 'value'),)
            call = multicallable(messages_pb2.SimpleRequest(),
                                 metadata=metadata)
            await call
            self.assertSequenceEqual([
                'log1:intercept_service',
                'log2:intercept_service',
            ], record)

            record.clear()
            metadata = (('key', 'value'), ('secret', '42'))
            call = multicallable(messages_pb2.SimpleRequest(),
                                 metadata=metadata)
            await call
            self.assertSequenceEqual([
                'log1:intercept_service',
                'log3:intercept_service',
                'log2:intercept_service',
            ], record)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
