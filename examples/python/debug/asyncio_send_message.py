# Copyright 2020 The gRPC Authors
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
"""Send multiple greeting messages to the backend."""

import asyncio
import logging
import argparse
import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto")


async def process(stub: helloworld_pb2_grpc.GreeterStub,
                  request: helloworld_pb2.HelloRequest) -> None:
    try:
        response = await stub.SayHello(request)
    except grpc.aio.AioRpcError as rpc_error:
        print(f'Received error: {rpc_error}')
    else:
        print(f'Received message: {response}')


async def run(addr: str, n: int) -> None:
    async with grpc.aio.insecure_channel(addr) as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        request = helloworld_pb2.HelloRequest(name='you')
        for _ in range(n):
            await process(stub, request)


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--addr',
                        nargs=1,
                        type=str,
                        default='[::]:50051',
                        help='the address to request')
    parser.add_argument('-n',
                        nargs=1,
                        type=int,
                        default=10,
                        help='an integer for number of messages to sent')
    args = parser.parse_args()
    await run(addr=args.addr, n=args.n)


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    asyncio.get_event_loop().run_until_complete(main())
