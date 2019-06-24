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
import base64
import logging
import hashlib
import struct
import time

import grpc

from examples.python.cancellation import hash_name_pb2
from examples.python.cancellation import hash_name_pb2_grpc


_LOGGER = logging.getLogger(__name__)
_SERVER_HOST = 'localhost'
_ONE_DAY_IN_SECONDS = 60 * 60 * 24


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
    hasher = hashlib.sha256()
    hasher.update(secret)
    return base64.b64encode(hasher.digest())


class HashFinder(hash_name_pb2_grpc.HashFinderServicer):

    # TODO(rbellevi): Make this use less memory.
    def Find(self, request, context):
        to_check = deque((i,) for i in range(256))
        count = 0
        while True:
            if count % 1000 == 0:
                logging.info("Checked {} hashes.".format(count))
            current = to_check.popleft()
            for i in range(256):
                to_check.append(current + (i,))
            secret = b''.join(struct.pack('B', i) for i in current)
            hash = _get_hash(secret)
            distance = _get_substring_hamming_distance(hash, request.desired_name)
            if distance <= request.maximum_hamming_distance:
                return hash_name_pb2.HashNameResponse(secret=base64.b64encode(secret),
                                                      hashed_name=hash,
                                                      hamming_distance=distance)
            count += 1



def main():
    port = 50051
    server = grpc.server(futures.ThreadPoolExecutor())
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
    pass

if __name__ == "__main__":
    logging.basicConfig()
    main()
