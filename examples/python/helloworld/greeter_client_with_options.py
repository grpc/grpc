# Copyright 2018 gRPC authors.
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

import grpc

import helloworld_pb2
import helloworld_pb2_grpc


def run():
    # For more channel options, please see https://grpc.io/grpc/core/group__grpc__arg__keys.html
    channel = grpc.insecure_channel(
        target='localhost:50051',
        options=[('grpc.lb_policy_name', 'pick_first'),
                 ('grpc.enable_retries', 0),
                 ('grpc.keepalive_timeout_ms', 10),
                 ('grpc.max_receive_message_length', 12)])
    stub = helloworld_pb2_grpc.GreeterStub(channel)

    try:
        # synchronous rpc call
        stub.SayHello(helloworld_pb2.HelloRequest(name='you'))
    except Exception as err:
        print('Raised by max_receive_message_length option\n' + str(err))

    try:
        # asynchronous rpc call. timeout in second
        future = stub.SayHello.future(helloworld_pb2.HelloRequest(name='me'), timeout=1)
        response = future.result()
        print("Greeter client received: " + response.message)
    finally:
        channel.close()


if __name__ == '__main__':
    run()
