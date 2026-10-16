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
"""Implementations of interoperability test methods."""

import argparse
import asyncio
import collections
import datetime
import enum
import inspect
import json
import os
import threading
import time
from typing import Any, Optional, Union

from google import auth as google_auth
from google.auth import environment_vars as google_auth_environment_vars
from google.auth.transport import grpc as google_auth_transport_grpc
from google.auth.transport import requests as google_auth_transport_requests
import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

_INITIAL_METADATA_KEY = "x-grpc-test-echo-initial"
_TRAILING_METADATA_KEY = "x-grpc-test-echo-trailing-bin"


async def _expect_status_code(
    call: aio.Call, expected_code: grpc.StatusCode
) -> None:
    code = await call.code()
    if code != expected_code:
        raise ValueError(
            "expected code %s, got %s" % (expected_code, await call.code())
        )


async def _expect_status_details(call: aio.Call, expected_details: str) -> None:
    details = await call.details()
    if details != expected_details:
        raise ValueError(
            "expected message %s, got %s"
            % (expected_details, await call.details())
        )


async def _validate_status_code_and_details(
    call: aio.Call, expected_code: grpc.StatusCode, expected_details: str
) -> None:
    await _expect_status_code(call, expected_code)
    await _expect_status_details(call, expected_details)


def _validate_payload_type_and_length(
    response: Union[
        messages_pb2.SimpleResponse, messages_pb2.StreamingOutputCallResponse
    ],
    expected_type: Any,
    expected_length: int,
) -> None:
    if response.payload.type is not expected_type:
        raise ValueError(
            "expected payload type %s, got %s"
            % (expected_type, type(response.payload.type))
        )
    elif len(response.payload.body) != expected_length:
        raise ValueError(
            "expected payload body size %d, got %d"
            % (expected_length, len(response.payload.body))
        )


async def _large_unary_common_behavior(
    stub: test_pb2_grpc.TestServiceStub,
    fill_username: bool,
    fill_oauth_scope: bool,
    call_credentials: Optional[grpc.CallCredentials],
) -> messages_pb2.SimpleResponse:
    size = 314159
    request = messages_pb2.SimpleRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_size=size,
        payload=messages_pb2.Payload(body=b"\x00" * 271828),
        fill_username=fill_username,
        fill_oauth_scope=fill_oauth_scope,
    )
    response = await stub.UnaryCall(request, credentials=call_credentials)
    _validate_payload_type_and_length(response, messages_pb2.COMPRESSABLE, size)
    return response


async def _empty_unary(stub: test_pb2_grpc.TestServiceStub) -> None:
    response = await stub.EmptyCall(empty_pb2.Empty())
    if not isinstance(response, empty_pb2.Empty):
        raise TypeError(
            'response is of type "%s", not empty_pb2.Empty!' % type(response)
        )


async def _large_unary(stub: test_pb2_grpc.TestServiceStub) -> None:
    await _large_unary_common_behavior(stub, False, False, None)


async def _client_streaming(stub: test_pb2_grpc.TestServiceStub) -> None:
    payload_body_sizes = (
        27182,
        8,
        1828,
        45904,
    )

    async def request_gen():
        for size in payload_body_sizes:
            yield messages_pb2.StreamingInputCallRequest(
                payload=messages_pb2.Payload(body=b"\x00" * size)
            )

    response = await stub.StreamingInputCall(request_gen())
    if response.aggregated_payload_size != sum(payload_body_sizes):
        raise ValueError(
            "incorrect size %d!" % response.aggregated_payload_size
        )


async def _server_streaming(stub: test_pb2_grpc.TestServiceStub) -> None:
    sizes = (
        31415,
        9,
        2653,
        58979,
    )

    request = messages_pb2.StreamingOutputCallRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_parameters=(
            messages_pb2.ResponseParameters(size=sizes[0]),
            messages_pb2.ResponseParameters(size=sizes[1]),
            messages_pb2.ResponseParameters(size=sizes[2]),
            messages_pb2.ResponseParameters(size=sizes[3]),
        ),
    )
    call = stub.StreamingOutputCall(request)
    for size in sizes:
        response = await call.read()
        _validate_payload_type_and_length(
            response, messages_pb2.COMPRESSABLE, size
        )


async def _ping_pong(stub: test_pb2_grpc.TestServiceStub) -> None:
    request_response_sizes = (
        31415,
        9,
        2653,
        58979,
    )
    request_payload_sizes = (
        27182,
        8,
        1828,
        45904,
    )

    call = stub.FullDuplexCall()
    for response_size, payload_size in zip(
        request_response_sizes, request_payload_sizes
    ):
        request = messages_pb2.StreamingOutputCallRequest(
            response_type=messages_pb2.COMPRESSABLE,
            response_parameters=(
                messages_pb2.ResponseParameters(size=response_size),
            ),
            payload=messages_pb2.Payload(body=b"\x00" * payload_size),
        )

        await call.write(request)
        response = await call.read()
        _validate_payload_type_and_length(
            response, messages_pb2.COMPRESSABLE, response_size
        )
    await call.done_writing()
    await _validate_status_code_and_details(call, grpc.StatusCode.OK, "")


async def _cancel_after_begin(stub: test_pb2_grpc.TestServiceStub):
    call = stub.StreamingInputCall()
    call.cancel()
    if not call.cancelled():
        raise ValueError("expected cancelled method to return True")
    code = await call.code()
    if code is not grpc.StatusCode.CANCELLED:
        raise ValueError("expected status code CANCELLED")


async def _cancel_after_first_response(stub: test_pb2_grpc.TestServiceStub):
    request_response_sizes = (
        31415,
        9,
        2653,
        58979,
    )
    request_payload_sizes = (
        27182,
        8,
        1828,
        45904,
    )

    call = stub.FullDuplexCall()

    response_size = request_response_sizes[0]
    payload_size = request_payload_sizes[0]
    request = messages_pb2.StreamingOutputCallRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_parameters=(
            messages_pb2.ResponseParameters(size=response_size),
        ),
        payload=messages_pb2.Payload(body=b"\x00" * payload_size),
    )

    await call.write(request)
    await call.read()

    call.cancel()

    try:
        await call.read()
    except asyncio.CancelledError:
        assert await call.code() is grpc.StatusCode.CANCELLED
    else:
        raise ValueError("expected call to be cancelled")


async def _timeout_on_sleeping_server(stub: test_pb2_grpc.TestServiceStub):
    request_payload_size = 27182
    time_limit = datetime.timedelta(seconds=1)

    call = stub.FullDuplexCall(timeout=time_limit.total_seconds())

    request = messages_pb2.StreamingOutputCallRequest(
        response_type=messages_pb2.COMPRESSABLE,
        payload=messages_pb2.Payload(body=b"\x00" * request_payload_size),
        response_parameters=(
            messages_pb2.ResponseParameters(
                interval_us=int(time_limit.total_seconds() * 2 * 10**6)
            ),
        ),
    )
    await call.write(request)
    await call.done_writing()
    try:
        await call.read()
    except aio.AioRpcError as rpc_error:
        if rpc_error.code() is not grpc.StatusCode.DEADLINE_EXCEEDED:
            raise
    else:
        raise ValueError("expected call to exceed deadline")


async def _empty_stream(stub: test_pb2_grpc.TestServiceStub):
    call = stub.FullDuplexCall()
    await call.done_writing()
    assert await call.read() == aio.EOF


async def _status_code_and_message(stub: test_pb2_grpc.TestServiceStub):
    details = "test status message"
    status = grpc.StatusCode.UNKNOWN  # code = 2

    # Test with a UnaryCall
    request = messages_pb2.SimpleRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_size=1,
        payload=messages_pb2.Payload(body=b"\x00"),
        response_status=messages_pb2.EchoStatus(
            code=status.value[0], message=details
        ),
    )
    call = stub.UnaryCall(request)
    await _validate_status_code_and_details(call, status, details)

    # Test with a FullDuplexCall
    call = stub.FullDuplexCall()
    request = messages_pb2.StreamingOutputCallRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_parameters=(messages_pb2.ResponseParameters(size=1),),
        payload=messages_pb2.Payload(body=b"\x00"),
        response_status=messages_pb2.EchoStatus(
            code=status.value[0], message=details
        ),
    )
    await call.write(request)  # sends the initial request.
    await call.done_writing()
    try:
        await call.read()
    except aio.AioRpcError as rpc_error:
        assert rpc_error.code() == status
    await _validate_status_code_and_details(call, status, details)


async def _unimplemented_method(stub: test_pb2_grpc.TestServiceStub):
    call = stub.UnimplementedCall(empty_pb2.Empty())
    await _expect_status_code(call, grpc.StatusCode.UNIMPLEMENTED)


async def _unimplemented_service(stub: test_pb2_grpc.UnimplementedServiceStub):
    call = stub.UnimplementedCall(empty_pb2.Empty())
    await _expect_status_code(call, grpc.StatusCode.UNIMPLEMENTED)


async def _custom_metadata(stub: test_pb2_grpc.TestServiceStub):
    initial_metadata_value = "test_initial_metadata_value"
    trailing_metadata_value = b"\x0a\x0b\x0a\x0b\x0a\x0b"
    metadata = aio.Metadata(
        (_INITIAL_METADATA_KEY, initial_metadata_value),
        (_TRAILING_METADATA_KEY, trailing_metadata_value),
    )

    async def _validate_metadata(call):
        initial_metadata = await call.initial_metadata()
        if initial_metadata[_INITIAL_METADATA_KEY] != initial_metadata_value:
            raise ValueError(
                "expected initial metadata %s, got %s"
                % (
                    initial_metadata_value,
                    initial_metadata[_INITIAL_METADATA_KEY],
                )
            )

        trailing_metadata = await call.trailing_metadata()
        if trailing_metadata[_TRAILING_METADATA_KEY] != trailing_metadata_value:
            raise ValueError(
                "expected trailing metadata %s, got %s"
                % (
                    trailing_metadata_value,
                    trailing_metadata[_TRAILING_METADATA_KEY],
                )
            )

    # Testing with UnaryCall
    request = messages_pb2.SimpleRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_size=1,
        payload=messages_pb2.Payload(body=b"\x00"),
    )
    call = stub.UnaryCall(request, metadata=metadata)
    await _validate_metadata(call)

    # Testing with FullDuplexCall
    call = stub.FullDuplexCall(metadata=metadata)
    request = messages_pb2.StreamingOutputCallRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_parameters=(messages_pb2.ResponseParameters(size=1),),
    )
    await call.write(request)
    await call.read()
    await call.done_writing()
    await _validate_metadata(call)


async def _compute_engine_creds(
    stub: test_pb2_grpc.TestServiceStub, args: argparse.Namespace
):
    response = await _large_unary_common_behavior(stub, True, True, None)
    if args.default_service_account != response.username:
        raise ValueError(
            "expected username %s, got %s"
            % (args.default_service_account, response.username)
        )


async def _oauth2_auth_token(
    stub: test_pb2_grpc.TestServiceStub, args: argparse.Namespace
):
    json_key_filename = os.environ[google_auth_environment_vars.CREDENTIALS]
    wanted_email = json.load(open(json_key_filename, "r"))["client_email"]
    response = await _large_unary_common_behavior(stub, True, True, None)
    if wanted_email != response.username:
        raise ValueError(
            "expected username %s, got %s" % (wanted_email, response.username)
        )
    if args.oauth_scope.find(response.oauth_scope) == -1:
        raise ValueError(
            'expected to find oauth scope "{}" in received "{}"'.format(
                response.oauth_scope, args.oauth_scope
            )
        )


async def _jwt_token_creds(stub: test_pb2_grpc.TestServiceStub):
    json_key_filename = os.environ[google_auth_environment_vars.CREDENTIALS]
    wanted_email = json.load(open(json_key_filename, "r"))["client_email"]
    response = await _large_unary_common_behavior(stub, True, False, None)
    if wanted_email != response.username:
        raise ValueError(
            "expected username %s, got %s" % (wanted_email, response.username)
        )


async def _per_rpc_creds(
    stub: test_pb2_grpc.TestServiceStub, args: argparse.Namespace
):
    json_key_filename = os.environ[google_auth_environment_vars.CREDENTIALS]
    wanted_email = json.load(open(json_key_filename, "r"))["client_email"]
    google_credentials, unused_project_id = google_auth.default(
        scopes=[args.oauth_scope]
    )
    call_credentials = grpc.metadata_call_credentials(
        google_auth_transport_grpc.AuthMetadataPlugin(
            credentials=google_credentials,
            request=google_auth_transport_requests.Request(),
        )
    )
    response = await _large_unary_common_behavior(
        stub, True, False, call_credentials
    )
    if wanted_email != response.username:
        raise ValueError(
            "expected username %s, got %s" % (wanted_email, response.username)
        )


async def _special_status_message(stub: test_pb2_grpc.TestServiceStub):
    details = (
        b"\t\ntest with whitespace\r\nand Unicode BMP \xe2\x98\xba and non-BMP"
        b" \xf0\x9f\x98\x88\t\n".decode("utf-8")
    )
    status = grpc.StatusCode.UNKNOWN  # code = 2

    # Test with a UnaryCall
    request = messages_pb2.SimpleRequest(
        response_type=messages_pb2.COMPRESSABLE,
        response_size=1,
        payload=messages_pb2.Payload(body=b"\x00"),
        response_status=messages_pb2.EchoStatus(
            code=status.value[0], message=details
        ),
    )
    call = stub.UnaryCall(request)
    await _validate_status_code_and_details(call, status, details)


@enum.unique
class TestCase(enum.Enum):
    EMPTY_UNARY = "empty_unary"
    LARGE_UNARY = "large_unary"
    SERVER_STREAMING = "server_streaming"
    CLIENT_STREAMING = "client_streaming"
    PING_PONG = "ping_pong"
    CANCEL_AFTER_BEGIN = "cancel_after_begin"
    CANCEL_AFTER_FIRST_RESPONSE = "cancel_after_first_response"
    TIMEOUT_ON_SLEEPING_SERVER = "timeout_on_sleeping_server"
    EMPTY_STREAM = "empty_stream"
    STATUS_CODE_AND_MESSAGE = "status_code_and_message"
    UNIMPLEMENTED_METHOD = "unimplemented_method"
    UNIMPLEMENTED_SERVICE = "unimplemented_service"
    CUSTOM_METADATA = "custom_metadata"
    COMPUTE_ENGINE_CREDS = "compute_engine_creds"
    OAUTH2_AUTH_TOKEN = "oauth2_auth_token"
    JWT_TOKEN_CREDS = "jwt_token_creds"
    PER_RPC_CREDS = "per_rpc_creds"
    SPECIAL_STATUS_MESSAGE = "special_status_message"


_TEST_CASE_IMPLEMENTATION_MAPPING = {
    TestCase.EMPTY_UNARY: _empty_unary,
    TestCase.LARGE_UNARY: _large_unary,
    TestCase.SERVER_STREAMING: _server_streaming,
    TestCase.CLIENT_STREAMING: _client_streaming,
    TestCase.PING_PONG: _ping_pong,
    TestCase.CANCEL_AFTER_BEGIN: _cancel_after_begin,
    TestCase.CANCEL_AFTER_FIRST_RESPONSE: _cancel_after_first_response,
    TestCase.TIMEOUT_ON_SLEEPING_SERVER: _timeout_on_sleeping_server,
    TestCase.EMPTY_STREAM: _empty_stream,
    TestCase.STATUS_CODE_AND_MESSAGE: _status_code_and_message,
    TestCase.UNIMPLEMENTED_METHOD: _unimplemented_method,
    TestCase.UNIMPLEMENTED_SERVICE: _unimplemented_service,
    TestCase.CUSTOM_METADATA: _custom_metadata,
    TestCase.COMPUTE_ENGINE_CREDS: _compute_engine_creds,
    TestCase.OAUTH2_AUTH_TOKEN: _oauth2_auth_token,
    TestCase.JWT_TOKEN_CREDS: _jwt_token_creds,
    TestCase.PER_RPC_CREDS: _per_rpc_creds,
    TestCase.SPECIAL_STATUS_MESSAGE: _special_status_message,
}


async def test_interoperability(
    case: TestCase,
    stub: test_pb2_grpc.TestServiceStub,
    args: Optional[argparse.Namespace] = None,
) -> None:
    method = _TEST_CASE_IMPLEMENTATION_MAPPING.get(case)
    if method is None:
        raise NotImplementedError(f'Test case "{case}" not implemented!')
    else:
        num_params = len(inspect.signature(method).parameters)
        if num_params == 1:
            await method(stub)
        elif num_params == 2:
            if args is not None:
                await method(stub, args)
            else:
                raise ValueError(f"Failed to run case [{case}]: args is None")
        else:
            raise ValueError(f"Invalid number of parameters [{num_params}]")
