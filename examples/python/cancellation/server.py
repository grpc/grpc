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
import contextlib
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
        distance = _get_hamming_distance(candidate[i:i + len(target)], target)
        if min_distance is None or distance < min_distance:
            min_distance = distance
    return min_distance


def _get_hash(secret):
    hasher = hashlib.sha1()
    hasher.update(secret)
    return base64.b64encode(hasher.digest())


class ResourceLimitExceededError(Exception):
    """Signifies the request has exceeded configured limits."""


# TODO(rbellevi): File issue about indefinite blocking for server-side
#   streaming.


def _find_secret_of_length(target,
                           ideal_distance,
                           length,
                           stop_event,
                           maximum_hashes,
                           interesting_hamming_distance=None):
    """Find a candidate with the given length.

    Args:
      target: The search string.
      ideal_distance: The desired Hamming distance.
      length: The length of secret string to search for.
      stop_event: An event indicating whether the RPC should terminate.
      maximum_hashes: The maximum number of hashes to check before stopping.
      interesting_hamming_distance: If specified, strings with a Hamming
        distance from the target below this value will be yielded.

    Yields:
      A stream of tuples of type Tuple[Optional[HashNameResponse], int]. The
        element of the tuple, if specified, signifies an ideal or interesting
        candidate. If this element is None, it signifies that the stream has
        ended because an ideal candidate has been found. The second element is
        the number of hashes computed up this point.

    Raises:
      ResourceLimitExceededError: If the computation exceeds `maximum_hashes`
        iterations.
    """
    digits = [0] * length
    hashes_computed = 0
    while True:
        if stop_event.is_set():
            # Yield a sentinel and stop the generator if the RPC has been
            # cancelled.
            yield None, hashes_computed
            raise StopIteration()
        secret = b''.join(struct.pack('B', i) for i in digits)
        hash = _get_hash(secret)
        distance = _get_substring_hamming_distance(hash, target)
        if interesting_hamming_distance is not None and distance <= interesting_hamming_distance:
            # Surface interesting candidates, but don't stop.
            yield hash_name_pb2.HashNameResponse(
                secret=base64.b64encode(secret),
                hashed_name=hash,
                hamming_distance=distance), hashes_computed
        elif distance <= ideal_distance:
            # Yield the ideal candidate followed by a sentinel to signal the end
            # of the stream.
            yield hash_name_pb2.HashNameResponse(
                secret=base64.b64encode(secret),
                hashed_name=hash,
                hamming_distance=distance), hashes_computed
            yield None, hashes_computed
            raise StopIteration()
        digits[-1] += 1
        i = length - 1
        while digits[i] == _BYTE_MAX + 1:
            digits[i] = 0
            i -= 1
            if i == -1:
                # Terminate the generator since we've run out of strings of
                # `length` bytes.
                raise StopIteration()
            else:
                digits[i] += 1
        hashes_computed += 1
        if hashes_computed == maximum_hashes:
            raise ResourceLimitExceededError()


def _find_secret(target,
                 maximum_distance,
                 stop_event,
                 maximum_hashes,
                 interesting_hamming_distance=None):
    """Find candidate strings.

    Search through the space of all bytestrings, in order of increasing length,
    indefinitely, until a hash with a Hamming distance of `maximum_distance` or
    less has been found.

    Args:
      target: The search string.
      maximum_distance: The desired Hamming distance.
      stop_event: An event indicating whether the RPC should terminate.
      maximum_hashes: The maximum number of hashes to check before stopping.
      interesting_hamming_distance: If specified, strings with a Hamming
        distance from the target below this value will be yielded.

    Yields:
      Instances  of HashNameResponse. The final entry in the stream will be of
        `maximum_distance` Hamming distance or less from the target string,
        while all others will be of less than `interesting_hamming_distance`.

    Raises:
      ResourceLimitExceededError: If the computation exceeds `maximum_hashes`
        iterations.
    """
    length = 1
    total_hashes = 0
    while True:
        last_hashes_computed = 0
        for candidate, hashes_computed in _find_secret_of_length(
                target,
                maximum_distance,
                length,
                stop_event,
                maximum_hashes - total_hashes,
                interesting_hamming_distance=interesting_hamming_distance):
            last_hashes_computed = hashes_computed
            if candidate is not None:
                yield candidate
            else:
                raise StopIteration()
            if stop_event.is_set():
                # Terminate the generator if the RPC has been cancelled.
                raise StopIteration()
        total_hashes += last_hashes_computed
        length += 1


class HashFinder(hash_name_pb2_grpc.HashFinderServicer):

    def __init__(self, maximum_hashes):
        super(HashFinder, self).__init__()
        self._maximum_hashes = maximum_hashes

    def Find(self, request, context):
        stop_event = threading.Event()

        def on_rpc_done():
            _LOGGER.debug("Attempting to regain servicer thread.")
            stop_event.set()

        context.add_callback(on_rpc_done)
        try:
            candidates = list(
                _find_secret(request.desired_name,
                             request.ideal_hamming_distance, stop_event,
                             self._maximum_hashes))
        except ResourceLimitExceededError:
            _LOGGER.info("Cancelling RPC due to exhausted resources.")
            context.cancel()
        _LOGGER.debug("Servicer thread returning.")
        if not candidates:
            return hash_name_pb2.HashNameResponse()
        return candidates[-1]

    def FindRange(self, request, context):
        stop_event = threading.Event()

        def on_rpc_done():
            _LOGGER.debug("Attempting to regain servicer thread.")
            stop_event.set()

        context.add_callback(on_rpc_done)
        secret_generator = _find_secret(
            request.desired_name,
            request.ideal_hamming_distance,
            stop_event,
            self._maximum_hashes,
            interesting_hamming_distance=request.interesting_hamming_distance)
        try:
            for candidate in secret_generator:
                yield candidate
        except ResourceLimitExceededError:
            _LOGGER.info("Cancelling RPC due to exhausted resources.")
            context.cancel()
        _LOGGER.debug("Regained servicer thread.")


@contextlib.contextmanager
def _running_server(port, maximum_hashes):
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=1), maximum_concurrent_rpcs=1)
    hash_name_pb2_grpc.add_HashFinderServicer_to_server(
        HashFinder(maximum_hashes), server)
    address = '{}:{}'.format(_SERVER_HOST, port)
    actual_port = server.add_insecure_port(address)
    server.start()
    print("Server listening at '{}'".format(address))
    try:
        yield actual_port
    except KeyboardInterrupt:
        pass
    finally:
        server.stop(None)


def main():
    parser = argparse.ArgumentParser(description=_DESCRIPTION)
    parser.add_argument(
        '--port',
        type=int,
        default=50051,
        nargs='?',
        help='The port on which the server will listen.')
    parser.add_argument(
        '--maximum-hashes',
        type=int,
        default=10000,
        nargs='?',
        help='The maximum number of hashes to search before cancelling.')
    args = parser.parse_args()
    with _running_server(args.port, args.maximum_hashes):
        while True:
            time.sleep(_ONE_DAY_IN_SECONDS)


if __name__ == "__main__":
    logging.basicConfig()
    main()
