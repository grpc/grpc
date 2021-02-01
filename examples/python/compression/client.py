# Copyright 2019 the gRPC authors.
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
"""An example of compression on the client side with gRPC."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
import logging
import grpc

from examples.protos import helloworld_pb2
from examples.protos import helloworld_pb2_grpc

_DESCRIPTION = 'A client capable of compression.'
_COMPRESSION_OPTIONS = {
    "none": grpc.Compression.NoCompression,
    "deflate": grpc.Compression.Deflate,
    "gzip": grpc.Compression.Gzip,
}

_LOGGER = logging.getLogger(__name__)


def run_client(channel_compression, call_compression, target):
    with grpc.insecure_channel(target,
                               compression=channel_compression) as channel:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        response = stub.SayHello(helloworld_pb2.HelloRequest(name='you'),
                                 compression=call_compression,
                                 wait_for_ready=True)
        print("Response: {}".format(response))


def main():
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument('--channel_compression',
                        default='none',
                        nargs='?',
                        choices=_COMPRESSION_OPTIONS.keys(),
                        help='The compression method to use for the channel.')
    parser.add_argument(
        '--call_compression',
        default='none',
        nargs='?',
        choices=_COMPRESSION_OPTIONS.keys(),
        help='The compression method to use for an individual call.')
    parser.add_argument('--server',
                        default='localhost:50051',
                        type=str,
                        nargs='?',
                        help='The host-port pair at which to reach the server.')
    args = parser.parse_args()
    channel_compression = _COMPRESSION_OPTIONS[args.channel_compression]
    call_compression = _COMPRESSION_OPTIONS[args.call_compression]
    run_client(channel_compression, call_compression, args.server)


if __name__ == "__main__":
    logging.basicConfig()
    main()
