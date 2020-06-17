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
"""Tests behavior around the Core channel arguments."""

import asyncio
import logging
import platform
import random
import errno
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests.unit.framework import common
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_RANDOM_SEED = 42

_ENABLE_REUSE_PORT = 'SO_REUSEPORT enabled'
_DISABLE_REUSE_PORT = 'SO_REUSEPORT disabled'
_SOCKET_OPT_SO_REUSEPORT = 'grpc.so_reuseport'
_OPTIONS = (
    (_ENABLE_REUSE_PORT, ((_SOCKET_OPT_SO_REUSEPORT, 1),)),
    (_DISABLE_REUSE_PORT, ((_SOCKET_OPT_SO_REUSEPORT, 0),)),
)

_NUM_SERVER_CREATED = 5

_GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH = 'grpc.max_receive_message_length'
_MAX_MESSAGE_LENGTH = 1024

_ADDRESS_TOKEN_ERRNO = errno.EADDRINUSE, errno.ENOSR


class _TestPointerWrapper(object):

    def __int__(self):
        return 123456


_TEST_CHANNEL_ARGS = (
    ('arg1', b'bytes_val'),
    ('arg2', 'str_val'),
    ('arg3', 1),
    (b'arg4', 'str_val'),
    ('arg6', _TestPointerWrapper()),
)

_INVALID_TEST_CHANNEL_ARGS = [
    {
        'foo': 'bar'
    },
    (('key',),),
    'str',
]


async def test_if_reuse_port_enabled(server: aio.Server):
    port = server.add_insecure_port('localhost:0')
    await server.start()

    try:
        with common.bound_socket(
                bind_address='localhost',
                port=port,
                listen=False,
        ) as (unused_host, bound_port):
            assert bound_port == port
    except OSError as e:
        if e.errno in _ADDRESS_TOKEN_ERRNO:
            return False
        else:
            logging.exception(e)
            raise
    else:
        return True


class TestChannelArgument(AioTestBase):

    async def setUp(self):
        random.seed(_RANDOM_SEED)

    @unittest.skipIf(platform.system() == 'Windows',
                     'SO_REUSEPORT only available in Linux-like OS.')
    async def test_server_so_reuse_port_is_set_properly(self):

        async def test_body():
            fact, options = random.choice(_OPTIONS)
            server = aio.server(options=options)
            try:
                result = await test_if_reuse_port_enabled(server)
                if fact == _ENABLE_REUSE_PORT and not result:
                    self.fail(
                        'Enabled reuse port in options, but not observed in socket'
                    )
                elif fact == _DISABLE_REUSE_PORT and result:
                    self.fail(
                        'Disabled reuse port in options, but observed in socket'
                    )
            finally:
                await server.stop(None)

        # Creating a lot of servers concurrently
        await asyncio.gather(*(test_body() for _ in range(_NUM_SERVER_CREATED)))

    async def test_client(self):
        # Do not segfault, or raise exception!
        channel = aio.insecure_channel('[::]:0', options=_TEST_CHANNEL_ARGS)
        await channel.close()

    async def test_server(self):
        # Do not segfault, or raise exception!
        server = aio.server(options=_TEST_CHANNEL_ARGS)
        await server.stop(None)

    async def test_invalid_client_args(self):
        for invalid_arg in _INVALID_TEST_CHANNEL_ARGS:
            self.assertRaises((ValueError, TypeError),
                              aio.insecure_channel,
                              '[::]:0',
                              options=invalid_arg)

    async def test_max_message_length_applied(self):
        address, server = await start_test_server()

        async with aio.insecure_channel(
                address,
                options=((_GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
                          _MAX_MESSAGE_LENGTH),)) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)

            request = messages_pb2.StreamingOutputCallRequest()
            # First request will pass
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_MAX_MESSAGE_LENGTH // 2,))
            # Second request should fail
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_MAX_MESSAGE_LENGTH * 2,))

            call = stub.StreamingOutputCall(request)

            response = await call.read()
            self.assertEqual(_MAX_MESSAGE_LENGTH // 2,
                             len(response.payload.body))

            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call.read()
            rpc_error = exception_context.exception
            self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED,
                             rpc_error.code())
            self.assertIn(str(_MAX_MESSAGE_LENGTH), rpc_error.details())

            self.assertEqual(grpc.StatusCode.RESOURCE_EXHAUSTED, await
                             call.code())

        await server.stop(None)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
