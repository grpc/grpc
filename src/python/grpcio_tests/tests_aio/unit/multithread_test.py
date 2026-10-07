# Copyright 2026 gRPC authors.
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
import concurrent.futures
import os
import queue
import threading
import time
import unittest

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase

_LOOP_TIMEOUT_S = 45.0
_LOOPS = 8
_CONCURRENCY = 50
_TIMEOUT_S = 90.0


def _open_fds():
    return len(os.listdir("/proc/self/fd"))


class GenericService:
    @staticmethod
    async def UnaryCall(request, context):
        return request


class MultithreadTest(AioTestBase):
    async def _start_server(self):
        server = grpc.aio.server()
        rpc_method_handlers = {
            "UnaryCall": grpc.unary_unary_rpc_method_handler(
                GenericService.UnaryCall,
            ),
        }
        generic_handler = grpc.method_handlers_generic_handler(
            "grpc.testing.TestService", rpc_method_handlers
        )
        server.add_generic_rpc_handlers((generic_handler,))
        port = server.add_insecure_port("[::]:0")
        await server.start()
        return port, server

    async def run_client(self, port, num_of_rpcs=1):
        async with aio.insecure_channel(f"localhost:{port}") as channel:
            unary_call = channel.unary_unary(
                "/grpc.testing.TestService/UnaryCall"
            )
            return await asyncio.gather(
                *(unary_call(b"request") for _ in range(num_of_rpcs))
            )

    def thread_target(self, coro_factory, results):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            result = loop.run_until_complete(coro_factory())
            results.put(result)
        except Exception as e:
            results.put(e)
        finally:
            loop.run_until_complete(loop.shutdown_asyncgens())
            loop.close()

    async def _run_in_threads(
        self, coro_factories, executor=None, timeout=None
    ):
        results = queue.Queue()
        threads = [
            threading.Thread(
                target=self.thread_target,
                args=(coro_factory, results),
                daemon=True,
            )
            for coro_factory in coro_factories
        ]

        def run():
            for t in threads:
                t.start()
            deadline = time.monotonic() + timeout if timeout else None
            for t in threads:
                remaining = None
                if deadline:
                    remaining = max(0.0, deadline - time.monotonic())
                t.join(remaining)
            return sum(t.is_alive() for t in threads)

        stalled = await self.loop.run_in_executor(executor, run)
        self.assertEqual(
            0, stalled, f"{stalled} of {len(threads)} threads stalled"
        )
        return [results.get_nowait() for _ in range(results.qsize())]

    async def _test_multithread(self, executor=None):
        port, server = await self._start_server()
        results = await self._run_in_threads(
            [lambda: self.run_client(port)] * _CONCURRENCY, executor=executor
        )
        await server.stop(None)

        # Verify results
        self.assertEqual([[b"request"]] * _CONCURRENCY, results)

    @unittest.skipUnless(
        os.path.isdir("/proc/self/fd"), "Needs /proc/self/fd dir"
    )
    async def test_temporary_event_loops_do_not_lead_fds(self):
        port, server = await self._start_server()
        client = lambda: self.run_client(port)
        keeper = aio.insecure_channel(f"localhost:{port}")
        try:
            await keeper.channel_ready()
            # warm-up: lazily created core resources must not count as growth
            self.assertEqual(
                [[b"request"]],
                await self._run_in_threads([client], timeout=_LOOP_TIMEOUT_S),
            )
            fds_before = _open_fds()

            # spawn _CONCURRENCY count temporary loops, each one adding two fd's
            self.assertEqual(
                [[b"request"]] * _CONCURRENCY,
                await self._run_in_threads(
                    [client] * _CONCURRENCY, timeout=_LOOP_TIMEOUT_S
                ),
            )

            # sweep temporary loops
            self.assertEqual(
                [[b"request"]],
                await self._run_in_threads([client], timeout=_LOOP_TIMEOUT_S),
            )

            delta_fds = _open_fds() - fds_before
            self.assertEqual(
                0,
                delta_fds,
                f"{delta_fds} fds leaked over {_CONCURRENCY} event loops",
            )
        finally:
            await keeper.close()
            await server.stop(None)

    async def test_concurrent_event_loops_do_no_hang(self):
        async def serve_and_call():
            port, server = await self._start_server()
            try:
                return await self.run_client(port, _CONCURRENCY)
            finally:
                await server.stop(None)

        self.assertEqual(
            [[b"request"] * _CONCURRENCY] * _LOOPS,
            await self._run_in_threads(
                [serve_and_call] * _LOOPS, timeout=_TIMEOUT_S
            ),
        )

    async def test_multithread(self):
        await self._test_multithread(executor=None)

    async def test_multithread_with_thread_pool(self):
        with concurrent.futures.ThreadPoolExecutor() as pool:
            await self._test_multithread(executor=pool)


if __name__ == "__main__":
    unittest.main(verbosity=2)
