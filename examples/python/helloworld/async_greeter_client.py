# Copyright 2020 gRPC authors.
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

import logging
import asyncio
import grpc

import helloworld_pb2
import helloworld_pb2_grpc


# this is the example code if we have the stub (auto generated async code)
# this will require changes in protoc-gen-lyft-python repo to generate asyn grpc streaming client/stub
async def run() -> None:
    # NOTE(gRPC Python Team): .close() is possible on a channel and should be
    # used in circumstances in which the with statement does not fit the needs
    # of the code.
    async with grpc.aio.insecure_channel('localhost:50051') as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        async def request_iterator():
            # this won't work for userclient. we need an infinite loop for each action and break out of the loop when the client is done with this action
            for i in range(3):
                request = helloworld_pb2.HelloRequest(name=str(i))
                yield request

        call = stub.SayHelloStreaming(request_iterator())
        await call.wait_for_connection()
        response_cnt = 0
        async for response in call:
            response_cnt += 1
            print(f"Greeter client received {response_cnt}: " + response.message)


if __name__ == '__main__':
    logging.basicConfig()
    asyncio.run(run())
