# Copyright 2023 gRPC authors.
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

from concurrent import futures
from typing import Tuple

import grpc

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x00\x00\x00"

_SERVICE_NAME = "test"
_UNARY_UNARY = "UnaryUnary"
_UNARY_UNARY_FILTERED = "UnaryUnaryFiltered"
_UNARY_STREAM = "UnaryStream"
_STREAM_UNARY = "StreamUnary"
_STREAM_STREAM = "StreamStream"
STREAM_LENGTH = 5
TRIGGER_RPC_METADATA = ("control", "trigger_rpc")
TRIGGER_RPC_TO_NEW_SERVER_METADATA = ("to_new_server", "")


def handle_unary_unary(request, servicer_context):
    if TRIGGER_RPC_METADATA in servicer_context.invocation_metadata():
        for k, v in servicer_context.invocation_metadata():
            if "port" in k:
                unary_unary_call(port=int(v))
            if "to_new_server" in k:
                second_server = grpc.server(
                    futures.ThreadPoolExecutor(max_workers=10)
                )
                second_server.add_generic_rpc_handlers((_GenericHandler(),))
                second_server_port = second_server.add_insecure_port("[::]:0")
                second_server.start()
                unary_unary_call(port=second_server_port)
                second_server.stop(0)
    return _RESPONSE


def handle_unary_stream(request, servicer_context):
    for _ in range(STREAM_LENGTH):
        yield _RESPONSE


def handle_stream_unary(request_iterator, servicer_context):
    return _RESPONSE


def handle_stream_stream(request_iterator, servicer_context):
    for request in request_iterator:
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
            self.stream_stream = handle_stream_stream
        elif self.request_streaming:
            self.stream_unary = handle_stream_unary
        elif self.response_streaming:
            self.unary_stream = handle_unary_stream
        else:
            self.unary_unary = handle_unary_unary


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(False, False)
        if handler_call_details.method == _UNARY_UNARY_FILTERED:
            return _MethodHandler(False, False)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(False, True)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(True, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True)
        else:
            return None


RPC_METHOD_HANDLERS = {
    _UNARY_UNARY_FILTERED: _MethodHandler(False, False),
    _UNARY_UNARY: _MethodHandler(False, False),
    _UNARY_STREAM: _MethodHandler(False, True),
    _STREAM_UNARY: _MethodHandler(True, False),
    _STREAM_STREAM: _MethodHandler(True, True),
}

REGISTERED_RPC_METHOD_HANDLERS = {
    _UNARY_UNARY_FILTERED: _MethodHandler(False, False),
    _UNARY_UNARY: _MethodHandler(False, False),
    _UNARY_STREAM: _MethodHandler(False, True),
    _STREAM_UNARY: _MethodHandler(True, False),
    _STREAM_STREAM: _MethodHandler(True, True),
}


def start_server(
    interceptors=None, register_method=True
) -> Tuple[grpc.Server, int]:
    if interceptors:
        server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=10),
            interceptors=interceptors,
        )
    else:
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    generic_handler = grpc.method_handlers_generic_handler(
        _SERVICE_NAME, RPC_METHOD_HANDLERS
    )
    server.add_generic_rpc_handlers((generic_handler,))
    if register_method:
        server.add_registered_method_handlers(
            _SERVICE_NAME, REGISTERED_RPC_METHOD_HANDLERS
        )
    port = server.add_insecure_port("[::]:0")
    server.start()
    return server, port


def unary_unary_call(port, metadata=None, registered_method=False):
    with grpc.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.unary_unary(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_UNARY),
            _registered_method=registered_method,
        )
        if metadata:
            unused_response, call = multi_callable.with_call(
                _REQUEST, metadata=metadata
            )
        else:
            unused_response, call = multi_callable.with_call(_REQUEST)


def intercepted_unary_unary_call(port, interceptors, metadata=None):
    with grpc.insecure_channel(f"localhost:{port}") as channel:
        intercept_channel = grpc.intercept_channel(channel, interceptors)
        multi_callable = intercept_channel.unary_unary(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_UNARY)
        )
        if metadata:
            unused_response, call = multi_callable.with_call(
                _REQUEST, metadata=metadata
            )
        else:
            unused_response, call = multi_callable.with_call(_REQUEST)


def unary_unary_filtered_call(port, metadata=None):
    with grpc.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.unary_unary(
            grpc._common.fully_qualified_method(
                _SERVICE_NAME, _UNARY_UNARY_FILTERED
            )
        )
        if metadata:
            unused_response, call = multi_callable.with_call(
                _REQUEST, metadata=metadata
            )
        else:
            unused_response, call = multi_callable.with_call(_REQUEST)


def unary_stream_call(port):
    with grpc.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.unary_stream(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_STREAM)
        )
        call = multi_callable(_REQUEST)
        for _ in call:
            pass


def stream_unary_call(port):
    with grpc.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.stream_unary(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_UNARY)
        )
        unused_response, call = multi_callable.with_call(
            iter([_REQUEST] * STREAM_LENGTH)
        )


def stream_stream_call(port):
    with grpc.insecure_channel(f"localhost:{port}") as channel:
        multi_callable = channel.stream_stream(
            grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_STREAM)
        )
        call = multi_callable(iter([_REQUEST] * STREAM_LENGTH))
        for _ in call:
            pass
