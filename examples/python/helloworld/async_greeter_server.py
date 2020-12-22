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
"""The Python AsyncIO implementation of the GRPC helloworld.Greeter server."""

import logging
import asyncio
import grpc
import helloworld_pb2
import helloworld_pb2_grpc
_STREAM_STREAM_ASYNC_GEN = '/test/StreamStreamAsyncGen'
_REQUEST = b'\x00\x00\x00'


class Greeter(helloworld_pb2_grpc.GreeterServicer):

    async def SayHello(self, request: helloworld_pb2.HelloRequest,
                       context: grpc.aio.ServicerContext
                      ) -> helloworld_pb2.HelloReply:
        return helloworld_pb2.HelloReply(message='Hello, %s!' % request.name)

    async def SayHelloStreaming(self, request_iterator, context):
        async for request in request_iterator:
            print(f"hello {request.name}")
            # business logic
            yield helloworld_pb2.HelloReply(message=f'Hello {request.name}')



async def serve() -> None:
    server = grpc.aio.server()
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    listen_addr = '[::]:50051'
    port = server.add_insecure_port(listen_addr)
    addr = 'localhost:%d' % port
    logging.info(f"Starting server on {listen_addr}, addr {addr}")
    await server.start()
    try:
        await server.wait_for_termination()
    except KeyboardInterrupt:
        # Shuts down the server with 0 seconds of grace period. During the
        # grace period, the server won't accept new connections and allow
        # existing RPCs to continue within the grace period.
        await server.stop(0)


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    asyncio.run(serve())
