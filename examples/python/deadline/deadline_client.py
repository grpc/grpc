# Copyright 2018 The gRPC Authors
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
"""Example gRPC client that sets deadline/timeout"""

from __future__ import print_function
import logging
import time

import grpc

import helloworld_pb2
import helloworld_pb2_grpc


def run():
    # NOTE(gRPC Python Team): .close() is possible on a channel and should be
    # used in circumstances in which the with statement does not fit the needs
    # of the code.
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)

        print('Greeter client sends request with 5 second timeout')
        response = stub.SayHello(
            helloworld_pb2.HelloRequest(name='you'), timeout=5)
        print('Greeter client received: ' + response.message)

        print('Greeter client sends request with 1 second timeout')
        try:
            stub.SayHello(helloworld_pb2.HelloRequest(name='you'), timeout=1)
        except grpc.RpcError as rpc_error:
            print('Greeter client caught exception: ', rpc_error)
            print('Status code:', rpc_error.code())
            print('Status details:', rpc_error.details())


if __name__ == '__main__':
    logging.basicConfig()
    run()
