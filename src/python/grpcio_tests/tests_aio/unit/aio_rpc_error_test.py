# Copyright 2019 The gRPC Authors
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
"""Tests AioRpcError class."""

import logging
import unittest

import grpc

from grpc.experimental import aio
from grpc.aio._call import AioRpcError
from tests_aio.unit._test_base import AioTestBase

_TEST_INITIAL_METADATA = aio.Metadata(
    ('initial metadata key', 'initial metadata value'))
_TEST_TRAILING_METADATA = aio.Metadata(
    ('trailing metadata key', 'trailing metadata value'))
_TEST_DEBUG_ERROR_STRING = '{This is a debug string}'


class TestAioRpcError(unittest.TestCase):

    def test_attributes(self):
        aio_rpc_error = AioRpcError(grpc.StatusCode.CANCELLED,
                                    initial_metadata=_TEST_INITIAL_METADATA,
                                    trailing_metadata=_TEST_TRAILING_METADATA,
                                    details="details",
                                    debug_error_string=_TEST_DEBUG_ERROR_STRING)
        self.assertEqual(aio_rpc_error.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(aio_rpc_error.details(), 'details')
        self.assertEqual(aio_rpc_error.initial_metadata(),
                         _TEST_INITIAL_METADATA)
        self.assertEqual(aio_rpc_error.trailing_metadata(),
                         _TEST_TRAILING_METADATA)
        self.assertEqual(aio_rpc_error.debug_error_string(),
                         _TEST_DEBUG_ERROR_STRING)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
