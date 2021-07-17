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
"""Server of the Python AsyncIO example of customizing authentication mechanism."""

import argparse
import asyncio
import logging
from typing import Awaitable, Callable, Tuple

import grpc

import _credentials

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto")

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

_LISTEN_ADDRESS_TEMPLATE = 'localhost:%d'
_SIGNATURE_HEADER_KEY = 'x-signature'


class SignatureValidationInterceptor(grpc.aio.ServerInterceptor):

    def __init__(self):

        def abort(ignored_request, context: grpc.aio.ServicerContext) -> None:
            context.abort(grpc.StatusCode.UNAUTHENTICATED, 'Invalid signature')

        self._abort_handler = grpc.unary_unary_rpc_method_handler(abort)

    async def intercept_service(
            self, continuation: Callable[[grpc.HandlerCallDetails],
                                         Awaitable[grpc.RpcMethodHandler]],
            handler_call_details: grpc.HandlerCallDetails
    ) -> grpc.RpcMethodHandler:
        # Example HandlerCallDetails object:
        #     _HandlerCallDetails(
        #       method=u'/helloworld.Greeter/SayHello',
        #       invocation_metadata=...)
        method_name = handler_call_details.method.split('/')[-1]
        expected_metadata = (_SIGNATURE_HEADER_KEY, method_name[::-1])
        if expected_metadata in handler_call_details.invocation_metadata:
            return await continuation(handler_call_details)
        else:
            return self._abort_handler


class SimpleGreeter(helloworld_pb2_grpc.GreeterServicer):

    async def SayHello(self, request: helloworld_pb2.HelloRequest,
                       unused_context) -> helloworld_pb2.HelloReply:
        return helloworld_pb2.HelloReply(message='Hello, %s!' % request.name)


async def run_server(port: int) -> Tuple[grpc.aio.Server, int]:
    # Bind interceptor to server
    server = grpc.aio.server(interceptors=(SignatureValidationInterceptor(),))
    helloworld_pb2_grpc.add_GreeterServicer_to_server(SimpleGreeter(), server)

    # Loading credentials
    server_credentials = grpc.ssl_server_credentials(((
        _credentials.SERVER_CERTIFICATE_KEY,
        _credentials.SERVER_CERTIFICATE,
    ),))

    # Pass down credentials
    port = server.add_secure_port(_LISTEN_ADDRESS_TEMPLATE % port,
                                  server_credentials)

    await server.start()
    return server, port


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--port',
                        nargs='?',
                        type=int,
                        default=50051,
                        help='the listening port')
    args = parser.parse_args()

    server, port = await run_server(args.port)
    logging.info('Server is listening at port :%d', port)
    await server.wait_for_termination()


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    asyncio.run(main())
