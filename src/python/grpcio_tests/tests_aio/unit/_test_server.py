# Copyright 2019 The gRPC Authors
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

import grpc

from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests_aio.unit._constants import UNARY_CALL_WITH_SLEEP_VALUE

_INITIAL_METADATA_KEY = "initial-md-key"
_TRAILING_METADATA_KEY = "trailing-md-key-bin"


async def _maybe_echo_metadata(servicer_context):
    """Copies metadata from request to response if it is present."""
    invocation_metadata = dict(servicer_context.invocation_metadata())
    if _INITIAL_METADATA_KEY in invocation_metadata:
        initial_metadatum = (_INITIAL_METADATA_KEY,
                             invocation_metadata[_INITIAL_METADATA_KEY])
        await servicer_context.send_initial_metadata((initial_metadatum,))
    if _TRAILING_METADATA_KEY in invocation_metadata:
        trailing_metadatum = (_TRAILING_METADATA_KEY,
                              invocation_metadata[_TRAILING_METADATA_KEY])
        servicer_context.set_trailing_metadata((trailing_metadatum,))


class _TestServiceServicer(test_pb2_grpc.TestServiceServicer):

    async def UnaryCall(self, unused_request, context):
        await _maybe_echo_metadata(context)
        return messages_pb2.SimpleResponse()

    async def StreamingOutputCall(
            self, request: messages_pb2.StreamingOutputCallRequest,
            unused_context):
        for response_parameters in request.response_parameters:
            if response_parameters.interval_us != 0:
                await asyncio.sleep(
                    datetime.timedelta(microseconds=response_parameters.
                                       interval_us).total_seconds())
            yield messages_pb2.StreamingOutputCallResponse(
                payload=messages_pb2.Payload(type=request.response_type,
                                             body=b'\x00' *
                                             response_parameters.size))

    # Next methods are extra ones that are registred programatically
    # when the sever is instantiated. They are not being provided by
    # the proto file.
    async def UnaryCallWithSleep(self, request, context):
        await asyncio.sleep(UNARY_CALL_WITH_SLEEP_VALUE)
        return messages_pb2.SimpleResponse()

    async def StreamingInputCall(self, request_async_iterator, unused_context):
        aggregate_size = 0
        async for request in request_async_iterator:
            if request.payload is not None and request.payload.body:
                aggregate_size += len(request.payload.body)
        return messages_pb2.StreamingInputCallResponse(
            aggregated_payload_size=aggregate_size)

    async def FullDuplexCall(self, request_async_iterator, unused_context):
        async for request in request_async_iterator:
            for response_parameters in request.response_parameters:
                if response_parameters.interval_us != 0:
                    await asyncio.sleep(
                        datetime.timedelta(microseconds=response_parameters.
                                           interval_us).total_seconds())
                yield messages_pb2.StreamingOutputCallResponse(
                    payload=messages_pb2.Payload(type=request.payload.type,
                                                 body=b'\x00' *
                                                 response_parameters.size))


async def start_test_server(secure=False):
    server = aio.server(options=(('grpc.so_reuseport', 0),))
    servicer = _TestServiceServicer()
    test_pb2_grpc.add_TestServiceServicer_to_server(servicer, server)

    # Add programatically extra methods not provided by the proto file
    # that are used during the tests
    rpc_method_handlers = {
        'UnaryCallWithSleep':
            grpc.unary_unary_rpc_method_handler(
                servicer.UnaryCallWithSleep,
                request_deserializer=messages_pb2.SimpleRequest.FromString,
                response_serializer=messages_pb2.SimpleResponse.
                SerializeToString)
    }
    extra_handler = grpc.method_handlers_generic_handler(
        'grpc.testing.TestService', rpc_method_handlers)
    server.add_generic_rpc_handlers((extra_handler,))

    if secure:
        server_credentials = grpc.local_server_credentials(
            grpc.LocalConnectionType.LOCAL_TCP)
        port = server.add_secure_port('[::]:0', server_credentials)
    else:
        port = server.add_insecure_port('[::]:0')

    await server.start()
    # NOTE(lidizheng) returning the server to prevent it from deallocation
    return 'localhost:%d' % port, server
