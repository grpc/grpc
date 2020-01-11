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

import asyncio
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


async def _block_until_certain_state(channel, expected_state):
    state = channel.get_state()
    while state != expected_state:
        await channel.wait_for_state_change(state)
        state = channel.get_state()


class TestConnectivityState(AioTestBase):

    async def setUp(self):
        self._server_address, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_unavailable_backend(self):
        async with aio.insecure_channel(_INVALID_BACKEND_ADDRESS) as channel:
            self.assertEqual(grpc.ChannelConnectivity.IDLE,
                             channel.get_state(False))
            self.assertEqual(grpc.ChannelConnectivity.IDLE,
                             channel.get_state(True))

            async def waiting_transient_failure():
                state = channel.get_state()
                while state != grpc.ChannelConnectivity.TRANSIENT_FAILURE:
                    channel.wait_for_state_change(state)

            # Should not time out
            await asyncio.wait_for(
                _block_until_certain_state(
                    channel, grpc.ChannelConnectivity.TRANSIENT_FAILURE),
                test_constants.SHORT_TIMEOUT)

    async def test_normal_backend(self):
        async with aio.insecure_channel(self._server_address) as channel:
            current_state = channel.get_state(True)
            self.assertEqual(grpc.ChannelConnectivity.IDLE, current_state)

            # Should not time out
            await asyncio.wait_for(
                _block_until_certain_state(channel,
                                           grpc.ChannelConnectivity.READY),
                test_constants.SHORT_TIMEOUT)

    async def test_timeout(self):
        async with aio.insecure_channel(self._server_address) as channel:
            self.assertEqual(grpc.ChannelConnectivity.IDLE,
                             channel.get_state(False))

            # If timed out, the function should return None.
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(
                    _block_until_certain_state(channel,
                                               grpc.ChannelConnectivity.READY),
                    test_constants.SHORT_TIMEOUT)

    async def test_shutdown(self):
        channel = aio.insecure_channel(self._server_address)

        self.assertEqual(grpc.ChannelConnectivity.IDLE,
                         channel.get_state(False))

        await channel.close()

        self.assertEqual(grpc.ChannelConnectivity.SHUTDOWN,
                         channel.get_state(True))

        self.assertEqual(grpc.ChannelConnectivity.SHUTDOWN,
                         channel.get_state(False))

        # It can raise exceptions since it is an usage error, but it should not
        # segfault or abort.
        with self.assertRaises(RuntimeError):
            await channel.wait_for_state_change(
                grpc.ChannelConnectivity.SHUTDOWN)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
