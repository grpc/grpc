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
"""Testing the server context ability to access peer info."""

import asyncio
import logging
import os
import unittest
from typing import Callable, Iterable, Sequence, Tuple

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests.unit.framework.common import test_constants
from tests_aio.unit import _common
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import TestServiceServicer, start_test_server

_REQUEST = b'\x03\x07'
_TEST_METHOD = '/test/UnaryUnary'


class TestContextPeer(AioTestBase):

    async def test_peer(self):

        @grpc.unary_unary_rpc_method_handler
        async def check_peer_unary_unary(request: bytes,
                                         context: aio.ServicerContext):
            self.assertEqual(_REQUEST, request)
            # The peer address could be ipv4 or ipv6
            self.assertIn('ip', context.peer())
            return request

        # Creates a server
        server = aio.server()
        handlers = grpc.method_handlers_generic_handler(
            'test', {'UnaryUnary': check_peer_unary_unary})
        server.add_generic_rpc_handlers((handlers,))
        port = server.add_insecure_port('[::]:0')
        await server.start()

        # Creates a channel
        async with aio.insecure_channel('localhost:%d' % port) as channel:
            response = await channel.unary_unary(_TEST_METHOD)(_REQUEST)
            self.assertEqual(_REQUEST, response)

        await server.stop(None)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
