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

_SERVICE_NAME = "test"
_UNARY_UNARY = "UnaryUnary"
_UNARY_STREAM = "UnaryStream"
_STREAM_UNARY = "StreamUnary"
_STREAM_STREAM = "StreamStream"
STREAM_LENGTH = 5


async def unary_unary(unused_request, unused_context):
    return _RESPONSE


async def unary_stream(unused_request, unused_context):
    for _ in range(STREAM_LENGTH):
        yield _RESPONSE


async def stream_unary(request_iterator, unused_context):
    async for _ in request_iterator:
        pass
    return _RESPONSE


async def stream_stream(request_iterator, servicer_context):
    async for _ in request_iterator:
        yield _RESPONSE


class _MethodHandler(grpc.RpcMethodHandler):
    def __init__(self, request_streaming, response_streaming):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self.stream_stream = None
        if self.request_streaming and self.response_streaming:
            self.stream_stream = stream_stream
        elif self.request_streaming:
            self.stream_unary = stream_unary
        elif self.response_streaming:
            self.unary_stream = unary_stream
        else:
            self.unary_unary = unary_unary


RPC_METHOD_HANDLERS = {
    _UNARY_UNARY: _MethodHandler(False, False),
    _UNARY_STREAM: _MethodHandler(False, True),
    _STREAM_UNARY: _MethodHandler(True, False),
    _STREAM_STREAM: _MethodHandler(True, True),
}


REGISTERED_RPC_METHOD_HANDLERS = {
    _UNARY_UNARY: _MethodHandler(False, False),
    _UNARY_STREAM: _MethodHandler(False, True),
    _STREAM_UNARY: _MethodHandler(True, False),
    _STREAM_STREAM: _MethodHandler(True, True),
}


async def start_server(register_method=False) -> Tuple[grpc.aio.Server, int]:
    server = grpc.aio.server()
    port = server.add_insecure_port("[::]:0")
    generic_handler = grpc.method_handlers_generic_handler(
        _SERVICE_NAME, RPC_METHOD_HANDLERS
    )
    server.add_generic_rpc_handlers((generic_handler,))
    if register_method:
        server.add_registered_method_handlers(
            _SERVICE_NAME, REGISTERED_RPC_METHOD_HANDLERS
        )
    await server.start()
    return server, port


async def unary_unary_call(port, registered_method=False):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.unary_unary(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_UNARY),
            _registered_method=registered_method,
        )
        unused_response = await multi_callable(_REQUEST)


async def unary_stream_call(port, registered_method=False):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.unary_stream(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_STREAM),
            _registered_method=registered_method,
        )
        call = multi_callable(_REQUEST)
        async for _ in call:
            pass


async def stream_unary_call(port, registered_method=False):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.stream_unary(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_UNARY),
            _registered_method=registered_method,
        )
        call = multi_callable(iter([_REQUEST] * STREAM_LENGTH))
        unused_response = await call


async def stream_stream_call(port, registered_method=False):
    async with grpc.aio.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.stream_stream(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_STREAM),
            _registered_method=registered_method,
        )
        call = multi_callable(iter([_REQUEST] * STREAM_LENGTH))
        async for _ in call:
            pass
