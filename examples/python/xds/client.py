# Copyright 2020 The gRPC authors.
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
"""The Python implementation of the GRPC helloworld.Greeter client."""

from __future__ import print_function

import argparse
import logging

import grpc
import grpc.experimental
import helloworld_pb2
import helloworld_pb2_grpc

_DESCRIPTION = "Get a greeting from a server."


def run(server_address, secure):
    if secure:
        fallback_creds = grpc.experimental.insecure_channel_credentials()
        channel_creds = grpc.xds_channel_credentials(fallback_creds)
        channel = grpc.secure_channel(server_address, channel_creds)
    else:
        channel = grpc.insecure_channel(server_address)
    with channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        response = stub.SayHello(helloworld_pb2.HelloRequest(name="you"))
        print("Greeter client received: " + response.message)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument(
        "server", default=None, help="The address of the server."
    )
    parser.add_argument(
        "--xds-creds",
        action="store_true",
        help="If specified, uses xDS credentials to connect to the server.",
    )
    args = parser.parse_args()
    logging.basicConfig()
    run(args.server, args.xds_creds)
