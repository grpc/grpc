# Copyright 2017 gRPC authors.
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
"""The Python implementation of the gRPC helloworld.Greeter client."""

from __future__ import print_function

import grpc

import helloworld_pb2
import helloworld_pb2_grpc
import default_value_client_interceptor


def run():
    default_value = helloworld_pb2.HelloReply(
        message='Hello from your local interceptor!')
    default_value_interceptor = default_value_client_interceptor.DefaultValueClientInterceptor(
        default_value)
    channel = grpc.insecure_channel('localhost:50051')
    channel = grpc.intercept_channel(channel, default_value_interceptor)
    stub = helloworld_pb2_grpc.GreeterStub(channel)
    response = stub.SayHello(helloworld_pb2.HelloRequest(name='you'))
    print("Greeter client received: " + response.message)


if __name__ == '__main__':
    run()
