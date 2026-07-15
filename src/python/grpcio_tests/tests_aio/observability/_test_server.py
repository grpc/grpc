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

import asyncio
from typing import Tuple

import grpc

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x00\x00\x00"

_UNARY_UNARY = "/test/UnaryUnary"
_UNARY_STREAM = "/test/UnaryStream"
_STREAM_UNARY = "/test/StreamUnary"
_STREAM_STREAM = "/test/StreamStream"
STREAM_LENGTH = 5


class _GenericHandler(grpc.GenericRpcHandler):
    def __init__(self):
        self._called = asyncio.get_event_loop().create_future()
        self._routing_table = {
            _UNARY_UNARY: grpc.unary_unary_rpc_method_handler(
                self._unary_unary
            ),
            _UNARY_STREAM: grpc.unary_stream_rpc_method_handler(
                self._unary_stream
            ),
            _STREAM_UNARY: grpc.stream_unary_rpc_method_handler(
                self._stream_unary
            ),
            _STREAM_STREAM: grpc.stream_stream_rpc_method_handler(
                self._stream_stream
            ),
        }

    async def _unary_unary(self, unused_request, unused_context):
        return _RESPONSE

    async def _unary_stream(self, unused_request, unused_context):
        for _ in range(STREAM_LENGTH):
            yield _RESPONSE

    async def _stream_unary(self, request_iterator, unused_context):
        async for _ in request_iterator:
            pass
        return _RESPONSE

    async def _stream_stream(self, request_iterator, servicer_context):
        async for _ in request_iterator:
            yield _RESPONSE

    def service(self, handler_details):
        if not self._called.done():
            self._called.set_result(None)
        return self._routing_table.get(handler_details.method)


async def start_server() -> Tuple[grpc.aio.Server, int]:
    server = grpc.aio.server()
    port = server.add_insecure_port("[::]:0")
    generic_handler = _GenericHandler()
    server.add_generic_rpc_handlers((generic_handler,))
    await server.start()
    return server, port


async def unary_unary_call(port):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.unary_unary(_UNARY_UNARY)
        unused_response = await multi_callable(_REQUEST)


async def unary_stream_call(port):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.unary_stream(_UNARY_STREAM)
        call = multi_callable(_REQUEST)
        async for _ in call:
            pass


async def stream_unary_call(port):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.stream_unary(_STREAM_UNARY)
        call = multi_callable(iter([_REQUEST] * STREAM_LENGTH))
        unused_response = await call


async def stream_stream_call(port):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.stream_stream(_STREAM_STREAM)
        call = multi_callable(iter([_REQUEST] * STREAM_LENGTH))
        async for _ in call:
            pass
