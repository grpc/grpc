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
"""Tests behavior around the metadata mechanism."""

import asyncio
import logging
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests_aio.unit._test_server import start_test_server

_NUM_OF_LOOPS = 50


class TestOutsideInit(unittest.TestCase):
    def test_behavior_outside_asyncio(self):
        # Ensures non-AsyncIO object can be initiated
        channel_creds = grpc.ssl_channel_credentials()

        # Ensures AsyncIO API not raising outside of AsyncIO.
        # NOTE(lidiz) This behavior is bound with GAPIC generator, and required
        # by test frameworks like pytest. In test frameworks, objects shared
        # across cases need to be created outside of AsyncIO coroutines.
        aio.insecure_channel("")
        aio.secure_channel("", channel_creds)
        aio.server()
        aio.init_grpc_aio()
        aio.shutdown_grpc_aio()

    def test_multi_ephemeral_loops(self):
        # Initializes AIO module outside. It's part of the test. We especially
        # want to ensure the closing of the default loop won't cause deadlocks.
        aio.init_grpc_aio()

        async def ping_pong():
            address, server = await start_test_server()
            channel = aio.insecure_channel(address)
            stub = test_pb2_grpc.TestServiceStub(channel)

            await stub.UnaryCall(messages_pb2.SimpleRequest())

            await channel.close()
            await server.stop(None)

        for i in range(_NUM_OF_LOOPS):
            old_loop = asyncio.get_event_loop()
            old_loop.close()

            loop = asyncio.new_event_loop()
            loop.set_debug(True)
            asyncio.set_event_loop(loop)

            loop.run_until_complete(ping_pong())

        aio.shutdown_grpc_aio()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
