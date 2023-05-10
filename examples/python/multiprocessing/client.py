# Copyright 2019 gRPC authors.
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
"""An example of multiprocessing concurrency with gRPC."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import argparse
import atexit
import logging
import multiprocessing
import operator
import sys

import grpc
import prime_pb2
import prime_pb2_grpc

_PROCESS_COUNT = 8
_MAXIMUM_CANDIDATE = 10000

# Each worker process initializes a single channel after forking.
# It's regrettable, but to ensure that each subprocess only has to instantiate
# a single channel to be reused across all RPCs, we use globals.
_worker_channel_singleton = None
_worker_stub_singleton = None

_LOGGER = logging.getLogger(__name__)


def _shutdown_worker():
    _LOGGER.info("Shutting worker process down.")
    if _worker_channel_singleton is not None:
        _worker_channel_singleton.close()


def _initialize_worker(server_address):
    global _worker_channel_singleton  # pylint: disable=global-statement
    global _worker_stub_singleton  # pylint: disable=global-statement
    _LOGGER.info('Initializing worker process.')
    _worker_channel_singleton = grpc.insecure_channel(server_address)
    _worker_stub_singleton = prime_pb2_grpc.PrimeCheckerStub(
        _worker_channel_singleton)
    atexit.register(_shutdown_worker)


def _run_worker_query(primality_candidate):
    _LOGGER.info('Checking primality of %s.', primality_candidate)
    return _worker_stub_singleton.check(
        prime_pb2.PrimeCandidate(candidate=primality_candidate))


def _calculate_primes(server_address):
    worker_pool = multiprocessing.Pool(processes=_PROCESS_COUNT,
                                       initializer=_initialize_worker,
                                       initargs=(server_address,))
    check_range = range(2, _MAXIMUM_CANDIDATE)
    primality = worker_pool.map(_run_worker_query, check_range)
    primes = zip(check_range, map(operator.attrgetter('isPrime'), primality))
    return tuple(primes)


def main():
    msg = 'Determine the primality of the first {} integers.'.format(
        _MAXIMUM_CANDIDATE)
    parser = argparse.ArgumentParser(description=msg)
    parser.add_argument('server_address',
                        help='The address of the server (e.g. localhost:50051)')
    args = parser.parse_args()
    primes = _calculate_primes(args.server_address)
    print(primes)


if __name__ == '__main__':
    handler = logging.StreamHandler(sys.stdout)
    formatter = logging.Formatter('[PID %(process)d] %(message)s')
    handler.setFormatter(formatter)
    _LOGGER.addHandler(handler)
    _LOGGER.setLevel(logging.INFO)
    main()
