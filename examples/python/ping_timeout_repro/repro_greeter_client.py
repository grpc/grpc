# Copyright 2015 gRPC authors.
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

import logging
import sys
import time

import grpc
import helloworld_pb2
import helloworld_pb2_grpc

# helloworld_pb2, helloworld_pb2_grpc = grpc.protos_and_services(
#     "helloworld.proto"
# )

def run():
    # NOTE(gRPC Python Team): .close() is possible on a channel and should be
    # used in circumstances in which the with statement does not fit the needs
    # of the code.
    target = "localhost:50051" #sys.argv[1] if len(sys.argv) else "localhost:50051"
    sleep = 10

    # gRPC 1.68.x: causes FD Shutdown
    options = [
        ("grpc.keepalive_time_ms", 2_000), # default: MAX_INT (disabled)
        ("grpc.http2.ping_timeout_ms", sleep * 1_000), # default: 1 minute
        # these options don't seem to matter with respect to the issue
        ("grpc.keepalive_timeout_ms", 1_500), # default: 20_000
        ("grpc.keepalive_permit_without_calls", 1), # default: 0 (disabled)
    ]

    running = True
    with grpc.insecure_channel(target, options=options) as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        while running:
            try:
                logging.info("Will try to greet world ...")
                response = stub.SayHello(helloworld_pb2.HelloRequest(name="you"))
                logging.info(f"Greeter client received: {response.message}; sleeping {sleep} seconds")
            except Exception as err:
                logging.error(f"gRPC error {err}")
                running = False
            time.sleep(sleep+5)


if __name__ == "__main__":
    logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)
    run()
