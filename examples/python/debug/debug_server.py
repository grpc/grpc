# Copyright 2019 The gRPC Authors
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
"""The Python example of utilizing Channelz feature."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
from concurrent import futures
import logging
import random

import grpc

helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
    "helloworld.proto"
)

# TODO: Suppress until the macOS segfault fix rolled out
from grpc_channelz.v1 import channelz  # pylint: disable=wrong-import-position

_LOGGER = logging.getLogger(__name__)
_LOGGER.setLevel(logging.INFO)

_RANDOM_FAILURE_RATE = 0.3


class FaultInjectGreeter(helloworld_pb2_grpc.GreeterServicer):
    def __init__(self, failure_rate):
        self._failure_rate = failure_rate

    def SayHello(self, request, context):
        if random.random() < self._failure_rate:
            context.abort(
                grpc.StatusCode.UNAVAILABLE, "Randomly injected failure."
            )
        return helloworld_pb2.HelloReply(message="Hello, %s!" % request.name)


def create_server(addr, failure_rate):
    server = grpc.server(futures.ThreadPoolExecutor())
    helloworld_pb2_grpc.add_GreeterServicer_to_server(
        FaultInjectGreeter(failure_rate), server
    )

    # Add Channelz Servicer to the gRPC server
    channelz.add_channelz_servicer(server)

    server.add_insecure_port(addr)
    return server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--addr",
        nargs=1,
        type=str,
        default="[::]:50051",
        help="the address to listen on",
    )
    parser.add_argument(
        "--failure_rate",
        nargs=1,
        type=float,
        default=0.3,
        help="a float indicates the percentage of failed message injections",
    )
    args = parser.parse_args()

    server = create_server(addr=args.addr, failure_rate=args.failure_rate)
    server.start()
    server.wait_for_termination()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()
