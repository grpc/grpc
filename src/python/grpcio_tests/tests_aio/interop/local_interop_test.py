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
"""Conducts interop tests locally."""

import logging
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import test_pb2_grpc
from tests.interop import resources
from tests_aio.interop import methods
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_SERVER_HOST_OVERRIDE = 'foo.test.google.fr'


class InteropTestCaseMixin:
    """Unit test methods.

    This class must be mixed in with unittest.TestCase and a class that defines
    setUp and tearDown methods that manage a stub attribute.
    """
    _stub: test_pb2_grpc.TestServiceStub

    async def test_empty_unary(self):
        await methods.TestCase.EMPTY_UNARY.test_interoperability(
            self._stub, None)

    async def test_large_unary(self):
        await methods.TestCase.LARGE_UNARY.test_interoperability(
            self._stub, None)

    async def test_server_streaming(self):
        await methods.TestCase.SERVER_STREAMING.test_interoperability(
            self._stub, None)

    async def test_client_streaming(self):
        await methods.TestCase.CLIENT_STREAMING.test_interoperability(
            self._stub, None)

    async def test_ping_pong(self):
        await methods.TestCase.PING_PONG.test_interoperability(self._stub, None)

    async def test_cancel_after_begin(self):
        await methods.TestCase.CANCEL_AFTER_BEGIN.test_interoperability(
            self._stub, None)

    async def test_cancel_after_first_response(self):
        await methods.TestCase.CANCEL_AFTER_FIRST_RESPONSE.test_interoperability(
            self._stub, None)

    @unittest.skip('TODO(https://github.com/grpc/grpc/issues/21707)')
    async def test_timeout_on_sleeping_server(self):
        await methods.TestCase.TIMEOUT_ON_SLEEPING_SERVER.test_interoperability(
            self._stub, None)


class InsecureLocalInteropTest(InteropTestCaseMixin, AioTestBase):

    async def setUp(self):
        address, self._server = await start_test_server()
        self._channel = aio.insecure_channel(address)
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
