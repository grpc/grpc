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
import logging
import sys
import threading
import unittest

import grpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase

_UNARY_UNARY_ABORT = "/test/UnaryUnaryAbort"
_SYNC_UNARY_UNARY_ABORT = "/test/SyncUnaryUnaryAbort"
_SUPPRESS_ABORT = "/test/SuppressAbort"
_REPLACE_ABORT = "/test/ReplaceAbort"
_ABORT_AFTER_REPLY = "/test/AbortAfterReply"
_STREAM_UNARY_ABORT_AFTER_FIRST_REQ = "/test/StreamUnaryAbortAfterFirstReq"
_STREAM_STREAM_ABORT_AFTER_FIRST_REQ = "/test/StreamStreamAbortAfterFirstReq"
_STREAM_UNARY_REPLY_AFTER_FIRST_REQ = "/test/StreamUnaryReplyAfterFirstReq"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"

_LARGE_REQUEST = b"\x00" * (1024 * 1024)
_MAX_LARGE_WRITES = 64
_ROUNDS = 3
_READER_ROUNDS = 40

_ABORT_CODE = grpc.StatusCode.RESOURCE_EXHAUSTED
_ABORT_DETAILS = "Phony error details"


class _GenericHandler(grpc.GenericRpcHandler):
    @staticmethod
    async def _unary_unary_abort(unused_request, context):
        await context.abort(_ABORT_CODE, _ABORT_DETAILS)
        raise RuntimeError("This line should not be executed")

    @staticmethod
    def _sync_unary_unary_abort(unused_request, context):
        context.abort(_ABORT_CODE, _ABORT_DETAILS)
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

    @staticmethod
    async def _abort_after_first_request(request_iterator, context):
        async for _ in request_iterator:
            break
        await context.abort(_ABORT_CODE, _ABORT_DETAILS)

    @staticmethod
    async def _reply_after_first_request(request_iterator, context):
        async for _ in request_iterator:
            break
        return _RESPONSE

    def service(self, handler_details):
        if handler_details.method == _UNARY_UNARY_ABORT:
            return grpc.unary_unary_rpc_method_handler(self._unary_unary_abort)
        if handler_details.method == _SYNC_UNARY_UNARY_ABORT:
            return grpc.unary_unary_rpc_method_handler(
                self._sync_unary_unary_abort
            )
        if handler_details.method == _SUPPRESS_ABORT:
            return grpc.unary_unary_rpc_method_handler(self._suppress_abort)
        if handler_details.method == _REPLACE_ABORT:
            return grpc.unary_unary_rpc_method_handler(self._replace_abort)
        if handler_details.method == _ABORT_AFTER_REPLY:
            return grpc.unary_stream_rpc_method_handler(self._abort_after_reply)
        if handler_details.method == _STREAM_UNARY_ABORT_AFTER_FIRST_REQ:
            return grpc.stream_unary_rpc_method_handler(
                self._abort_after_first_request
            )
        if handler_details.method == _STREAM_STREAM_ABORT_AFTER_FIRST_REQ:
            return grpc.stream_stream_rpc_method_handler(
                self._abort_after_first_request
            )
        if handler_details.method == _STREAM_UNARY_REPLY_AFTER_FIRST_REQ:
            return grpc.stream_unary_rpc_method_handler(
                self._reply_after_first_request
            )


async def _large_requests():
    for _ in range(_MAX_LARGE_WRITES):
        yield _LARGE_REQUEST


def _record_repr_when_done(call):
    reprs = []
    call.add_done_callback(lambda done_call: reprs.append(repr(done_call)))
    return reprs


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

    async def test_sync_unary_unary_abort(self):
        method = self._channel.unary_unary(_SYNC_UNARY_UNARY_ABORT)
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

    async def test_stream_unary_abort_during_request_iteration(self):
        method = self._channel.stream_unary(_STREAM_UNARY_ABORT_AFTER_FIRST_REQ)
        for _ in range(_ROUNDS):
            call = method(_large_requests())
            reprs_when_done = _record_repr_when_done(call)

            with self.assertRaises(aio.AioRpcError) as exc_ctx:
                await call

            rpc_error = exc_ctx.exception
            self.assertEqual(_ABORT_CODE, rpc_error.code())
            self.assertEqual(_ABORT_DETAILS, rpc_error.details())
            self.assertEqual(_ABORT_CODE, await call.code())
            self.assertEqual([repr(call)], reprs_when_done)

    async def test_stream_stream_abort_during_write(self):
        method = self._channel.stream_stream(
            _STREAM_STREAM_ABORT_AFTER_FIRST_REQ
        )
        for _ in range(_ROUNDS):
            call = method()
            reprs_when_done = _record_repr_when_done(call)

            with self.assertRaises(
                (aio.AioRpcError, asyncio.InvalidStateError)
            ):
                for _ in range(_MAX_LARGE_WRITES):
                    await call.write(_LARGE_REQUEST)

            self.assertEqual(_ABORT_CODE, await call.code())
            self.assertEqual(_ABORT_DETAILS, await call.details())
            self.assertEqual([repr(call)], reprs_when_done)

    async def test_stream_unary_reply_during_request_iteration(self):
        method = self._channel.stream_unary(_STREAM_UNARY_REPLY_AFTER_FIRST_REQ)
        for _ in range(_ROUNDS):
            call = method(_large_requests())
            reprs_when_done = _record_repr_when_done(call)

            self.assertEqual(_RESPONSE, await call)
            self.assertEqual(grpc.StatusCode.OK, await call.code())
            self.assertEqual([repr(call)], reprs_when_done)

    @unittest.skipIf(
        getattr(sys, "_is_gil_enabled", lambda: True)(),
        "The use-after-free needs a free-threaded build",
    )
    async def test_status_read_from_another_thread_during_abort(self):
        """Tests if status of an RPC is published once.

        Replacing the status while another thread reads it (`repr()` and
        `cancelled()` API) is a use-after-free on a free-threaded builds
        (a crash).
        """
        method = self._channel.stream_stream(
            _STREAM_STREAM_ABORT_AFTER_FIRST_REQ
        )
        errors = []
        codes = []
        for _ in range(_READER_ROUNDS):
            call = method()
            stop = threading.Event()

            def read(call=call):
                while not stop.is_set():
                    try:
                        repr(call)
                        call.cancelled()
                    except Exception as exc:  # pylint: disable=broad-except
                        errors.append(exc)
                        return

            reader = threading.Thread(target=read)
            reader.start()

            try:
                with self.assertRaises(
                    (aio.AioRpcError, asyncio.InvalidStateError)
                ):
                    for _ in range(_MAX_LARGE_WRITES):
                        await call.write(_LARGE_REQUEST)
                codes.append(await call.code())
            finally:
                stop.set()
                reader.join()

        self.assertEqual([], errors)
        self.assertEqual([_ABORT_CODE] * _READER_ROUNDS, codes)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
