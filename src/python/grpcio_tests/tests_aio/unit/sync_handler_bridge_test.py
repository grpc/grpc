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
"""Tests for the cygrpc helpers bridging sync and async code in the server."""

import asyncio
import concurrent.futures
import inspect
import logging
import threading
import unittest

from grpc._cython import cygrpc

from tests.unit.framework.common import test_constants
from tests_aio.unit import _common
from tests_aio.unit._test_base import AioTestBase


class TestScheduleCoroThreadsafe(unittest.TestCase):
    def setUp(self):
        self._loop_thread = _common.LoopInThread(debug=False)

    def tearDown(self):
        self._loop_thread.stop()

    def test_from_foreign_thread(self):
        coro_executed = threading.Event()

        async def coro():
            coro_executed.set()

        scheduled = cygrpc.schedule_coro_threadsafe(
            coro(), self._loop_thread.loop
        )

        self.assertIsInstance(scheduled, concurrent.futures.Future)
        self.assertTrue(
            coro_executed.wait(timeout=test_constants.TIME_ALLOWANCE),
            "coroutine was never executed",
        )
        scheduled.result(timeout=test_constants.TIME_ALLOWANCE)

    def test_from_loop_thread(self):
        async def schedule_on_owning_thread():
            coro_executed = asyncio.Event()

            async def coro():
                coro_executed.set()

            scheduled = cygrpc.schedule_coro_threadsafe(
                coro(), asyncio.get_running_loop()
            )
            self.assertIsInstance(scheduled, asyncio.Task)
            await asyncio.wait_for(
                coro_executed.wait(), test_constants.TIME_ALLOWANCE
            )
            await scheduled

        self._loop_thread.run(schedule_on_owning_thread())


class _GeneratorTestBase(AioTestBase):
    async def _in_thread(self, fn, *args):
        # `None` picks the loop's default ThreadPoolExecutor, deliberately not
        # the DaemonThreadExecutor whose workers are being counted.
        return await self.loop.run_in_executor(None, fn, *args)


class TestAsyncGeneratorToGenerator(_GeneratorTestBase):
    async def test_items_are_forwarded(self):
        executor = _common.DaemonThreadExecutor()

        async def items():
            for item in (b"a", b"b", b"c"):
                yield item

        gen = cygrpc.async_generator_to_generator(items(), self.loop)
        self.assertEqual(
            [b"a", b"b", b"c"],
            await self.loop.run_in_executor(executor, list, gen),
        )
        self.assertTrue(
            await self._in_thread(
                executor.workers_finished, test_constants.TIME_ALLOWANCE
            ),
            "thread pool worker leaked",
        )

    async def test_eof_ends_iteration(self):
        executor = _common.DaemonThreadExecutor()

        async def items_with_eof():
            yield b"a"
            yield cygrpc.EOF
            yield b"never"

        gen = cygrpc.async_generator_to_generator(items_with_eof(), self.loop)
        self.assertEqual(
            [b"a"], await self.loop.run_in_executor(executor, list, gen)
        )
        self.assertTrue(
            await self._in_thread(
                executor.workers_finished, test_constants.TIME_ALLOWANCE
            ),
            "thread pool worker leaked",
        )


class TestGeneratorToAsyncGenerator(_GeneratorTestBase):
    async def test_items_are_forwarded(self):
        executor = _common.DaemonThreadExecutor()

        def items():
            yield from (b"a", b"b", b"c")

        agen = cygrpc.generator_to_async_generator(items(), self.loop, executor)
        self.assertEqual([b"a", b"b", b"c"], [item async for item in agen])
        self.assertTrue(
            await self._in_thread(
                executor.workers_finished, test_constants.TIME_ALLOWANCE
            ),
            "thread pool worker leaked",
        )

    async def test_generator_error_is_forwarded(self):
        executor = _common.DaemonThreadExecutor()

        def failing():
            yield b"a"
            raise ValueError("boom")

        agen = cygrpc.generator_to_async_generator(
            failing(), self.loop, executor
        )
        self.assertEqual(b"a", await agen.__anext__())
        with self.assertRaises(ValueError):
            await agen.__anext__()
        self.assertTrue(
            await self._in_thread(
                executor.workers_finished, test_constants.TIME_ALLOWANCE
            ),
            "thread pool worker leaked",
        )

    async def test_async_generator_closure_releases_worker(self):
        executor = _common.DaemonThreadExecutor()
        closed = threading.Event()

        def endless():
            try:
                while True:
                    yield b"item"
            finally:
                closed.set()

        agen = cygrpc.generator_to_async_generator(
            endless(), self.loop, executor
        )
        self.assertEqual(b"item", await agen.__anext__())
        self.assertEqual(b"item", await agen.__anext__())

        # The worker is now parked in queue.put() on the full one-slot queue.
        await agen.aclose()

        self.assertTrue(
            await self._in_thread(closed.wait, test_constants.TIME_ALLOWANCE),
            "generator was not closed",
        )
        self.assertTrue(
            await self._in_thread(
                executor.workers_finished, test_constants.TIME_ALLOWANCE
            ),
            "thread pool worker leaked",
        )

    async def test_cancelled_consumer_releases_worker(self):
        executor = _common.DaemonThreadExecutor()
        release = threading.Event()
        closed = threading.Event()

        def blocking():
            try:
                yield b"first"
                release.wait(test_constants.TIME_ALLOWANCE)
                yield b"second"
                yield b"third"
            finally:
                closed.set()

        agen = cygrpc.generator_to_async_generator(
            blocking(), self.loop, executor
        )
        self.assertEqual(b"first", await agen.__anext__())

        async def consume_next():
            return await agen.__anext__()

        consumer = self.loop.create_task(consume_next())
        await asyncio.sleep(0.1)  # The consumer is suspended in queue.get().
        consumer.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await consumer

        release.set()
        self.assertTrue(
            await self._in_thread(closed.wait, test_constants.TIME_ALLOWANCE),
            "generator was not closed",
        )
        self.assertTrue(
            await self._in_thread(
                executor.workers_finished, test_constants.TIME_ALLOWANCE
            ),
            "thread pool worker leaked",
        )


class TestWorkerReleasedWhenLoopCloses(unittest.TestCase):
    """Tests that a worker thread is released when event loop is closed.

    A thread blocked on a future that only the loop can resolve has to give
    up once that loop is closed, instead of staying parked forever.
    """

    def setUp(self):
        self._loop_thread = _common.LoopInThread()

    def tearDown(self):
        self._loop_thread.stop()

    def test_async_generator_to_generator(self):
        executor = _common.DaemonThreadExecutor()

        async def never_yields():
            await asyncio.Event().wait()
            yield b"never"

        gen = cygrpc.async_generator_to_generator(
            never_yields(), self._loop_thread.loop
        )

        first_item = executor.submit(next, gen)
        self._loop_thread.run(asyncio.sleep(0.1))

        # close the event loop mid-stream
        self.assertTrue(self._loop_thread.stop())

        self.assertTrue(
            executor.workers_finished(test_constants.TIME_ALLOWANCE),
            "worker still blocked on a closed loop",
        )
        with self.assertRaises(RuntimeError):
            first_item.result(timeout=0)

    def test_generator_to_async_generator(self):
        executor = _common.DaemonThreadExecutor()
        closed = threading.Event()

        def endless():
            try:
                while True:
                    yield b"item"
            finally:
                closed.set()

        async def take_one():
            agen = cygrpc.generator_to_async_generator(
                endless(), asyncio.get_running_loop(), executor
            )
            await agen.__anext__()
            # let the worker be parked in queue.put()
            await asyncio.sleep(0.1)
            return agen  # Keep it alive. GC must not `aclose()` it for us.

        agen = self._loop_thread.run(take_one())
        # close the event loop mid-stream
        self.assertTrue(self._loop_thread.stop())
        self.assertTrue(
            executor.workers_finished(test_constants.TIME_ALLOWANCE),
            "worker still blocked on a closed loop",
        )
        self.assertTrue(closed.is_set(), "generator was not closed")
        del agen


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
