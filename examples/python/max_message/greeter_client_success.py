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
"""Demonstrates success when server has increased its 4MB limit."""

import logging

import grpc
import helloworld_pb2
import helloworld_pb2_grpc

# 5MB string
LARGE_NAME = "a" * (2 * 1024 * 1024)


def run():
    print(
        "Will try to send a 5MB message to a server with custom 10MB limit..."
    )
    # Default channel options (MAX_SEND is unlimited, but Server MAX_RECEIVE is usually 4MB)
    # Here we assume the server has been started with the 10MB limit.
    with grpc.insecure_channel("localhost:50051") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        try:
            response = stub.SayHello(
                helloworld_pb2.HelloRequest(name=LARGE_NAME)
            )
            print(f"Greeter client received success response from server!")
            # Just print first 50 chars of the message
            print(f"Response message: {response.message[:50]}...")
        except grpc.RpcError as e:
            print(f"RPC failed unexpectedly with code: {e.code()}")
            print(f"Details: {e.details()}")


if __name__ == "__main__":
    logging.basicConfig()
    run()
