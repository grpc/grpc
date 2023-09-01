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
"""An example of cancelling requests in gRPC."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
import logging
import signal
import sys

import grpc

from examples.python.cancellation import hash_name_pb2
from examples.python.cancellation import hash_name_pb2_grpc

_DESCRIPTION = "A client for finding hashes similar to names."
_LOGGER = logging.getLogger(__name__)


def run_unary_client(server_target, name, ideal_distance):
    with grpc.insecure_channel(server_target) as channel:
        stub = hash_name_pb2_grpc.HashFinderStub(channel)
        future = stub.Find.future(
            hash_name_pb2.HashNameRequest(
                desired_name=name, ideal_hamming_distance=ideal_distance
            ),
            wait_for_ready=True,
        )

        def cancel_request(unused_signum, unused_frame):
            future.cancel()
            sys.exit(0)

        signal.signal(signal.SIGINT, cancel_request)
        result = future.result()
        print(result)


def run_streaming_client(
    server_target, name, ideal_distance, interesting_distance
):
    with grpc.insecure_channel(server_target) as channel:
        stub = hash_name_pb2_grpc.HashFinderStub(channel)
        result_generator = stub.FindRange(
            hash_name_pb2.HashNameRequest(
                desired_name=name,
                ideal_hamming_distance=ideal_distance,
                interesting_hamming_distance=interesting_distance,
            ),
            wait_for_ready=True,
        )

        def cancel_request(unused_signum, unused_frame):
            result_generator.cancel()
            sys.exit(0)

        signal.signal(signal.SIGINT, cancel_request)
        for result in result_generator:
            print(result)


def main():
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument("name", type=str, help="The desired name.")
    parser.add_argument(
        "--ideal-distance",
        default=0,
        nargs="?",
        type=int,
        help="The desired Hamming distance.",
    )
    parser.add_argument(
        "--server",
        default="localhost:50051",
        type=str,
        nargs="?",
        help="The host-port pair at which to reach the server.",
    )
    parser.add_argument(
        "--show-inferior",
        default=None,
        type=int,
        nargs="?",
        help=(
            "Also show candidates with a Hamming distance less than this value."
        ),
    )

    args = parser.parse_args()
    if args.show_inferior is not None:
        run_streaming_client(
            args.server, args.name, args.ideal_distance, args.show_inferior
        )
    else:
        run_unary_client(args.server, args.name, args.ideal_distance)


if __name__ == "__main__":
    logging.basicConfig()
    main()
