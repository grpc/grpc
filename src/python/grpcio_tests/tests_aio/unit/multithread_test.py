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
from concurrent import futures
import queue
import threading
import unittest

import grpc
from grpc.experimental import aio


class GenericService:
    @staticmethod
    def UnaryCall(request, context):
        return request


class MultithreadTest(unittest.TestCase):
    def test_multithread(self):
        # Create server with port 0 (dynamic based on available ports)
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
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
        server.start()

        results_queue = queue.Queue()

        async def run_client(port):
            async with aio.insecure_channel(f"localhost:{port}") as channel:
                unary_call = channel.unary_unary(
                    "/grpc.testing.TestService/UnaryCall"
                )
                response = await unary_call(b"request")
                return response

        def thread_target(port, q):
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            try:
                response = loop.run_until_complete(run_client(port))
                q.put(response)
            except Exception as e:
                q.put(e)
            finally:
                loop.close()

        threads = []
        for _ in range(10):
            t = threading.Thread(
                target=thread_target, args=(port, results_queue)
            )
            t.start()
            threads.append(t)

        for t in threads:
            t.join()

        server.stop(0)

        # Verify results
        self.assertEqual(
            results_queue.qsize(), 10, "Expected 10 results in queue"
        )
        while not results_queue.empty():
            result = results_queue.get()
            self.assertIsInstance(
                result, bytes, f"Expected bytes result, got {type(result)}"
            )
            self.assertEqual(
                result, b"request", f"Expected b'request', got {result}"
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
