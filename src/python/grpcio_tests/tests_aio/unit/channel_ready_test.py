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
"""Testing the channel_ready function."""

import asyncio
import gc
import logging
import socket
import time
import unittest

import grpc
from grpc.experimental import aio

from tests.unit.framework.common import get_socket, test_constants
from tests_aio.unit import _common
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server


class TestChannelReady(AioTestBase):

    async def setUp(self):
        address, self._port, self._socket = get_socket(
            listen=False, sock_options=(socket.SO_REUSEADDR,))
        self._channel = aio.insecure_channel(f"{address}:{self._port}")
        self._socket.close()

    async def tearDown(self):
        await self._channel.close()

    async def test_channel_ready_success(self):
        # Start `channel_ready` as another Task
        channel_ready_task = self.loop.create_task(
            self._channel.channel_ready())

        # Wait for TRANSIENT_FAILURE
        await _common.block_until_certain_state(
            self._channel, grpc.ChannelConnectivity.TRANSIENT_FAILURE)

        try:
            # Start the server
            _, server = await start_test_server(port=self._port)

            # The RPC should recover itself
            await channel_ready_task
        finally:
            await server.stop(None)

    async def test_channel_ready_blocked(self):
        with self.assertRaises(asyncio.TimeoutError):
            await asyncio.wait_for(self._channel.channel_ready(),
                                   test_constants.SHORT_TIMEOUT)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
