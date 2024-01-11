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
"""The Python implementation of the GRPC helloworld.Greeter client."""

import logging

import grpc
import helloworld_pb2
import helloworld_pb2_grpc


def run():
    print("Will try to greet world ...")
    options = (("grpc.lb_policy_name", "round_robin"),)
    # Load balancing takes effect when the DNS server returns multiple IPs for the DNS hostname.
    # Replace "localhost" with such hostname to see the round robin LB policy take effect.
    with grpc.insecure_channel("localhost:50051", options=options) as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        response = stub.SayHello(helloworld_pb2.HelloRequest(name="you"))
    print("Greeter client received: " + response.message)


if __name__ == "__main__":
    logging.basicConfig()
    run()
