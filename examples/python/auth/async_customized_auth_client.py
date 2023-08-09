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
"""Client of the Python AsyncIO example of customizing authentication mechanism."""

import argparse
import asyncio
import logging

import _credentials
import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

_SERVER_ADDR_TEMPLATE = "localhost:%d"
_SIGNATURE_HEADER_KEY = "x-signature"


class AuthGateway(grpc.AuthMetadataPlugin):
    def __call__(
        self,
        context: grpc.AuthMetadataContext,
        callback: grpc.AuthMetadataPluginCallback,
    ) -> None:
        """Implements authentication by passing metadata to a callback.

        Implementations of this method must not block.

        Args:
          context: An AuthMetadataContext providing information on the RPC that
            the plugin is being called to authenticate.
          callback: An AuthMetadataPluginCallback to be invoked either
            synchronously or asynchronously.
        """
        # Example AuthMetadataContext object:
        # AuthMetadataContext(
        #     service_url=u'https://localhost:50051/helloworld.Greeter',
        #     method_name=u'SayHello')
        signature = context.method_name[::-1]
        callback(((_SIGNATURE_HEADER_KEY, signature),), None)


def create_client_channel(addr: str) -> grpc.aio.Channel:
    # Call credential object will be invoked for every single RPC
    call_credentials = grpc.metadata_call_credentials(
        AuthGateway(), name="auth gateway"
    )
    # Channel credential will be valid for the entire channel
    channel_credential = grpc.ssl_channel_credentials(
        _credentials.ROOT_CERTIFICATE
    )
    # Combining channel credentials and call credentials together
    composite_credentials = grpc.composite_channel_credentials(
        channel_credential,
        call_credentials,
    )
    channel = grpc.aio.secure_channel(addr, composite_credentials)
    return channel


async def send_rpc(channel: grpc.aio.Channel) -> helloworld_pb2.HelloReply:
    stub = helloworld_pb2_grpc.GreeterStub(channel)
    request = helloworld_pb2.HelloRequest(name="you")
    try:
        response = await stub.SayHello(request)
    except grpc.RpcError as rpc_error:
        _LOGGER.error("Received error: %s", rpc_error)
        return rpc_error
    else:
        _LOGGER.info("Received message: %s", response)
        return response


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--port",
        nargs="?",
        type=int,
        default=50051,
        help="the address of server",
    )
    args = parser.parse_args()

    channel = create_client_channel(_SERVER_ADDR_TEMPLATE % args.port)
    await send_rpc(channel)
    await channel.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    asyncio.run(main())
