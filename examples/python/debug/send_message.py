# Copyright 2019 The gRPC Authors
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
"""Send multiple greeting messages to the backend."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import logging
import argparse
import grpc
from examples import helloworld_pb2
from examples import helloworld_pb2_grpc


def process(stub, request):
    try:
        response = stub.SayHello(request)
    except grpc.RpcError as rpc_error:
        print('Received error: %s' % rpc_error)
    else:
        print('Received message: %s' % response)


def run(addr, n):
    with grpc.insecure_channel(addr) as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        request = helloworld_pb2.HelloRequest(name='you')
        for _ in range(n):
            process(stub, request)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--addr',
        nargs=1,
        type=str,
        default='[::]:50051',
        help='the address to request')
    parser.add_argument(
        '-n',
        nargs=1,
        type=int,
        default=10,
        help='an integer for number of messages to sent')
    args = parser.parse_args()
    run(addr=args.addr, n=args.n)


if __name__ == '__main__':
    logging.basicConfig()
    main()
