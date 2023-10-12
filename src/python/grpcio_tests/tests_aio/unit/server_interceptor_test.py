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
"""Test the functionality of server interceptors."""

import asyncio
import functools
import logging
from typing import Any, Awaitable, Callable, Tuple
import unittest

import grpc
from grpc.experimental import aio
from grpc.experimental import wrap_server_method_handler

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 42


class _LoggingInterceptor(aio.ServerInterceptor):
    def __init__(self, tag: str, record: list) -> None:
        self.tag = tag
        self.record = record

    async def intercept_service(
        self,
        continuation: Callable[
            [grpc.HandlerCallDetails], Awaitable[grpc.RpcMethodHandler]
        ],
        handler_call_details: grpc.HandlerCallDetails,
    ) -> grpc.RpcMethodHandler:
        self.record.append(self.tag + ":intercept_service")
        return await continuation(handler_call_details)


class _GenericInterceptor(aio.ServerInterceptor):
    def __init__(
        self,
        fn: Callable[
            [
                Callable[
                    [grpc.HandlerCallDetails], Awaitable[grpc.RpcMethodHandler]
                ],
                grpc.HandlerCallDetails,
            ],
            Any,
        ],
    ) -> None:
        self._fn = fn

    async def intercept_service(
        self,
        continuation: Callable[
            [grpc.HandlerCallDetails], Awaitable[grpc.RpcMethodHandler]
        ],
        handler_call_details: grpc.HandlerCallDetails,
    ) -> grpc.RpcMethodHandler:
        return await self._fn(continuation, handler_call_details)


def _filter_server_interceptor(
    condition: Callable, interceptor: aio.ServerInterceptor
) -> aio.ServerInterceptor:
    async def intercept_service(
        continuation: Callable[
            [grpc.HandlerCallDetails], Awaitable[grpc.RpcMethodHandler]
        ],
        handler_call_details: grpc.HandlerCallDetails,
    ) -> grpc.RpcMethodHandler:
        if condition(handler_call_details):
            return await interceptor.intercept_service(
                continuation, handler_call_details
            )
        return await continuation(handler_call_details)

    return _GenericInterceptor(intercept_service)


class _CacheInterceptor(aio.ServerInterceptor):
    """An interceptor that caches response based on request message."""

    def __init__(self, cache_store=None):
        self.cache_store = cache_store or {}

    async def intercept_service(
        self,
        continuation: Callable[
            [grpc.HandlerCallDetails], Awaitable[grpc.RpcMethodHandler]
        ],
        handler_call_details: grpc.HandlerCallDetails,
    ) -> grpc.RpcMethodHandler:
        # Get the actual handler
        handler = await continuation(handler_call_details)

        # Only intercept unary call RPCs
        if handler and (
            handler.request_streaming
            or handler.response_streaming  # pytype: disable=attribute-error
        ):  # pytype: disable=attribute-error
            return handler

        def wrapper(
            behavior: Callable[
                [messages_pb2.SimpleRequest, aio.ServicerContext],
                messages_pb2.SimpleResponse,
            ]
        ):
            @functools.wraps(behavior)
            async def wrapper(
                request: messages_pb2.SimpleRequest,
                context: aio.ServicerContext,
            ) -> messages_pb2.SimpleResponse:
                if request.response_size not in self.cache_store:
                    self.cache_store[request.response_size] = await behavior(
                        request, context
                    )
                return self.cache_store[request.response_size]

            return wrapper

        return wrap_server_method_handler(wrapper, handler)


async def _create_server_stub_pair(
    *interceptors: aio.ServerInterceptor,
) -> Tuple[aio.Server, test_pb2_grpc.TestServiceStub]:
    """Creates a server-stub pair with given interceptors.

    Returning the server object to protect it from being garbage collected.
    """
    server_target, server = await start_test_server(interceptors=interceptors)
    channel = aio.insecure_channel(server_target)
    return server, test_pb2_grpc.TestServiceStub(channel)


class TestServerInterceptor(AioTestBase):
    async def test_invalid_interceptor(self):
        class InvalidInterceptor:
            """Just an invalid Interceptor"""

        with self.assertRaises(ValueError):
            server_target, _ = await start_test_server(
                interceptors=(InvalidInterceptor(),)
            )

    async def test_executed_right_order(self):
        record = []
        server_target, _ = await start_test_server(
            interceptors=(
                _LoggingInterceptor("log1", record),
                _LoggingInterceptor("log2", record),
            )
        )

        async with aio.insecure_channel(server_target) as channel:
            multicallable = channel.unary_unary(
                "/grpc.testing.TestService/UnaryCall",
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString,
            )
            call = multicallable(messages_pb2.SimpleRequest())
            response = await call

            # Check that all interceptors were executed, and were executed
            # in the right order.
            self.assertSequenceEqual(
                [
                    "log1:intercept_service",
                    "log2:intercept_service",
                ],
                record,
            )
            self.assertIsInstance(response, messages_pb2.SimpleResponse)

    async def test_response_ok(self):
        record = []
        server_target, _ = await start_test_server(
            interceptors=(_LoggingInterceptor("log1", record),)
        )

        async with aio.insecure_channel(server_target) as channel:
            multicallable = channel.unary_unary(
                "/grpc.testing.TestService/UnaryCall",
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString,
            )
            call = multicallable(messages_pb2.SimpleRequest())
            response = await call
            code = await call.code()

            self.assertSequenceEqual(["log1:intercept_service"], record)
            self.assertIsInstance(response, messages_pb2.SimpleResponse)
            self.assertEqual(code, grpc.StatusCode.OK)

    async def test_apply_different_interceptors_by_metadata(self):
        record = []
        conditional_interceptor = _filter_server_interceptor(
            lambda x: ("secret", "42") in x.invocation_metadata,
            _LoggingInterceptor("log3", record),
        )
        server_target, _ = await start_test_server(
            interceptors=(
                _LoggingInterceptor("log1", record),
                conditional_interceptor,
                _LoggingInterceptor("log2", record),
            )
        )

        async with aio.insecure_channel(server_target) as channel:
            multicallable = channel.unary_unary(
                "/grpc.testing.TestService/UnaryCall",
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString,
            )

            metadata = aio.Metadata(
                ("key", "value"),
            )
            call = multicallable(
                messages_pb2.SimpleRequest(), metadata=metadata
            )
            await call
            self.assertSequenceEqual(
                [
                    "log1:intercept_service",
                    "log2:intercept_service",
                ],
                record,
            )

            record.clear()
            metadata = aio.Metadata(("key", "value"), ("secret", "42"))
            call = multicallable(
                messages_pb2.SimpleRequest(), metadata=metadata
            )
            await call
            self.assertSequenceEqual(
                [
                    "log1:intercept_service",
                    "log3:intercept_service",
                    "log2:intercept_service",
                ],
                record,
            )

    async def test_response_caching(self):
        # Prepares a preset value to help testing
        interceptor = _CacheInterceptor(
            {
                42: messages_pb2.SimpleResponse(
                    payload=messages_pb2.Payload(body=b"\x42")
                )
            }
        )

        # Constructs a server with the cache interceptor
        server, stub = await _create_server_stub_pair(interceptor)

        # Tests if the cache store is used
        response = await stub.UnaryCall(
            messages_pb2.SimpleRequest(response_size=42)
        )
        self.assertEqual(1, len(interceptor.cache_store[42].payload.body))
        self.assertEqual(interceptor.cache_store[42], response)

        # Tests response can be cached
        response = await stub.UnaryCall(
            messages_pb2.SimpleRequest(response_size=1337)
        )
        self.assertEqual(1337, len(interceptor.cache_store[1337].payload.body))
        self.assertEqual(interceptor.cache_store[1337], response)
        response = await stub.UnaryCall(
            messages_pb2.SimpleRequest(response_size=1337)
        )
        self.assertEqual(interceptor.cache_store[1337], response)

    async def test_interceptor_unary_stream(self):
        record = []
        server, stub = await _create_server_stub_pair(
            _LoggingInterceptor("log_unary_stream", record)
        )

        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                )
            )

        # Tests if the cache store is used
        call = stub.StreamingOutputCall(request)

        # Ensures the RPC goes fine
        async for response in call:
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        self.assertSequenceEqual(
            [
                "log_unary_stream:intercept_service",
            ],
            record,
        )

    async def test_interceptor_stream_unary(self):
        record = []
        server, stub = await _create_server_stub_pair(
            _LoggingInterceptor("log_stream_unary", record)
        )

        # Invokes the actual RPC
        call = stub.StreamingInputCall()

        # Prepares the request
        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        # Sends out requests
        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(request)
        await call.done_writing()

        # Validates the responses
        response = await call
        self.assertIsInstance(response, messages_pb2.StreamingInputCallResponse)
        self.assertEqual(
            _NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
            response.aggregated_payload_size,
        )

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        self.assertSequenceEqual(
            [
                "log_stream_unary:intercept_service",
            ],
            record,
        )

    async def test_interceptor_stream_stream(self):
        record = []
        server, stub = await _create_server_stub_pair(
            _LoggingInterceptor("log_stream_stream", record)
        )

        # Prepares the request
        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        async def gen():
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request

        # Invokes the actual RPC
        call = stub.StreamingInputCall(gen())

        # Validates the responses
        response = await call
        self.assertIsInstance(response, messages_pb2.StreamingInputCallResponse)
        self.assertEqual(
            _NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
            response.aggregated_payload_size,
        )

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        self.assertSequenceEqual(
            [
                "log_stream_stream:intercept_service",
            ],
            record,
        )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
