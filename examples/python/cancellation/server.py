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
from collections import deque
import argparse
import base64
import logging
import hashlib
import struct
import time
import threading

import grpc

from examples.python.cancellation import hash_name_pb2
from examples.python.cancellation import hash_name_pb2_grpc


_BYTE_MAX = 255

_LOGGER = logging.getLogger(__name__)
_SERVER_HOST = 'localhost'
_ONE_DAY_IN_SECONDS = 60 * 60 * 24

_DESCRIPTION = "A server for finding hashes similar to names."


def _get_hamming_distance(a, b):
    """Calculates hamming distance between strings of equal length."""
    assert len(a) == len(b), "'{}', '{}'".format(a, b)
    distance = 0
    for char_a, char_b in zip(a, b):
        if char_a.lower() != char_b.lower():
            distance += 1
    return distance


def _get_substring_hamming_distance(candidate, target):
    """Calculates the minimum hamming distance between between the target
        and any substring of the candidate.

    Args:
      candidate: The string whose substrings will be tested.
      target: The target string.

    Returns:
      The minimum Hamming distance between candidate and target.
    """
    assert len(target) <= len(candidate)
    assert len(candidate) != 0
    min_distance = None
    for i in range(len(candidate) - len(target) + 1):
        distance = _get_hamming_distance(candidate[i:i+len(target)], target)
        if min_distance is None or distance < min_distance:
            min_distance = distance
    return min_distance


def _get_hash(secret):
    hasher = hashlib.sha1()
    hasher.update(secret)
    return base64.b64encode(hasher.digest())


def _find_secret_of_length(target, maximum_distance, length, stop_event):
    digits = [0] * length
    while True:
        if stop_event.is_set():
            return hash_name_pb2.HashNameResponse()
        secret = b''.join(struct.pack('B', i) for i in digits)
        hash = _get_hash(secret)
        distance = _get_substring_hamming_distance(hash, target)
        if distance <= maximum_distance:
            return hash_name_pb2.HashNameResponse(secret=base64.b64encode(secret),
                                                  hashed_name=hash,
                                                  hamming_distance=distance)
        digits[-1] += 1
        i = length - 1
        while digits[i] == _BYTE_MAX + 1:
            digits[i] = 0
            i -= 1
            if i == -1:
                return None
            else:
                digits[i] += 1


def _find_secret(target, maximum_distance, stop_event):
    length = 1
    while True:
        print("Checking strings of length {}.".format(length))
        match = _find_secret_of_length(target, maximum_distance, length, stop_event)
        if match is not None:
            return match
        if stop_event.is_set():
            return hash_name_pb2.HashNameResponse()
        length += 1


class HashFinder(hash_name_pb2_grpc.HashFinderServicer):

    def Find(self, request, context):
        stop_event = threading.Event()
        def on_rpc_done():
            stop_event.set()
        context.add_callback(on_rpc_done)
        result = _find_secret(request.desired_name, request.maximum_hamming_distance, stop_event)
        return result


def _run_server(port):
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
    hash_name_pb2_grpc.add_HashFinderServicer_to_server(
            HashFinder(), server)
    address = '{}:{}'.format(_SERVER_HOST, port)
    server.add_insecure_port(address)
    server.start()
    print("Server listening at '{}'".format(address))
    try:
        while True:
            time.sleep(_ONE_DAY_IN_SECONDS)
    except KeyboardInterrupt:
        server.stop(None)


def main():
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument(
        '--port',
        type=int,
        default=50051,
        nargs='?',
        help='The port on which the server will listen.')
    args = parser.parse_args()
    _run_server(args.port)

if __name__ == "__main__":
    logging.basicConfig()
    main()
