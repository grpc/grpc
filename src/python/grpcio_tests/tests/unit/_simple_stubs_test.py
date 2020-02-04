# Copyright 2020 The gRPC authors.
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
"""Tests for Simple Stubs."""

import contextlib
import datetime
import inspect
import unittest
import sys
import time

import logging

import grpc
import test_common

# TODO: Figure out how to get this test to run only for Python 3.
from typing import Callable, Optional

_CACHE_EPOCHS = 8
_CACHE_TRIALS = 6


_UNARY_UNARY = "/test/UnaryUnary"


def _unary_unary_handler(request, context):
    return request


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_unary_unary_handler)
        else:
            raise NotImplementedError()


def _time_invocation(to_time: Callable[[], None]) -> datetime.timedelta:
    start = datetime.datetime.now()
    to_time()
    return datetime.datetime.now() - start


@contextlib.contextmanager
def _server(credentials: Optional[grpc.ServerCredentials]):
    try:
        server = test_common.test_server()
        target = '[::]:0'
        if credentials is None:
            port = server.add_insecure_port(target)
        else:
            port = server.add_secure_port(target, credentials)
        server.add_generic_rpc_handlers((_GenericHandler(),))
        server.start()
        yield server, port
    finally:
        server.stop(None)


@unittest.skipIf(sys.version_info[0] < 3, "Unsupported on Python 2.")
class SimpleStubsTest(unittest.TestCase):

    def assert_cached(self, to_check: Callable[[str], None]) -> None:
        """Asserts that a function caches intermediate data/state.

        To be specific, given a function whose caching behavior is
        deterministic in the value of a supplied string, this function asserts
        that, on average, subsequent invocations of the function for a specific
        string are faster than first invocations with that same string.

        Args:
          to_check: A function returning nothing, that caches values based on
            an arbitrary supplied Text object.
        """
        initial_runs = []
        cached_runs = []
        for epoch in range(_CACHE_EPOCHS):
            runs = []
            text = str(epoch)
            for trial in range(_CACHE_TRIALS):
                runs.append(_time_invocation(lambda: to_check(text)))
            initial_runs.append(runs[0])
            cached_runs.extend(runs[1:])
        average_cold = sum((run for run in initial_runs), datetime.timedelta()) / len(initial_runs)
        average_warm = sum((run for run in cached_runs), datetime.timedelta()) / len(cached_runs)
        self.assertLess(average_warm, average_cold)

    def test_unary_unary_insecure(self):
        with _server(None) as (_, port):
            target = f'localhost:{port}'
            request = b'0000'
            response = grpc.unary_unary(request, target, _UNARY_UNARY)
            self.assertEqual(request, response)

    def test_unary_unary_secure(self):
        with _server(grpc.local_server_credentials()) as (_, port):
            target = f'localhost:{port}'
            request = b'0000'
            response = grpc.unary_unary(request,
                                        target,
                                        _UNARY_UNARY,
                                        channel_credentials=grpc.local_channel_credentials())
            self.assertEqual(request, response)

    def test_channels_cached(self):
        with _server(grpc.local_server_credentials()) as (_, port):
            target = f'localhost:{port}'
            request = b'0000'
            test_name = inspect.stack()[0][3]
            args = (request, target, _UNARY_UNARY)
            kwargs = {"channel_credentials": grpc.local_channel_credentials()}
            def _invoke(seed: Text):
                run_kwargs = dict(kwargs)
                run_kwargs["options"] = ((test_name + seed, ""),)
                grpc.unary_unary(*args, **run_kwargs)
            self.assert_cached(_invoke)

    # TODO: Test request_serializer
    # TODO: Test request_deserializer
    # TODO: Test channel_credentials
    # TODO: Test call_credentials
    # TODO: Test compression
    # TODO: Test wait_for_ready
    # TODO: Test metadata

if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)


