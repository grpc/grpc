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

import asyncio
import gc
import logging
import time
import unittest

import grpc
from grpc.experimental import aio

from tests.unit.framework.common import test_constants
from tests_aio.unit._test_base import AioTestBase

_UNARY_UNARY_ABORT = "/test/UnaryUnaryAbort"
_SUPPRESS_ABORT = "/test/SuppressAbort"
_REPLACE_ABORT = "/test/ReplaceAbort"
_ABORT_AFTER_REPLY = "/test/AbortAfterReply"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"
_NUM_STREAM_RESPONSES = 5

_ABORT_CODE = grpc.StatusCode.RESOURCE_EXHAUSTED
_ABORT_DETAILS = "Phony error details"


class _GenericHandler(grpc.GenericRpcHandler):
    @staticmethod
    async def _unary_unary_abort(unused_request, context):
        await context.abort(_ABORT_CODE, _ABORT_DETAILS)
        raise RuntimeError("This line should not be executed")

    @staticmethod
    async def _suppress_abort(unused_request, context):
        try:
            await context.abort(_ABORT_CODE, _ABORT_DETAILS)
        except aio.AbortError as e:
            pass
        return _RESPONSE

    @staticmethod
    async def _replace_abort(unused_request, context):
        try:
            await context.abort(_ABORT_CODE, _ABORT_DETAILS)
        except aio.AbortError as e:
            await context.abort(
                grpc.StatusCode.INVALID_ARGUMENT, "Override abort!"
            )

    @staticmethod
    async def _abort_after_reply(unused_request, context):
        yield _RESPONSE
        await context.abort(_ABORT_CODE, _ABORT_DETAILS)
        raise RuntimeError("This line should not be executed")

    def service(self, handler_details):
        if handler_details.method == _UNARY_UNARY_ABORT:
            return grpc.unary_unary_rpc_method_handler(self._unary_unary_abort)
        if handler_details.method == _SUPPRESS_ABORT:
            return grpc.unary_unary_rpc_method_handler(self._suppress_abort)
        if handler_details.method == _REPLACE_ABORT:
            return grpc.unary_unary_rpc_method_handler(self._replace_abort)
        if handler_details.method == _ABORT_AFTER_REPLY:
            return grpc.unary_stream_rpc_method_handler(self._abort_after_reply)


async def _start_test_server():
    server = aio.server()
    port = server.add_insecure_port("[::]:0")
    server.add_generic_rpc_handlers((_GenericHandler(),))
    await server.start()
    return "localhost:%d" % port, server


class TestAbort(AioTestBase):
    async def setUp(self):
        address, self._server = await _start_test_server()
        self._channel = aio.insecure_channel(address)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def test_unary_unary_abort(self):
        method = self._channel.unary_unary(_UNARY_UNARY_ABORT)
        call = method(_REQUEST)

        self.assertEqual(_ABORT_CODE, await call.code())
        self.assertEqual(_ABORT_DETAILS, await call.details())

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call

        rpc_error = exception_context.exception
        self.assertEqual(_ABORT_CODE, rpc_error.code())
        self.assertEqual(_ABORT_DETAILS, rpc_error.details())

    async def test_suppress_abort(self):
        method = self._channel.unary_unary(_SUPPRESS_ABORT)
        call = method(_REQUEST)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call

        rpc_error = exception_context.exception
        self.assertEqual(_ABORT_CODE, rpc_error.code())
        self.assertEqual(_ABORT_DETAILS, rpc_error.details())

    async def test_replace_abort(self):
        method = self._channel.unary_unary(_REPLACE_ABORT)
        call = method(_REQUEST)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call

        rpc_error = exception_context.exception
        self.assertEqual(_ABORT_CODE, rpc_error.code())
        self.assertEqual(_ABORT_DETAILS, rpc_error.details())

    async def test_abort_after_reply(self):
        method = self._channel.unary_stream(_ABORT_AFTER_REPLY)
        call = method(_REQUEST)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call.read()
            await call.read()

        rpc_error = exception_context.exception
        self.assertEqual(_ABORT_CODE, rpc_error.code())
        self.assertEqual(_ABORT_DETAILS, rpc_error.details())

        self.assertEqual(_ABORT_CODE, await call.code())
        self.assertEqual(_ABORT_DETAILS, await call.details())


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
