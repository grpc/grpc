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
"""Example gRPC client that utilizes connectivity and termination callbacks"""

from __future__ import print_function
import logging

import grpc

from hellostreamingworld_pb2 import HelloRequest
from hellostreamingworld_pb2_grpc import MultiGreeterStub


def connectivity_event_logger(channel_connectivity):
    print('Channel Connectivity Event: %s' % channel_connectivity)


def on_termination():
    print('The RPC call ends!')


def run():
    with grpc.insecure_channel('localhost:50051') as channel:

        # Add callbacks to channel connectivity changes
        # Available connectivity states: https://grpc.io/grpc/python/grpc.html#channel-connectivity
        channel.subscribe(connectivity_event_logger)

        stub = MultiGreeterStub(channel)

        # A normal RPC
        response_iterator = stub.sayHello(
            HelloRequest(name='Alice', num_greetings='5'))
        # The callback will be executed when the RPC ends
        response_iterator.add_callback(on_termination)
        for response in response_iterator:
            print('Received: %s' % response.message)

        # Callbacks can be unsubscribed on the fly
        channel.unsubscribe(connectivity_event_logger)

        # An invalid RPC
        response_iterator = stub.sayHello(
            HelloRequest(name='Bob', num_greetings='-1'))
        response_iterator.add_callback(on_termination)
        try:
            for response in response_iterator:
                print('Received: %s' % response.message)
        except grpc.RpcError as rpc_error:
            print('RPC Error: %s' % rpc_error)


if __name__ == '__main__':
    logging.basicConfig()
    run()
