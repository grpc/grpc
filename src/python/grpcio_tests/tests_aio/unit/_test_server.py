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
from contextvars import ContextVar
import datetime
from typing import Optional

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit import resources
from tests_aio.unit import _constants

_INITIAL_METADATA_KEY = "x-grpc-test-echo-initial"
_TRAILING_METADATA_KEY = "x-grpc-test-echo-trailing-bin"

TEST_CONTEXT_VAR: ContextVar[str] = ContextVar("")


async def _maybe_echo_metadata(servicer_context):
    """Copies metadata from request to response if it is present."""
    invocation_metadata = dict(servicer_context.invocation_metadata())
    if _INITIAL_METADATA_KEY in invocation_metadata:
        initial_metadatum = (
            _INITIAL_METADATA_KEY,
            invocation_metadata[_INITIAL_METADATA_KEY],
        )
        await servicer_context.send_initial_metadata((initial_metadatum,))
    if _TRAILING_METADATA_KEY in invocation_metadata:
        trailing_metadatum = (
            _TRAILING_METADATA_KEY,
            invocation_metadata[_TRAILING_METADATA_KEY],
        )
        servicer_context.set_trailing_metadata((trailing_metadatum,))


async def _maybe_echo_status(
    request: messages_pb2.SimpleRequest, servicer_context
):
    """Echos the RPC status if demanded by the request."""
    if request.HasField("response_status"):
        await servicer_context.abort(
            request.response_status.code, request.response_status.message
        )


class TestServiceServicer(test_pb2_grpc.TestServiceServicer):
    def __init__(self, record: Optional[list] = None):
        self.record = record if record is not None else []

    def _append_to_log(self):
        self.record.append("servicer:" + TEST_CONTEXT_VAR.get("service"))

    async def UnaryCall(self, request, context):
        self._append_to_log()
        await _maybe_echo_metadata(context)
        await _maybe_echo_status(request, context)
        return messages_pb2.SimpleResponse(
            payload=messages_pb2.Payload(
                type=messages_pb2.COMPRESSABLE,
                body=b"\x00" * request.response_size,
            )
        )

    async def EmptyCall(self, request, context):
        self._append_to_log()
        return empty_pb2.Empty()

    async def StreamingOutputCall(
        self, request: messages_pb2.StreamingOutputCallRequest, unused_context
    ):
        self._append_to_log()
        for response_parameters in request.response_parameters:
            if response_parameters.interval_us != 0:
                await asyncio.sleep(
                    datetime.timedelta(
                        microseconds=response_parameters.interval_us
                    ).total_seconds()
                )
            if response_parameters.size != 0:
                yield messages_pb2.StreamingOutputCallResponse(
                    payload=messages_pb2.Payload(
                        type=request.response_type,
                        body=b"\x00" * response_parameters.size,
                    )
                )
            else:
                yield messages_pb2.StreamingOutputCallResponse()

    # Next methods are extra ones that are registered programmatically
    # when the sever is instantiated. They are not being provided by
    # the proto file.
    async def UnaryCallWithSleep(self, unused_request, unused_context):
        self._append_to_log()
        await asyncio.sleep(_constants.UNARY_CALL_WITH_SLEEP_VALUE)
        return messages_pb2.SimpleResponse()

    async def StreamingInputCall(self, request_async_iterator, unused_context):
        self._append_to_log()
        aggregate_size = 0
        async for request in request_async_iterator:
            if request.payload is not None and request.payload.body:
                aggregate_size += len(request.payload.body)
        return messages_pb2.StreamingInputCallResponse(
            aggregated_payload_size=aggregate_size
        )

    async def FullDuplexCall(self, request_async_iterator, context):
        self._append_to_log()
        await _maybe_echo_metadata(context)
        async for request in request_async_iterator:
            await _maybe_echo_status(request, context)
            for response_parameters in request.response_parameters:
                if response_parameters.interval_us != 0:
                    await asyncio.sleep(
                        datetime.timedelta(
                            microseconds=response_parameters.interval_us
                        ).total_seconds()
                    )
                if response_parameters.size != 0:
                    yield messages_pb2.StreamingOutputCallResponse(
                        payload=messages_pb2.Payload(
                            type=request.payload.type,
                            body=b"\x00" * response_parameters.size,
                        )
                    )
                else:
                    yield messages_pb2.StreamingOutputCallResponse()


def _create_extra_generic_handler(servicer: TestServiceServicer):
    # Add programmatically extra methods not provided by the proto file
    # that are used during the tests
    rpc_method_handlers = {
        "UnaryCallWithSleep": grpc.unary_unary_rpc_method_handler(
            servicer.UnaryCallWithSleep,
            request_deserializer=messages_pb2.SimpleRequest.FromString,
            response_serializer=messages_pb2.SimpleResponse.SerializeToString,
        )
    }
    return grpc.method_handlers_generic_handler(
        "grpc.testing.TestService", rpc_method_handlers
    )


async def start_test_server(
    port=0,
    secure=False,
    server_credentials=None,
    interceptors=None,
    record: Optional[list] = None,
):
    if not hasattr(grpc, "_uds_port_map"):
        grpc._uds_port_map = {}
        original_aio_insecure_channel = getattr(grpc.aio, "insecure_channel", None)
        original_aio_secure_channel = getattr(grpc.aio, "secure_channel", None)
        if original_aio_insecure_channel:
            def custom_aio_insecure_channel(target, options=None, compression=None, interceptors=None):
                try:
                    port = int(target.split(":")[-1])
                    if port in grpc._uds_port_map:
                        target = grpc._uds_port_map[port]
                except Exception:
                    pass
                return original_aio_insecure_channel(target, options, compression, interceptors)
            grpc.aio.insecure_channel = custom_aio_insecure_channel
            if getattr(grpc, "experimental", None) and getattr(grpc.experimental, "aio", None):
                grpc.experimental.aio.insecure_channel = custom_aio_insecure_channel
        if original_aio_secure_channel:
            def custom_aio_secure_channel(target, credentials, options=None, compression=None, interceptors=None):
                try:
                    port = int(target.split(":")[-1])
                    if port in grpc._uds_port_map:
                        target = grpc._uds_port_map[port]
                except Exception:
                    pass
                return original_aio_secure_channel(target, credentials, options, compression, interceptors)
            grpc.aio.secure_channel = custom_aio_secure_channel
            if getattr(grpc, "experimental", None) and getattr(grpc.experimental, "aio", None):
                grpc.experimental.aio.secure_channel = custom_aio_secure_channel

    server = aio.server(
        options=(("grpc.so_reuseport", 0),), interceptors=interceptors
    )
    servicer = TestServiceServicer(record)
    test_pb2_grpc.add_TestServiceServicer_to_server(servicer, server)

    server.add_generic_rpc_handlers((_create_extra_generic_handler(servicer),))

    if secure:
        if server_credentials is None:
            server_credentials = grpc.ssl_server_credentials(
                [(resources.private_key(), resources.certificate_chain())]
            )
        if port != 0:
            server.add_secure_port(f"127.0.0.1:{port}", server_credentials)
        else:
            import tempfile, os, uuid
            sock_name = f"grpc_test_{uuid.uuid4().hex}.sock"
            uds_path = os.path.join(tempfile.gettempdir(), sock_name)
            fake_port = hash(sock_name) % 10000 + 50000
            while fake_port in grpc._uds_port_map:
                fake_port += 1
            grpc._uds_port_map[fake_port] = f"unix:{uds_path}"
            server.add_secure_port(f"unix:{uds_path}", server_credentials)
            port = fake_port
    else:
        if port != 0:
            server.add_insecure_port(f"127.0.0.1:{port}")
        else:
            import tempfile, os, uuid
            sock_name = f"grpc_test_{uuid.uuid4().hex}.sock"
            uds_path = os.path.join(tempfile.gettempdir(), sock_name)
            fake_port = hash(sock_name) % 10000 + 40000
            while fake_port in grpc._uds_port_map:
                fake_port += 1
            grpc._uds_port_map[fake_port] = f"unix:{uds_path}"
            server.add_insecure_port(f"unix:{uds_path}")
            port = fake_port

    await server.start()

    # NOTE(lidizheng) returning the server to prevent it from deallocation
    return "127.0.0.1:%d" % port, server
