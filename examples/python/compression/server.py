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
"""An example of compression on the server side with gRPC."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
from concurrent import futures
import logging
import threading

import grpc

from examples.protos import helloworld_pb2
from examples.protos import helloworld_pb2_grpc

_DESCRIPTION = "A server capable of compression."
_COMPRESSION_OPTIONS = {
    "none": grpc.Compression.NoCompression,
    "deflate": grpc.Compression.Deflate,
    "gzip": grpc.Compression.Gzip,
}
_LOGGER = logging.getLogger(__name__)

_SERVER_HOST = "localhost"


class Greeter(helloworld_pb2_grpc.GreeterServicer):
    def __init__(self, no_compress_every_n):
        super(Greeter, self).__init__()
        self._no_compress_every_n = 0
        self._request_counter = 0
        self._counter_lock = threading.RLock()

    def _should_suppress_compression(self):
        suppress_compression = False
        with self._counter_lock:
            if (
                self._no_compress_every_n
                and self._request_counter % self._no_compress_every_n == 0
            ):
                suppress_compression = True
            self._request_counter += 1
        return suppress_compression

    def SayHello(self, request, context):
        if self._should_suppress_compression():
            context.set_response_compression(grpc.Compression.NoCompression)
        return helloworld_pb2.HelloReply(message="Hello, %s!" % request.name)


def run_server(server_compression, no_compress_every_n, port):
    server = grpc.server(
        futures.ThreadPoolExecutor(),
        compression=server_compression,
        options=(("grpc.so_reuseport", 1),),
    )
    helloworld_pb2_grpc.add_GreeterServicer_to_server(
        Greeter(no_compress_every_n), server
    )
    address = "{}:{}".format(_SERVER_HOST, port)
    server.add_insecure_port(address)
    server.start()
    print("Server listening at '{}'".format(address))
    server.wait_for_termination()


def main():
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument(
        "--server_compression",
        default="none",
        nargs="?",
        choices=_COMPRESSION_OPTIONS.keys(),
        help="The default compression method for the server.",
    )
    parser.add_argument(
        "--no_compress_every_n",
        type=int,
        default=0,
        nargs="?",
        help="If set, every nth reply will be uncompressed.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=50051,
        nargs="?",
        help="The port on which the server will listen.",
    )
    args = parser.parse_args()
    run_server(
        _COMPRESSION_OPTIONS[args.server_compression],
        args.no_compress_every_n,
        args.port,
    )


if __name__ == "__main__":
    logging.basicConfig()
    main()
