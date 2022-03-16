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
from typing import Callable, Iterable, Sequence, Tuple, Union
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.unit.framework.common import test_constants
from tests_aio.unit import _common
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import TestServiceServicer
from tests_aio.unit._test_server import start_test_server

_REQUEST = b'\x03\x07'


class TestContextPeer(AioTestBase):

    async def test_peer(self):

        def exercise(
            request: bytes, 
            context: aio.ServicerContext,
            is_sync: bool,
        ):
            method_name = "test_sync" if is_sync else "test_async"
            servicer_class = "_SyncServicerContext" if is_sync else "_ServicerContext"

            self.assertEqual(_REQUEST, request)
            # The peer address could be ipv4 or ipv6
            self.assertIn('ip', context.peer())

            self.assertIn('localhost', context.host().decode())
            self.assertEqual('/%s/UnaryUnary' % method_name, context.method().decode())
            self.assertEqual(0, context.code())
            self.assertSequenceEqual((), context.trailing_metadata())
            self.assertIn(servicer_class, str(type(context)))

        @grpc.unary_unary_rpc_method_handler
        async def check_peer_unary_unary_async(request: bytes,
                                         context: aio.ServicerContext):
            exercise(request, context, False)
            return request

        @grpc.unary_unary_rpc_method_handler
        def check_peer_unary_unary_sync(request: bytes,
                                         context: aio.ServicerContext):
            exercise(request, context, True)
            return request

        # Creates a server
        server = aio.server()
        handler_async = grpc.method_handlers_generic_handler(
            'test_async', {'UnaryUnary': check_peer_unary_unary_async})
        handler_sync = grpc.method_handlers_generic_handler(
            'test_sync', {'UnaryUnary': check_peer_unary_unary_sync})
        server.add_generic_rpc_handlers((handler_async, handler_sync))
        port = server.add_insecure_port('[::]:0')
        await server.start()

        # Creates a channel
        async with aio.insecure_channel('localhost:%d' % port) as channel:
            for method in ("test_async", "test_sync"):
                _test_method = '/%s/UnaryUnary' % method
                response = await channel.unary_unary(_test_method)(_REQUEST)
                self.assertEqual(_REQUEST, response)

        await server.stop(None)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
