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
import queue
import threading
import unittest

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase


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

    async def run_client(self, port):
        async with aio.insecure_channel(f"localhost:{port}") as channel:
            unary_call = channel.unary_unary(
                "/grpc.testing.TestService/UnaryCall"
            )
            response = await unary_call(b"request")
            return response

    def thread_target(self, port, queue):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            response = loop.run_until_complete(self.run_client(port))
            queue.put(response)
        except Exception as e:
            queue.put(e)
        finally:
            loop.run_until_complete(loop.shutdown_asyncgens())
            loop.close()

    async def _test_multithread(self, executor=None):
        results_queue = queue.Queue()
        port, server = await self.start_server()
        threads = []
        for _ in range(10):
            t = threading.Thread(
                target=self.thread_target, args=(port, results_queue)
            )
            t.start()
            threads.append(t)

        def join_threads():
            for t in threads:
                t.join()

        await self.loop.run_in_executor(None, join_threads)

        await server.stop(None)

        # Verify results
        self.assertEqual(results_queue.qsize(), 10)
        while not results_queue.empty():
            result = results_queue.get()
            self.assertIsInstance(result, bytes)
            self.assertEqual(result, b"request")

    async def test_multithread_with_thread_pool(self):
        with concurrent.futures.ThreadPoolExecutor() as pool:
            await self._test_multithread(executor=pool)

if __name__ == "__main__":
    unittest.main(verbosity=2)
