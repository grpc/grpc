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
"""Poll statistics from the server."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import logging
import argparse
import grpc

from grpc_channelz.v1 import channelz_pb2
from grpc_channelz.v1 import channelz_pb2_grpc


def run(addr):
    with grpc.insecure_channel(addr) as channel:
        channelz_stub = channelz_pb2_grpc.ChannelzStub(channel)
        # This RPC pulls server-level metrics, like sent/received messages,
        # succeeded/failed RPCs. For more info see:
        # https://github.com/grpc/grpc/blob/master/src/proto/grpc/channelz/channelz.proto
        response = channelz_stub.GetServers(channelz_pb2.GetServersRequest())
        print(f'Info for all servers: {response}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--addr',
                        nargs=1,
                        type=str,
                        default='[::]:50051',
                        help='the address to request')
    args = parser.parse_args()
    run(addr=args.addr)


if __name__ == '__main__':
    logging.basicConfig()
    main()
