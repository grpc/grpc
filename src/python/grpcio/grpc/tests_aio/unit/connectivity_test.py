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
import platform
import threading
import time
import unittest

import grpc
from grpc.experimental import aio

from tests.unit.framework.common import test_constants
from tests_aio.unit import _common
from tests_aio.unit._constants import UNREACHABLE_TARGET
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server


class TestConnectivityState(AioTestBase):
    async def setUp(self):
        self._server_address, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    @unittest.skipIf(
        "aarch64" in platform.machine(),
        "The transient failure propagation is slower on aarch64",
    )
    async def test_unavailable_backend(self):
        async with aio.insecure_channel(UNREACHABLE_TARGET) as channel:
            self.assertEqual(
                grpc.ChannelConnectivity.IDLE, channel.get_state(False)
            )
            self.assertEqual(
                grpc.ChannelConnectivity.IDLE, channel.get_state(True)
            )

            # Should not time out
            await asyncio.wait_for(
                _common.block_until_certain_state(
                    channel, grpc.ChannelConnectivity.TRANSIENT_FAILURE
                ),
                test_constants.SHORT_TIMEOUT,
            )

    async def test_normal_backend(self):
        async with aio.insecure_channel(self._server_address) as channel:
            current_state = channel.get_state(True)
            self.assertEqual(grpc.ChannelConnectivity.IDLE, current_state)

            # Should not time out
            await asyncio.wait_for(
                _common.block_until_certain_state(
                    channel, grpc.ChannelConnectivity.READY
                ),
                test_constants.SHORT_TIMEOUT,
            )

    async def test_timeout(self):
        async with aio.insecure_channel(self._server_address) as channel:
            self.assertEqual(
                grpc.ChannelConnectivity.IDLE, channel.get_state(False)
            )

            # If timed out, the function should return None.
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(
                    _common.block_until_certain_state(
                        channel, grpc.ChannelConnectivity.READY
                    ),
                    test_constants.SHORT_TIMEOUT,
                )

    async def test_shutdown(self):
        channel = aio.insecure_channel(self._server_address)

        self.assertEqual(
            grpc.ChannelConnectivity.IDLE, channel.get_state(False)
        )

        # Waiting for changes in a separate coroutine
        wait_started = asyncio.Event()

        async def a_pending_wait():
            wait_started.set()
            await channel.wait_for_state_change(grpc.ChannelConnectivity.IDLE)

        pending_task = self.loop.create_task(a_pending_wait())
        await wait_started.wait()

        await channel.close()

        self.assertEqual(
            grpc.ChannelConnectivity.SHUTDOWN, channel.get_state(True)
        )

        self.assertEqual(
            grpc.ChannelConnectivity.SHUTDOWN, channel.get_state(False)
        )

        # Make sure there isn't any exception in the task
        await pending_task

        # It can raise exceptions since it is an usage error, but it should not
        # segfault or abort.
        with self.assertRaises(aio.UsageError):
            await channel.wait_for_state_change(
                grpc.ChannelConnectivity.SHUTDOWN
            )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
