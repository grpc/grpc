# Copyright 2019 The gRPC Authors.
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
"""Tests behavior of the connectivity state."""

import logging
import threading
import unittest
import time
import grpc

from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import test_constants
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase

_INVALID_BACKEND_ADDRESS = '0.0.0.1:2'


class TestChannel(AioTestBase):

    async def setUp(self):
        self._server_address, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_unavailable_backend(self):
        channel = aio.insecure_channel(_INVALID_BACKEND_ADDRESS)

        self.assertEqual(grpc.ChannelConnectivity.IDLE,
                         channel.check_connectivity_state(False))
        self.assertEqual(grpc.ChannelConnectivity.IDLE,
                         channel.check_connectivity_state(True))
        self.assertEqual(
            grpc.ChannelConnectivity.CONNECTING, await
            channel.watch_connectivity_state(grpc.ChannelConnectivity.IDLE))
        self.assertEqual(
            grpc.ChannelConnectivity.TRANSIENT_FAILURE, await
            channel.watch_connectivity_state(grpc.ChannelConnectivity.CONNECTING
                                            ))

        await channel.close()

    async def test_normal_backend(self):
        channel = aio.insecure_channel(self._server_address)

        current_state = channel.check_connectivity_state(True)
        self.assertEqual(grpc.ChannelConnectivity.IDLE, current_state)

        deadline = time.time() + test_constants.SHORT_TIMEOUT

        while current_state != grpc.ChannelConnectivity.READY:
            current_state = await channel.watch_connectivity_state(
                current_state, deadline - time.time())
            self.assertIsNotNone(current_state)

        await channel.close()

    async def test_timeout(self):
        channel = aio.insecure_channel(self._server_address)

        self.assertEqual(grpc.ChannelConnectivity.IDLE,
                         channel.check_connectivity_state(False))

        # If timed out, the function should return None.
        self.assertIsNone(await channel.watch_connectivity_state(
            grpc.ChannelConnectivity.IDLE, test_constants.SHORT_TIMEOUT))

        await channel.close()

    async def test_shutdown(self):
        channel = aio.insecure_channel(self._server_address)

        self.assertEqual(grpc.ChannelConnectivity.IDLE,
                         channel.check_connectivity_state(False))

        await channel.close()

        self.assertEqual(grpc.ChannelConnectivity.SHUTDOWN,
                         channel.check_connectivity_state(False))


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
