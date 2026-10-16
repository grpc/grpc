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

import asyncio
import logging
import unittest

from grpc.experimental import aio

from src.proto.grpc.testing import benchmark_service_pb2_grpc
from tests_aio.benchmark import benchmark_servicer


async def _start_async_server():
    server = aio.server()

    port = server.add_insecure_port("localhost:%s" % 50051)
    servicer = benchmark_servicer.BenchmarkServicer()
    benchmark_service_pb2_grpc.add_BenchmarkServiceServicer_to_server(
        servicer, server
    )

    await server.start()
    logging.info("Benchmark server started at :%d" % port)
    await server.wait_for_termination()


def main():
    loop = asyncio.get_event_loop()
    loop.create_task(_start_async_server())
    loop.run_forever()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    main()
