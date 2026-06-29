# Copyright 2026 The gRPC Authors
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
"""Server-streaming cancellation must not leak ContextVar.Token across Contexts.

When ``_finish_handler_with_stream_responses`` consumes a user-supplied async
generator and the RPC is cancelled while the server is suspended in
``await servicer_context.write(...)``, the iteration unwinds without calling
``aclose()`` on the generator. CPython's default async-generator finalizer
hook then schedules ``aclose()`` on a fresh task whose ``contextvars.Context``
is a copy of the loop runner's, so any wrapper ``finally`` that calls
``ContextVar.reset(token)`` raises::

    ValueError: <Token ...> was created in a different Context

This is a regression test for that path.
"""

import asyncio
from contextlib import asynccontextmanager
from contextvars import ContextVar
import gc
import logging
from typing import Awaitable, Callable
import unittest

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase

_SERVICE = "test.Service"
_METHOD = "Stream"
_FULL_METHOD = "/{}/{}".format(_SERVICE, _METHOD)

# Many large messages let HTTP/2 flow control fill up so the server suspends
# in ``write(...)``; cancellation then lands inside the ``async for`` body
# rather than inside ``__anext__`` on the user generator. The
# ``max_send_message_length`` option below just lifts the per-message size
# cap so 64 KiB chunks are allowed; flow control is governed by HTTP/2 window
# sizes, not that option.
_CHUNK = b"x" * 64 * 1024
_NUM_RESPONSES = 1024

_TEST_VAR: ContextVar[int] = ContextVar("server_streaming_token_reset_test_var")


def _identity(payload: bytes) -> bytes:
    return payload


async def _stream_handler(unused_request, unused_context):
    for _ in range(_NUM_RESPONSES):
        yield _CHUNK


def _generic_handler() -> grpc.GenericRpcHandler:
    rpc_handler = grpc.unary_stream_rpc_method_handler(
        _stream_handler,
        request_deserializer=_identity,
        response_serializer=_identity,
    )
    return grpc.method_handlers_generic_handler(
        _SERVICE, {_METHOD: rpc_handler}
    )


class _TokenResetInterceptor(aio.ServerInterceptor):
    """Sets a ContextVar on entry and resets it on exit via Token.reset."""

    async def intercept_service(
        self,
        continuation: Callable[
            [grpc.HandlerCallDetails], Awaitable[grpc.RpcMethodHandler]
        ],
        handler_call_details: grpc.HandlerCallDetails,
    ) -> grpc.RpcMethodHandler:
        next_handler = await continuation(handler_call_details)

        @asynccontextmanager
        async def scope():
            token = _TEST_VAR.set(1)
            try:
                yield
            finally:
                _TEST_VAR.reset(token)

        async def wrapped(request, context):
            async with scope():
                async for item in next_handler.unary_stream(request, context):
                    yield item

        return grpc.unary_stream_rpc_method_handler(
            wrapped,
            request_deserializer=next_handler.request_deserializer,
            response_serializer=next_handler.response_serializer,
        )


class TestStreamingTokenReset(AioTestBase):
    async def test_cancel_does_not_leak_cross_context_token_reset(self):
        loop = asyncio.get_event_loop()
        captured = []
        previous_handler = loop.get_exception_handler()
        loop.set_exception_handler(lambda _loop, ctx: captured.append(ctx))
        try:
            server = aio.server(
                interceptors=(_TokenResetInterceptor(),),
                options=(
                    ("grpc.max_send_message_length", 1024 * 1024 * 1024),
                ),
            )
            server.add_generic_rpc_handlers((_generic_handler(),))
            port = server.add_insecure_port("[::]:0")
            await server.start()
            try:
                async with aio.insecure_channel(
                    "localhost:%d" % port
                ) as channel:
                    multicallable = channel.unary_stream(
                        _FULL_METHOD,
                        request_serializer=_identity,
                        response_deserializer=_identity,
                    )
                    call = multicallable(b"req")
                    async for item in call:
                        self.assertEqual(item, _CHUNK)
                        call.cancel()
                        break
                # Let the asyncgen finalizer hook run aclose() on any
                # leaked generator.
                await asyncio.sleep(0.2)
            finally:
                await server.stop(grace=0)

            for _ in range(3):
                gc.collect()
                await asyncio.sleep(0.1)
        finally:
            loop.set_exception_handler(previous_handler)

        cross_context = [
            ctx
            for ctx in captured
            if isinstance(ctx.get("exception"), ValueError)
            and "different Context" in str(ctx["exception"])
        ]
        self.assertFalse(
            cross_context,
            "Server-streaming cancel leaked a cross-Context Token.reset "
            "ValueError: %r" % cross_context,
        )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
