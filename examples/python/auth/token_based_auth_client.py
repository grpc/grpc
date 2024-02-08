# Copyright 2024 The gRPC Authors
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
"""Client of the Python example of token based authentication mechanism."""

import argparse
import contextlib
import logging

import _credentials
import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

_SERVER_ADDR_TEMPLATE = "localhost:%d"


@contextlib.contextmanager
def create_client_channel(addr):
    # Call credential object will be invoked for every single RPC
    call_credentials = grpc.access_token_call_credentials(
        "example_oauth2_token"
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
    channel = grpc.secure_channel(addr, composite_credentials)
    yield channel


def send_rpc(channel):
    stub = helloworld_pb2_grpc.GreeterStub(channel)
    request = helloworld_pb2.HelloRequest(name="you")
    try:
        response = stub.SayHello(request)
    except grpc.RpcError as rpc_error:
        _LOGGER.error("Received error: %s", rpc_error)
        return rpc_error
    else:
        _LOGGER.info("Received message: %s", response)
        return response


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--port",
        nargs="?",
        type=int,
        default=50051,
        help="the address of server",
    )
    args = parser.parse_args()

    with create_client_channel(_SERVER_ADDR_TEMPLATE % args.port) as channel:
        send_rpc(channel)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()
