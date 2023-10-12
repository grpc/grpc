# Copyright 2023 gRPC authors.
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
"""The Python AsyncIO implementation of the GRPC helloworld.Greeter client."""

import asyncio
import contextvars
import logging
import random

import grpc
import helloworld_pb2
import helloworld_pb2_grpc

test_var = contextvars.ContextVar("test", default="test")


async def run() -> None:
    async with grpc.aio.insecure_channel("localhost:50051") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        rpc_id = "{:032x}".format(random.getrandbits(128))
        metadata = grpc.aio.Metadata(
            ("client-rpc-id", rpc_id),
        )
        print(f"Sending request with rpc id: {rpc_id}")
        response = await stub.SayHello(
            helloworld_pb2.HelloRequest(name="you"), metadata=metadata
        )
    print("Greeter client received: " + response.message)


if __name__ == "__main__":
    logging.basicConfig()
    asyncio.run(run())
