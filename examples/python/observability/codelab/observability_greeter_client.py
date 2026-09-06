# Copyright 2024 gRPC authors.
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
"""gRPC Python helloworld.Greeter client for observability codelab."""

import logging
import time

import grpc
import helloworld_pb2
import helloworld_pb2_grpc

_SERVER_PORT = "50051"


def run():
    with grpc.insecure_channel(target=f"localhost:{_SERVER_PORT}") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        # Continuously send RPCs.
        while True:
            try:
                response = stub.SayHello(
                    helloworld_pb2.HelloRequest(name="You")
                )
                print(f"Greeter client received: {response.message}")
                time.sleep(1)
            except grpc.RpcError as rpc_error:
                print("Call failed with code: ", rpc_error.code())


if __name__ == "__main__":
    logging.basicConfig()
    run()
