# Copyright 2020 The gRPC Authors
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
"""A smoke test for memory leaks on short-lived channels without close.

This test doesn't guarantee all resources are cleaned if `Channel.close` is not
explicitly invoked. The recommended way of using Channel object is using `with`
clause, and let context manager automatically close the channel.
"""

import logging
import os
import resource
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor

import grpc

_TEST_METHOD = '/test/Test'
_REQUEST = b'\x23\x33'
_LARGE_NUM_OF_ITERATIONS = 5000

# If MAX_RSS inflated more than this size, the test is failed.
_FAIL_THRESHOLD = 25 * 1024 * 1024  #  25 MiB


def _get_max_rss():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss


def _pretty_print_bytes(x):
    if x > 1024 * 1024 * 1024:
        return "%.2f GiB" % (x / 1024.0 / 1024 / 1024)
    elif x > 1024 * 1024:
        return "%.2f MiB" % (x / 1024.0 / 1024)
    elif x > 1024:
        return "%.2f KiB" % (x / 1024.0)
    else:
        return "%d B" % x


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        if handler_call_details.method == _TEST_METHOD:
            return grpc.unary_unary_rpc_method_handler(lambda x, _: x)


def _start_a_test_server():
    server = grpc.server(ThreadPoolExecutor(max_workers=1),
                         options=(('grpc.so_reuseport', 0),))
    server.add_generic_rpc_handlers((_GenericHandler(),))
    port = server.add_insecure_port('localhost:0')
    server.start()
    return 'localhost:%d' % port, server


def _perform_an_rpc(address):
    channel = grpc.insecure_channel(address)
    multicallable = channel.unary_unary(_TEST_METHOD)
    response = multicallable(_REQUEST)
    assert _REQUEST == response


class TestLeak(unittest.TestCase):

    def test_leak_with_single_shot_rpcs(self):
        address, server = _start_a_test_server()

        # Records memory before experiment.
        before = _get_max_rss()

        # Amplifies the leak.
        for n in range(_LARGE_NUM_OF_ITERATIONS):
            _perform_an_rpc(address)

        # Fails the test if memory leak detected.
        diff = _get_max_rss() - before
        if diff > _FAIL_THRESHOLD:
            self.fail("Max RSS inflated {} > {}".format(
                _pretty_print_bytes(diff),
                _pretty_print_bytes(_FAIL_THRESHOLD)))


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
