# Copyright 2024 The gRPC Authors.
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
"""Tests using grpc.aio from multiple threads."""

import asyncio
import concurrent.futures
import logging
import unittest

import grpc

from src.proto.grpc.testing import messages_pb2
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_UNARY_CALL_METHOD = "/grpc.testing.TestService/UnaryCall"

_NUM_THREADS = 5
_NUM_CALLS_IN_THREAD = 20


class TestChannel(AioTestBase):
    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_use_aio_from_multiple_threads(self):
        
        async def run_in_thread():
            unhandled_exceptions = []

            def record_exceptions(loop, context) -> None:
                unhandled_exceptions.append(context.get("exception"))

            asyncio.get_running_loop().set_exception_handler(record_exceptions)

            async with grpc.aio.insecure_channel(self._server_target) as channel:
                hi = channel.unary_unary(
                    _UNARY_CALL_METHOD,
                    request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString,
                )
                for _ in range(_NUM_CALLS_IN_THREAD):
                    await hi(messages_pb2.SimpleRequest())
            
            return unhandled_exceptions
        
        with concurrent.futures.ThreadPoolExecutor(max_workers=_NUM_THREADS) as executor:
            futures = []
            for _ in range(_NUM_THREADS):
                futures.append(executor.submit(asyncio.run, run_in_thread()))

            for future in futures:
                unhandled_exceptions = await asyncio.wrap_future(future)
                self.assertFalse(unhandled_exceptions)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
