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
"""Tests behavior of the timeout mechanism on client side."""

import asyncio
import logging
import platform
import random
import unittest
import datetime

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit import _common

_SLEEP_TIME_UNIT_S = datetime.timedelta(seconds=1).total_seconds()

_TEST_SLEEPY_UNARY_UNARY = '/test/Test/SleepyUnaryUnary'
_TEST_SLEEPY_UNARY_STREAM = '/test/Test/SleepyUnaryStream'
_TEST_SLEEPY_STREAM_UNARY = '/test/Test/SleepyStreamUnary'
_TEST_SLEEPY_STREAM_STREAM = '/test/Test/SleepyStreamStream'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'


async def _test_sleepy_unary_unary(unused_request, unused_context):
    await asyncio.sleep(_SLEEP_TIME_UNIT_S)
    return _RESPONSE


async def _test_sleepy_unary_stream(unused_request, unused_context):
    yield _RESPONSE
    await asyncio.sleep(_SLEEP_TIME_UNIT_S)
    yield _RESPONSE


async def _test_sleepy_stream_unary(unused_request_iterator, context):
    assert _REQUEST == await context.read()
    await asyncio.sleep(_SLEEP_TIME_UNIT_S)
    assert _REQUEST == await context.read()
    return _RESPONSE


async def _test_sleepy_stream_stream(unused_request_iterator, context):
    assert _REQUEST == await context.read()
    await asyncio.sleep(_SLEEP_TIME_UNIT_S)
    await context.write(_RESPONSE)


_ROUTING_TABLE = {
    _TEST_SLEEPY_UNARY_UNARY:
        grpc.unary_unary_rpc_method_handler(_test_sleepy_unary_unary),
    _TEST_SLEEPY_UNARY_STREAM:
        grpc.unary_stream_rpc_method_handler(_test_sleepy_unary_stream),
    _TEST_SLEEPY_STREAM_UNARY:
        grpc.stream_unary_rpc_method_handler(_test_sleepy_stream_unary),
    _TEST_SLEEPY_STREAM_STREAM:
        grpc.stream_stream_rpc_method_handler(_test_sleepy_stream_stream)
}


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        return _ROUTING_TABLE.get(handler_call_details.method)


async def _start_test_server():
    server = aio.server()
    port = server.add_insecure_port('[::]:0')
    server.add_generic_rpc_handlers((_GenericHandler(),))
    await server.start()
    return f'localhost:{port}', server


class TestTimeout(AioTestBase):

    async def setUp(self):
        address, self._server = await _start_test_server()
        self._client = aio.insecure_channel(address)
        self.assertEqual(grpc.ChannelConnectivity.IDLE,
                         self._client.get_state(True))
        await _common.block_until_certain_state(self._client,
                                                grpc.ChannelConnectivity.READY)

    async def tearDown(self):
        await self._client.close()
        await self._server.stop(None)

    async def test_unary_unary_success_with_timeout(self):
        multicallable = self._client.unary_unary(_TEST_SLEEPY_UNARY_UNARY)
        call = multicallable(_REQUEST, timeout=2 * _SLEEP_TIME_UNIT_S)
        self.assertEqual(_RESPONSE, await call)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_unary_unary_deadline_exceeded(self):
        multicallable = self._client.unary_unary(_TEST_SLEEPY_UNARY_UNARY)
        call = multicallable(_REQUEST, timeout=0.5 * _SLEEP_TIME_UNIT_S)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call

        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, rpc_error.code())

    async def test_unary_stream_success_with_timeout(self):
        multicallable = self._client.unary_stream(_TEST_SLEEPY_UNARY_STREAM)
        call = multicallable(_REQUEST, timeout=2 * _SLEEP_TIME_UNIT_S)
        self.assertEqual(_RESPONSE, await call.read())
        self.assertEqual(_RESPONSE, await call.read())
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_unary_stream_deadline_exceeded(self):
        multicallable = self._client.unary_stream(_TEST_SLEEPY_UNARY_STREAM)
        call = multicallable(_REQUEST, timeout=0.5 * _SLEEP_TIME_UNIT_S)
        self.assertEqual(_RESPONSE, await call.read())

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.read()

        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, rpc_error.code())

    async def test_stream_unary_success_with_timeout(self):
        multicallable = self._client.stream_unary(_TEST_SLEEPY_STREAM_UNARY)
        call = multicallable(timeout=2 * _SLEEP_TIME_UNIT_S)
        await call.write(_REQUEST)
        await call.write(_REQUEST)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_stream_unary_deadline_exceeded(self):
        multicallable = self._client.stream_unary(_TEST_SLEEPY_STREAM_UNARY)
        call = multicallable(timeout=0.5 * _SLEEP_TIME_UNIT_S)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.write(_REQUEST)
            await call.write(_REQUEST)
            await call

        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, rpc_error.code())

    async def test_stream_stream_success_with_timeout(self):
        multicallable = self._client.stream_stream(_TEST_SLEEPY_STREAM_STREAM)
        call = multicallable(timeout=2 * _SLEEP_TIME_UNIT_S)
        await call.write(_REQUEST)
        self.assertEqual(_RESPONSE, await call.read())
        self.assertEqual(grpc.StatusCode.OK, await call.code())

    async def test_stream_stream_deadline_exceeded(self):
        multicallable = self._client.stream_stream(_TEST_SLEEPY_STREAM_STREAM)
        call = multicallable(timeout=0.5 * _SLEEP_TIME_UNIT_S)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.write(_REQUEST)
            await call.read()

        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, rpc_error.code())


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
