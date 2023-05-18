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
"""The Python AsyncIO implementation of the GRPC helloworld.Greeter server."""

import asyncio
import contextvars
import logging
from typing import Awaitable, Callable, Optional

import grpc
import helloworld_pb2
import helloworld_pb2_grpc

rpc_id_var = contextvars.ContextVar('rpc_id', default='default')


class RPCIdInterceptor(grpc.aio.ServerInterceptor):

    def __init__(self, tag: str, rpc_id: Optional[str] = None) -> None:
        self.tag = tag
        self.rpc_id = rpc_id

    async def intercept_service(
            self, continuation: Callable[[grpc.HandlerCallDetails],
                                         Awaitable[grpc.RpcMethodHandler]],
            handler_call_details: grpc.HandlerCallDetails
    ) -> grpc.RpcMethodHandler:
        """
        This interceptor prepends its tag to the rpc_id.
        If two of these interceptors are chained together, the resulting rpc_id
        will be something like this: Interceptor2-Interceptor1-RPC_ID.
        """
        logging.info("%s called with rpc_id: %s", self.tag, rpc_id_var.get())
        if rpc_id_var.get() == 'default':
            _metadata = dict(handler_call_details.invocation_metadata)
            rpc_id_var.set(self.decorate(_metadata['client-rpc-id']))
        else:
            rpc_id_var.set(self.decorate(rpc_id_var.get()))
        return await continuation(handler_call_details)

    def decorate(self, rpc_id: str):
        return f"{self.tag}-{rpc_id}"


class Greeter(helloworld_pb2_grpc.GreeterServicer):

    async def SayHello(
            self, request: helloworld_pb2.HelloRequest,
            context: grpc.aio.ServicerContext) -> helloworld_pb2.HelloReply:
        logging.info("Handle rpc with id %s in server handler.",
                     rpc_id_var.get())
        return helloworld_pb2.HelloReply(message='Hello, %s!' % request.name)


async def serve() -> None:
    interceptors = [
        RPCIdInterceptor('Interceptor1'),
        RPCIdInterceptor('Interceptor2')
    ]

    server = grpc.aio.server(interceptors=interceptors)
    helloworld_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    listen_addr = '[::]:50051'
    server.add_insecure_port(listen_addr)
    logging.info("Starting server on %s", listen_addr)
    await server.start()
    await server.wait_for_termination()


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    asyncio.run(serve())
