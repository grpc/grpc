# Copyright 2019 The gRPC Authors.
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

import asyncio
import logging
import unittest

import grpc
from grpc.experimental import aio
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase


class TestAioRpcError(unittest.TestCase):
    _TEST_INITIAL_METADATA = ("initial metadata",)
    _TEST_TRAILING_METADATA = ("trailing metadata",)

    def test_attributes(self):
        aio_rpc_error = aio.AioRpcError(self._TEST_INITIAL_METADATA, 0,
                                        "details", self._TEST_TRAILING_METADATA)
        self.assertEqual(aio_rpc_error.initial_metadata(),
                         self._TEST_INITIAL_METADATA)
        self.assertEqual(aio_rpc_error.code(), 0)
        self.assertEqual(aio_rpc_error.details(), "details")
        self.assertEqual(aio_rpc_error.trailing_metadata(),
                         self._TEST_TRAILING_METADATA)

    def test_class_hierarchy(self):
        aio_rpc_error = aio.AioRpcError(self._TEST_INITIAL_METADATA, 0,
                                        "details", self._TEST_TRAILING_METADATA)

        self.assertIsInstance(aio_rpc_error, grpc.RpcError)

    def test_class_attributes(self):
        aio_rpc_error = aio.AioRpcError(self._TEST_INITIAL_METADATA, 0,
                                        "details", self._TEST_TRAILING_METADATA)
        self.assertEqual(aio_rpc_error.__class__.__name__, "AioRpcError")
        self.assertEqual(aio_rpc_error.__class__.__doc__,
                         aio.AioRpcError.__doc__)

    def test_class_singleton(self):
        first_aio_rpc_error = aio.AioRpcError(self._TEST_INITIAL_METADATA, 0,
                                              "details",
                                              self._TEST_TRAILING_METADATA)
        second_aio_rpc_error = aio.AioRpcError(self._TEST_INITIAL_METADATA, 0,
                                               "details",
                                               self._TEST_TRAILING_METADATA)

        self.assertIs(first_aio_rpc_error.__class__,
                      second_aio_rpc_error.__class__)


class TestInsecureChannel(AioTestBase):

    def test_insecure_channel(self):

        async def coro():
            server_target, unused_server = await start_test_server()

            channel = aio.insecure_channel(server_target)
            self.assertIsInstance(channel, aio.Channel)

        self.loop.run_until_complete(coro())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
