# Copyright 2021 The gRPC Authors
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
"""A flaky backend for the gRPC Python retry example."""

import asyncio
import collections
import logging
import random

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)


class ErrorInjectingGreeter(helloworld_pb2_grpc.GreeterServicer):
    def __init__(self):
        self._counter = collections.defaultdict(int)

    async def SayHello(
        self,
        request: helloworld_pb2.HelloRequest,
        context: grpc.aio.ServicerContext,
    ) -> helloworld_pb2.HelloReply:
        self._counter[context.peer()] += 1
        if self._counter[context.peer()] < 5:
            if random.random() < 0.75:
                logging.info("Injecting error to RPC from %s", context.peer())
                await context.abort(
                    grpc.StatusCode.UNAVAILABLE, "injected error"
                )
        logging.info("Successfully responding to RPC from %s", context.peer())
        return helloworld_pb2.HelloReply(message="Hello, %s!" % request.name)


async def serve() -> None:
    server = grpc.aio.server()
    helloworld_pb2_grpc.add_GreeterServicer_to_server(
        ErrorInjectingGreeter(), server
    )
    listen_addr = "[::]:50051"
    server.add_insecure_port(listen_addr)
    logging.info("Starting flaky server on %s", listen_addr)
    await server.start()
    await server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(serve())
