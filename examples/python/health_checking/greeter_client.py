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
"""gRPC Python helloworld.Greeter client with health checking."""

import logging
from time import sleep

import grpc
from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc
import helloworld_pb2
import helloworld_pb2_grpc


def unary_call(stub: helloworld_pb2_grpc.GreeterStub, message: str):
    response = stub.SayHello(
        helloworld_pb2.HelloRequest(name=message), timeout=3
    )
    print(f"Greeter client received: {response.message}")


def health_check_call(stub: health_pb2_grpc.HealthStub):
    request = health_pb2.HealthCheckRequest(service="helloworld.Greeter")
    resp = stub.Check(request)
    if resp.status == health_pb2.HealthCheckResponse.SERVING:
        print("server is serving")
    elif resp.status == health_pb2.HealthCheckResponse.NOT_SERVING:
        print("server stopped serving")


def run():
    with grpc.insecure_channel("localhost:50051") as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        health_stub = health_pb2_grpc.HealthStub(channel)
        # Should succeed
        unary_call(stub, "you")

        # Check health status every 1 second for 30 seconds
        for _ in range(30):
            health_check_call(health_stub)
            sleep(1)


if __name__ == "__main__":
    logging.basicConfig()
    run()
