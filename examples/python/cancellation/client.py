# Copyright the 2019 gRPC authors.
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

from concurrent import futures
import argparse
import datetime
import logging
import time

import grpc

from examples.python.cancellation import hash_name_pb2
from examples.python.cancellation import hash_name_pb2_grpc

_DESCRIPTION = "A client for finding hashes similar to names."
_LOGGER = logging.getLogger(__name__)

# Interface:
#   Cancel on ctrl+c or an ideal candidate.

def run_unary_client(server_target, name, ideal_distance):
    # TODO(rbellevi): Cancel on ctrl+c
    with grpc.insecure_channel(server_target) as channel:
        stub = hash_name_pb2_grpc.HashFinderStub(channel)
        while True:
            print("Sending request")
            future = stub.Find.future(hash_name_pb2.HashNameRequest(desired_name=name,
                                                                      ideal_hamming_distance=ideal_distance))
            # TODO(rbellevi): Do not leave in a cancellation based on timeout.
            # That's best handled by, well.. timeout.
            try:
                result = future.result(timeout=20.0)
                print("Got response: \n{}".format(result))
            except grpc.FutureTimeoutError:
                print("Cancelling request")
                future.cancel()


def run_streaming_client(target, name, ideal_distance, interesting_distance):
    pass


def main():
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument("name", type=str, help='The desired name.')
    parser.add_argument("--ideal-distance", default=0, nargs='?',
                        type=int, help="The desired Hamming distance.")
    parser.add_argument(
        '--server',
        default='localhost:50051',
        type=str,
        nargs='?',
        help='The host-port pair at which to reach the server.')
    parser.add_argument(
        '--show-inferior',
        default=None,
        type=int,
        nargs='?',
        help='Also show candidates with a Hamming distance less than this value.')

    args = parser.parse_args()
    if args.show_inferior is not None:
        run_streaming_client(args.server, args.name, args.ideal_distance, args.interesting_distance)
    else:
        run_unary_client(args.server, args.name, args.ideal_distance)

if __name__ == "__main__":
    logging.basicConfig()
    main()
