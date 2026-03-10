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
"""Demonstrates RESOURCE_EXHAUSTED by sending a message larger than the default 4MB limit."""

import logging

import grpc
import helloworld_pb2
import helloworld_pb2_grpc

# 5MB string
LARGE_NAME = "a" * (5 * 1024 * 1024)


def run():
    print(
        "Will try to send a 5MB message to a server with default 4MB limit..."
    )
    # Default channel options (MAX_SEND is unlimited, but Server MAX_RECEIVE is 4MB)
    with grpc.insecure_channel("localhost:50051") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        try:
            response = stub.SayHello(
                helloworld_pb2.HelloRequest(name=LARGE_NAME)
            )
            print("Greeter client received: " + response.message)
        except grpc.RpcError as e:
            print(f"RPC failed with code: {e.code()}")
            print(f"Details: {e.details()}")
            if e.code() == grpc.StatusCode.RESOURCE_EXHAUSTED:
                print("Confirmed: Caught expected RESOURCE_EXHAUSTED error!")


if __name__ == "__main__":
    logging.basicConfig()
    run()
