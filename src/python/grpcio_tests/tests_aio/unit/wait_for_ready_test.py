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
"""Testing the done callbacks mechanism."""

import asyncio
import gc
import logging
import platform
import time
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import get_socket
from tests.unit.framework.common import test_constants
from tests_aio.unit import _common
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 42


async def _perform_unary_unary(stub, wait_for_ready):
    await stub.UnaryCall(
        messages_pb2.SimpleRequest(),
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready,
    )


async def _perform_unary_stream(stub, wait_for_ready):
    request = messages_pb2.StreamingOutputCallRequest()
    for _ in range(_NUM_STREAM_RESPONSES):
        request.response_parameters.append(
            messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
        )

    call = stub.StreamingOutputCall(
        request,
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready,
    )

    for _ in range(_NUM_STREAM_RESPONSES):
        await call.read()
    assert await call.code() == grpc.StatusCode.OK


async def _perform_stream_unary(stub, wait_for_ready):
    payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
    request = messages_pb2.StreamingInputCallRequest(payload=payload)

    async def gen():
        for _ in range(_NUM_STREAM_RESPONSES):
            yield request

    await stub.StreamingInputCall(
        gen(),
        timeout=test_constants.LONG_TIMEOUT,
        wait_for_ready=wait_for_ready,
    )


async def _perform_stream_stream(stub, wait_for_ready):
    call = stub.FullDuplexCall(
        timeout=test_constants.LONG_TIMEOUT, wait_for_ready=wait_for_ready
    )

    request = messages_pb2.StreamingOutputCallRequest()
    request.response_parameters.append(
        messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
    )

    for _ in range(_NUM_STREAM_RESPONSES):
        await call.write(request)
        response = await call.read()
        assert _RESPONSE_PAYLOAD_SIZE == len(response.payload.body)

    await call.done_writing()
    assert await call.code() == grpc.StatusCode.OK


_RPC_ACTIONS = (
    _perform_unary_unary,
    _perform_unary_stream,
    _perform_stream_unary,
    _perform_stream_stream,
)


class TestWaitForReady(AioTestBase):
    async def setUp(self):
        address, self._port, self._socket = get_socket(listen=False)
        self._channel = aio.insecure_channel(f"{address}:{self._port}")
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)
        self._socket.close()

    async def tearDown(self):
        await self._channel.close()

    async def _connection_fails_fast(self, wait_for_ready):
        for action in _RPC_ACTIONS:
            with self.subTest(name=action):
                with self.assertRaises(aio.AioRpcError) as exception_context:
                    await action(self._stub, wait_for_ready)
                rpc_error = exception_context.exception
                self.assertEqual(grpc.StatusCode.UNAVAILABLE, rpc_error.code())

    async def test_call_wait_for_ready_default(self):
        """RPC should fail immediately after connection failed."""
        await self._connection_fails_fast(None)

    async def test_call_wait_for_ready_disabled(self):
        """RPC should fail immediately after connection failed."""
        await self._connection_fails_fast(False)

    @unittest.skipIf(
        platform.system() == "Windows",
        "https://github.com/grpc/grpc/pull/26729",
    )
    async def test_call_wait_for_ready_enabled(self):
        """RPC will wait until the connection is ready."""
        for action in _RPC_ACTIONS:
            with self.subTest(name=action.__name__):
                # Starts the RPC
                action_task = self.loop.create_task(action(self._stub, True))

                # Wait for TRANSIENT_FAILURE, and RPC is not aborting
                await _common.block_until_certain_state(
                    self._channel, grpc.ChannelConnectivity.TRANSIENT_FAILURE
                )

                try:
                    # Start the server
                    _, server = await start_test_server(port=self._port)

                    # The RPC should recover itself
                    await action_task
                finally:
                    if server is not None:
                        await server.stop(None)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
