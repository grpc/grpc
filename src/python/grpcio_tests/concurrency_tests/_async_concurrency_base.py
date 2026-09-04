# Copyright 2026 The gRPC Authors
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
"""Shared test harness for aio concurrency test suites."""

import asyncio

from grpc.experimental import aio

from concurrency_tests._deadlock_debug import install_asyncio_suspended_task_debuggers
from concurrency_tests._deadlock_debug import install_deadlock_debuggers
from tests_aio.unit._test_base import AioTestBase


CONCURRENCY = 100
ITERATIONS_PER_TASK = 5


class AsyncConcurrencyTestCase(AioTestBase):
    """Base class providing the aio concurrent tasks harness"""

    concurrency = CONCURRENCY

    async def setUp(self) -> None:
        install_deadlock_debuggers(self.addCleanup)
        install_asyncio_suspended_task_debuggers(self.addCleanup)
        # multiple tasks are created and released in the exact same time
        self.loop.slow_callback_duration = 5.0
        self._release = asyncio.Event()
        self._aio_cleanups = []

    async def tearDown(self) -> None:
        for cleanup in reversed(self._aio_cleanups):
            await cleanup()

    async def start_server(self, servicer, add_to_server, stub_class):
        """Starts insecure aio server for the servicer; returns (server, stub)

        The channel and server are torn down via addCleanup, after tearDown's
        join, so a hang is detected before anything is stopped.
        """
        server = aio.server()
        add_to_server(servicer, server)
        port = server.add_insecure_port("localhost:0")
        await server.start()
        channel = aio.insecure_channel(f"localhost:{port}")
        self._aio_cleanups.append(lambda: server.stop(None))
        self._aio_cleanups.append(lambda: channel.close())
        return server, stub_class(channel)

    async def _worker(self, factory, index):
        await self._release.wait()
        await factory(index)

    async def run_workers(self, *factories):
        """Runs `concurrency` tasks round robin over factories"""
        tasks = [
            asyncio.ensure_future(
                self._worker(factories[i % len(factories)], i)
            )
            for i in range(self.concurrency)
        ]
        self._release.set()
        results = await asyncio.gather(*tasks, return_exceptions=True)
        errors = [
            repr(r)
            for r in results
            if isinstance(r, BaseException)
            and not isinstance(r, asyncio.CancelledError)
        ]
        self.assertEqual([], errors, "\n".join(errors))