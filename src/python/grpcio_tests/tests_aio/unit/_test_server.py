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

import grpc
from grpc.experimental import aio
from tests.unit import resources

from src.proto.grpc.testing import empty_pb2, messages_pb2, test_pb2_grpc
from tests_aio.unit import _constants

_INITIAL_METADATA_KEY = "x-grpc-test-echo-initial"
_TRAILING_METADATA_KEY = "x-grpc-test-echo-trailing-bin"


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


async def _maybe_echo_status(request: messages_pb2.SimpleRequest,
                             servicer_context):
    """Echos the RPC status if demanded by the request."""
    if request.HasField('response_status'):
        await servicer_context.abort(request.response_status.code,
                                     request.response_status.message)


class TestServiceServicer(test_pb2_grpc.TestServiceServicer):

    async def UnaryCall(self, request, context):
        await _maybe_echo_metadata(context)
        await _maybe_echo_status(request, context)
        return messages_pb2.SimpleResponse(
            payload=messages_pb2.Payload(type=messages_pb2.COMPRESSABLE,
                                         body=b'\x00' * request.response_size))

    async def EmptyCall(self, request, context):
        return empty_pb2.Empty()

    async def StreamingOutputCall(
            self, request: messages_pb2.StreamingOutputCallRequest,
            unused_context):
        for response_parameters in request.response_parameters:
            if response_parameters.interval_us != 0:
                await asyncio.sleep(
                    datetime.timedelta(microseconds=response_parameters.
                                       interval_us).total_seconds())
            if response_parameters.size != 0:
                yield messages_pb2.StreamingOutputCallResponse(
                    payload=messages_pb2.Payload(type=request.response_type,
                                                 body=b'\x00' *
                                                 response_parameters.size))
            else:
                yield messages_pb2.StreamingOutputCallResponse()

    # Next methods are extra ones that are registred programatically
    # when the sever is instantiated. They are not being provided by
    # the proto file.
    async def UnaryCallWithSleep(self, unused_request, unused_context):
        await asyncio.sleep(_constants.UNARY_CALL_WITH_SLEEP_VALUE)
        return messages_pb2.SimpleResponse()

    async def StreamingInputCall(self, request_async_iterator, unused_context):
        aggregate_size = 0
        async for request in request_async_iterator:
            if request.payload is not None and request.payload.body:
                aggregate_size += len(request.payload.body)
        return messages_pb2.StreamingInputCallResponse(
            aggregated_payload_size=aggregate_size)

    async def FullDuplexCall(self, request_async_iterator, context):
        await _maybe_echo_metadata(context)
        async for request in request_async_iterator:
            await _maybe_echo_status(request, context)
            for response_parameters in request.response_parameters:
                if response_parameters.interval_us != 0:
                    await asyncio.sleep(
                        datetime.timedelta(microseconds=response_parameters.
                                           interval_us).total_seconds())
                if response_parameters.size != 0:
                    yield messages_pb2.StreamingOutputCallResponse(
                        payload=messages_pb2.Payload(type=request.payload.type,
                                                     body=b'\x00' *
                                                     response_parameters.size))
                else:
                    yield messages_pb2.StreamingOutputCallResponse()


def _create_extra_generic_handler(servicer: TestServiceServicer):
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
    return grpc.method_handlers_generic_handler('grpc.testing.TestService',
                                                rpc_method_handlers)


async def start_test_server(port=0,
                            secure=False,
                            server_credentials=None,
                            interceptors=None):
    server = aio.server(options=(('grpc.so_reuseport', 0),),
                        interceptors=interceptors)
    servicer = TestServiceServicer()
    test_pb2_grpc.add_TestServiceServicer_to_server(servicer, server)

    server.add_generic_rpc_handlers((_create_extra_generic_handler(servicer),))

    if secure:
        if server_credentials is None:
            server_credentials = grpc.ssl_server_credentials([
                (resources.private_key(), resources.certificate_chain())
            ])
        port = server.add_secure_port('[::]:%d' % port, server_credentials)
    else:
        port = server.add_insecure_port('[::]:%d' % port)

    await server.start()

    # NOTE(lidizheng) returning the server to prevent it from deallocation
    return 'localhost:%d' % port, server
