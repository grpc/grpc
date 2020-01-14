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
"""Tests behavior around the metadata mechanism."""

import asyncio
import logging
import platform
import random
import unittest

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase

_TEST_CLIENT_TO_SERVER = '/test/TestClientToServer'
_TEST_SERVER_TO_CLIENT = '/test/TestServerToClient'
_TEST_TRAILING_METADATA = '/test/TestTrailingMetadata'
_TEST_ECHO_INITIAL_METADATA = '/test/TestEchoInitialMetadata'
_TEST_GENERIC_HANDLER = '/test/TestGenericHandler'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'

_INITIAL_METADATA_FROM_CLIENT_TO_SERVER = (
    ('client-to-server', 'question'),
    ('client-to-server-bin', b'\x07\x07\x07'),
)
_INITIAL_METADATA_FROM_SERVER_TO_CLIENT = (
    ('server-to-client', 'answer'),
    ('server-to-client-bin', b'\x06\x06\x06'),
)
_TRAILING_METADATA = (('a-trailing-metadata', 'stack-trace'),
                      ('a-trailing-metadata-bin', b'\x05\x05\x05'))
_INITIAL_METADATA_FOR_GENERIC_HANDLER = (('a-must-have-key', 'secret'),)

_INVALID_METADATA_TEST_CASES = (
    (
        TypeError,
        ((42, 42),),
    ),
    (
        TypeError,
        (({}, {}),),
    ),
    (
        TypeError,
        (('normal', object()),),
    ),
)


def _seen_metadata(expected, actual):
    metadata_dict = dict(actual)
    for metadatum in expected:
        if metadata_dict.get(metadatum[0]) != metadatum[1]:
            return False
    return True


class _TestGenericHandlerForMethods(grpc.GenericRpcHandler):

    @staticmethod
    async def _test_client_to_server(request, context):
        assert _REQUEST == request
        assert _seen_metadata(_INITIAL_METADATA_FROM_CLIENT_TO_SERVER,
                              context.invocation_metadata())
        return _RESPONSE

    @staticmethod
    async def _test_server_to_client(request, context):
        assert _REQUEST == request
        await context.send_initial_metadata(
            _INITIAL_METADATA_FROM_SERVER_TO_CLIENT)
        return _RESPONSE

    @staticmethod
    async def _test_trailing_metadata(request, context):
        assert _REQUEST == request
        context.set_trailing_metadata(_TRAILING_METADATA)
        return _RESPONSE

    def service(self, handler_details):
        if handler_details.method == _TEST_CLIENT_TO_SERVER:
            return grpc.unary_unary_rpc_method_handler(
                self._test_client_to_server)
        if handler_details.method == _TEST_SERVER_TO_CLIENT:
            return grpc.unary_unary_rpc_method_handler(
                self._test_server_to_client)
        if handler_details.method == _TEST_TRAILING_METADATA:
            return grpc.unary_unary_rpc_method_handler(
                self._test_trailing_metadata)
        return None


class _TestGenericHandlerItself(grpc.GenericRpcHandler):

    @staticmethod
    async def _method(request, unused_context):
        assert _REQUEST == request
        return _RESPONSE

    def service(self, handler_details):
        assert _seen_metadata(_INITIAL_METADATA_FOR_GENERIC_HANDLER,
                              handler_details.invocation_metadata)
        return grpc.unary_unary_rpc_method_handler(self._method)


async def _start_test_server():
    server = aio.server()
    port = server.add_insecure_port('[::]:0')
    server.add_generic_rpc_handlers((
        _TestGenericHandlerForMethods(),
        _TestGenericHandlerItself(),
    ))
    await server.start()
    return 'localhost:%d' % port, server


class TestMetadata(AioTestBase):

    async def setUp(self):
        address, self._server = await _start_test_server()
        self._client = aio.insecure_channel(address)

    async def tearDown(self):
        await self._client.close()
        await self._server.stop(None)

    async def test_from_client_to_server(self):
        multicallable = self._client.unary_unary(_TEST_CLIENT_TO_SERVER)
        call = multicallable(_REQUEST,
                             metadata=_INITIAL_METADATA_FROM_CLIENT_TO_SERVER)
        self.assertEqual(_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_from_server_to_client(self):
        multicallable = self._client.unary_unary(_TEST_SERVER_TO_CLIENT)
        call = multicallable(_REQUEST)
        self.assertEqual(_INITIAL_METADATA_FROM_SERVER_TO_CLIENT, await
                         call.initial_metadata())
        self.assertEqual(_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_trailing_metadata(self):
        multicallable = self._client.unary_unary(_TEST_TRAILING_METADATA)
        call = multicallable(_REQUEST)
        self.assertEqual(_TRAILING_METADATA, await call.trailing_metadata())
        self.assertEqual(_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_invalid_metadata(self):
        multicallable = self._client.unary_unary(_TEST_CLIENT_TO_SERVER)
        for exception_type, metadata in _INVALID_METADATA_TEST_CASES:
            call = multicallable(_REQUEST, metadata=metadata)
            with self.assertRaises(exception_type):
                await call

    async def test_generic_handler(self):
        multicallable = self._client.unary_unary(_TEST_GENERIC_HANDLER)
        call = multicallable(_REQUEST,
                             metadata=_INITIAL_METADATA_FOR_GENERIC_HANDLER)
        self.assertEqual(_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
