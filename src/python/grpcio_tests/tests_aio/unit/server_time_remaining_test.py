# Copyright 2021 The gRPC Authors
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
"""Test the time_remaining() method of async ServicerContext."""

import asyncio
import logging
import unittest
import datetime

import grpc
from grpc import aio

from tests_aio.unit._common import ADHOC_METHOD, AdhocGenericHandler
from tests_aio.unit._test_base import AioTestBase

_REQUEST = b'\x09\x05'
_REQUEST_TIMEOUT_S = datetime.timedelta(seconds=5).total_seconds()


class TestServerTimeRemaining(AioTestBase):

    async def setUp(self):
        # Create async server
        self._server = aio.server(options=(('grpc.so_reuseport', 0),))
        self._adhoc_handlers = AdhocGenericHandler()
        self._server.add_generic_rpc_handlers((self._adhoc_handlers,))
        port = self._server.add_insecure_port('[::]:0')
        address = 'localhost:%d' % port
        await self._server.start()
        # Create async channel
        self._channel = aio.insecure_channel(address)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def test_servicer_context_time_remaining(self):
        seen_time_remaining = []

        @grpc.unary_unary_rpc_method_handler
        def log_time_remaining(request: bytes,
                               context: grpc.ServicerContext) -> bytes:
            seen_time_remaining.append(context.time_remaining())
            return b""

        # Check if the deadline propagates properly
        self._adhoc_handlers.set_adhoc_handler(log_time_remaining)
        await self._channel.unary_unary(ADHOC_METHOD)(
            _REQUEST, timeout=_REQUEST_TIMEOUT_S)
        self.assertGreater(seen_time_remaining[0], _REQUEST_TIMEOUT_S / 2)
        # Check if there is no timeout, the time_remaining will be None
        self._adhoc_handlers.set_adhoc_handler(log_time_remaining)
        await self._channel.unary_unary(ADHOC_METHOD)(_REQUEST)
        self.assertIsNone(seen_time_remaining[1])


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
