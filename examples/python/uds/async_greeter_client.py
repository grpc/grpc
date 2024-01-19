# Copyright 2022 The gRPC Authors
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
"""The gRPC AsyncIO client for the UDS example."""

import asyncio
import logging

import grpc
import helloworld_pb2
import helloworld_pb2_grpc


async def run() -> None:
    uds_addresses = ["unix:helloworld.sock", "unix:///tmp/helloworld.sock"]
    for uds_address in uds_addresses:
        async with grpc.aio.insecure_channel(uds_address) as channel:
            stub = helloworld_pb2_grpc.GreeterStub(channel)
            response = await stub.SayHello(
                helloworld_pb2.HelloRequest(name="you")
            )
            logging.info("Received: %s", response.message)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(run())
