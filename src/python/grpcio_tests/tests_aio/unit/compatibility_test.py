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
"""Testing the compatibility between AsyncIO stack and the old stack."""

import asyncio
import concurrent.futures
import logging
import os
import random
import threading
from typing import Callable, Iterable, Sequence, Tuple
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

_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 42
_REQUEST = b"\x03\x07"
_INITIAL_METADATA = (("unique", "key-42"),)


def _unique_options() -> Sequence[Tuple[str, float]]:
    return (("iv", random.random()),)


@unittest.skipIf(
    os.environ.get("GRPC_ASYNCIO_ENGINE", "").lower() == "custom_io_manager",
    "Compatible mode needs POLLER completion queue.",
)
class TestCompatibility(AioTestBase):
    async def setUp(self):
        # A per-test executor that can tell whether all of its workers
        # returned
        self._thread_pool = _common.DaemonThreadExecutor()
        self._async_server = aio.server(
            options=(("grpc.so_reuseport", 0),),
            migration_thread_pool=self._thread_pool,
        )

        test_pb2_grpc.add_TestServiceServicer_to_server(
            TestServiceServicer(), self._async_server
        )
        self._adhoc_handlers = _common.AdhocGenericHandler()
        self._async_server.add_generic_rpc_handlers((self._adhoc_handlers,))

        port = self._async_server.add_insecure_port("[::]:0")
        address = "localhost:%d" % port
        await self._async_server.start()

        # Create async stub
        self._async_channel = aio.insecure_channel(
            address, options=_unique_options()
        )
        self._async_stub = test_pb2_grpc.TestServiceStub(self._async_channel)

        # Create sync stub
        self._sync_channel = grpc.insecure_channel(
            address, options=_unique_options()
        )
        self._sync_stub = test_pb2_grpc.TestServiceStub(self._sync_channel)

    async def tearDown(self):
        self._sync_channel.close()
        await self._async_channel.close()
        await self._async_server.stop(None)

    async def _run_in_another_thread(self, func: Callable[[], None]):
        work_done = asyncio.Event()

        def thread_work():
            func()
            self.loop.call_soon_threadsafe(work_done.set)

        thread = threading.Thread(target=thread_work, daemon=True)
        thread.start()
        await work_done.wait()
        thread.join()

    async def test_unary_unary(self):
        # Calling async API in this thread
        await self._async_stub.UnaryCall(
            messages_pb2.SimpleRequest(), timeout=test_constants.LONG_TIMEOUT
        )

        # Calling sync API in a different thread
        def sync_work() -> None:
            response, call = self._sync_stub.UnaryCall.with_call(
                messages_pb2.SimpleRequest(),
                timeout=test_constants.LONG_TIMEOUT,
            )
            self.assertIsInstance(response, messages_pb2.SimpleResponse)
            self.assertEqual(grpc.StatusCode.OK, call.code())

        await self._run_in_another_thread(sync_work)

    async def test_unary_stream(self):
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
            )

        # Calling async API in this thread
        call = self._async_stub.StreamingOutputCall(request)

        for _ in range(_NUM_STREAM_RESPONSES):
            await call.read()
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        # Calling sync API in a different thread
        def sync_work() -> None:
            response_iterator = self._sync_stub.StreamingOutputCall(request)
            for response in response_iterator:
                assert _RESPONSE_PAYLOAD_SIZE == len(response.payload.body)
            self.assertEqual(grpc.StatusCode.OK, response_iterator.code())

        await self._run_in_another_thread(sync_work)

    async def test_stream_unary(self):
        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        # Calling async API in this thread
        async def gen():
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request

        response = await self._async_stub.StreamingInputCall(gen())
        self.assertEqual(
            _NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
            response.aggregated_payload_size,
        )

        # Calling sync API in a different thread
        def sync_work() -> None:
            response = self._sync_stub.StreamingInputCall(
                iter([request] * _NUM_STREAM_RESPONSES)
            )
            self.assertEqual(
                _NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
                response.aggregated_payload_size,
            )

        await self._run_in_another_thread(sync_work)

    async def test_stream_stream(self):
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
        )

        # Calling async API in this thread
        call = self._async_stub.FullDuplexCall()

        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(request)
            response = await call.read()
            assert _RESPONSE_PAYLOAD_SIZE == len(response.payload.body)

        await call.done_writing()
        assert await call.code() == grpc.StatusCode.OK

        # Calling sync API in a different thread
        def sync_work() -> None:
            response_iterator = self._sync_stub.FullDuplexCall(iter([request]))
            for response in response_iterator:
                assert _RESPONSE_PAYLOAD_SIZE == len(response.payload.body)
            self.assertEqual(grpc.StatusCode.OK, response_iterator.code())

        await self._run_in_another_thread(sync_work)

    async def test_server(self):
        class GenericHandlers(grpc.GenericRpcHandler):
            def service(self, handler_call_details):
                return grpc.unary_unary_rpc_method_handler(lambda x, _: x)

        # It's fine to instantiate server object in the event loop thread.
        # The server will spawn its own serving thread.
        server = grpc.server(
            concurrent.futures.ThreadPoolExecutor(),
            handlers=(GenericHandlers(),),
        )
        port = server.add_insecure_port("localhost:0")
        server.start()

        def sync_work() -> None:
            for _ in range(100):
                with grpc.insecure_channel("localhost:%d" % port) as channel:
                    response = channel.unary_unary("/test/test")(b"\x07\x08")
                    self.assertEqual(response, b"\x07\x08")

        await self._run_in_another_thread(sync_work)

    async def test_many_loop(self):
        address, server = await start_test_server()

        # Run another loop in another thread
        def sync_work():
            async def async_work():
                # Create async stub
                async_channel = aio.insecure_channel(
                    address, options=_unique_options()
                )
                async_stub = test_pb2_grpc.TestServiceStub(async_channel)

                call = async_stub.UnaryCall(messages_pb2.SimpleRequest())
                response = await call
                self.assertIsInstance(response, messages_pb2.SimpleResponse)
                self.assertEqual(grpc.StatusCode.OK, await call.code())

            loop = asyncio.new_event_loop()
            loop.run_until_complete(async_work())

        await self._run_in_another_thread(sync_work)
        await server.stop(None)

    async def test_sync_unary_unary_success(self):
        @grpc.unary_unary_rpc_method_handler
        def echo_unary_unary(request: bytes, unused_context):
            return request

        self._adhoc_handlers.set_adhoc_handler(echo_unary_unary)
        response = await self._async_channel.unary_unary(_common.ADHOC_METHOD)(
            _REQUEST
        )
        self.assertEqual(_REQUEST, response)

    async def test_sync_unary_unary_metadata(self):
        metadata = (("unique", "key-42"),)

        @grpc.unary_unary_rpc_method_handler
        def metadata_unary_unary(request: bytes, context: grpc.ServicerContext):
            context.send_initial_metadata(metadata)
            return request

        self._adhoc_handlers.set_adhoc_handler(metadata_unary_unary)
        call = self._async_channel.unary_unary(_common.ADHOC_METHOD)(_REQUEST)
        self.assertTrue(
            _common.seen_metadata(
                aio.Metadata(*metadata), await call.initial_metadata()
            )
        )

    async def test_sync_unary_unary_abort(self):
        seen = []

        @grpc.unary_unary_rpc_method_handler
        def abort_unary_unary(request: bytes, context: grpc.ServicerContext):
            context.abort(grpc.StatusCode.INTERNAL, "Test")
            seen.append("after_abort")

        self._adhoc_handlers.set_adhoc_handler(abort_unary_unary)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._async_channel.unary_unary(_common.ADHOC_METHOD)(
                _REQUEST
            )
        self.assertEqual(
            grpc.StatusCode.INTERNAL, exception_context.exception.code()
        )
        self.assertEqual([], seen)

    async def test_sync_unary_unary_set_code(self):
        @grpc.unary_unary_rpc_method_handler
        def set_code_unary_unary(request: bytes, context: grpc.ServicerContext):
            context.set_code(grpc.StatusCode.INTERNAL)

        self._adhoc_handlers.set_adhoc_handler(set_code_unary_unary)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._async_channel.unary_unary(_common.ADHOC_METHOD)(
                _REQUEST
            )
        self.assertEqual(
            grpc.StatusCode.INTERNAL, exception_context.exception.code()
        )

    async def test_sync_unary_stream_success(self):
        @grpc.unary_stream_rpc_method_handler
        def echo_unary_stream(request: bytes, unused_context):
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request

        self._adhoc_handlers.set_adhoc_handler(echo_unary_stream)
        call = self._async_channel.unary_stream(_common.ADHOC_METHOD)(_REQUEST)
        async for response in call:
            self.assertEqual(_REQUEST, response)

    async def test_sync_unary_stream_error(self):
        @grpc.unary_stream_rpc_method_handler
        def error_unary_stream(request: bytes, unused_context):
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request
            raise RuntimeError("Test")

        self._adhoc_handlers.set_adhoc_handler(error_unary_stream)
        call = self._async_channel.unary_stream(_common.ADHOC_METHOD)(_REQUEST)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            async for response in call:
                self.assertEqual(_REQUEST, response)
        self.assertEqual(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    async def test_sync_unary_stream_cancel(self):
        closed = threading.Event()

        @grpc.unary_stream_rpc_method_handler
        def endless_unary_stream(request: bytes, unused_context):
            try:
                while True:
                    yield request
            finally:
                closed.set()

        self._adhoc_handlers.set_adhoc_handler(endless_unary_stream)
        call = self._async_channel.unary_stream(_common.ADHOC_METHOD)(_REQUEST)
        self.assertEqual(_REQUEST, await call.read())
        self.assertEqual(_REQUEST, await call.read())
        call.cancel()
        self.assertEqual(grpc.StatusCode.CANCELLED, await call.code())

        self.assertTrue(
            await self.loop.run_in_executor(
                None, closed.wait, test_constants.TIME_ALLOWANCE
            ),
            "handler generator was not closed",
        )
        self.assertTrue(
            await self.loop.run_in_executor(
                None,
                self._thread_pool.workers_finished,
                test_constants.TIME_ALLOWANCE,
            ),
            "thread pool worker leaked",
        )

    async def test_sync_stream_unary_success(self):
        @grpc.stream_unary_rpc_method_handler
        def echo_stream_unary(
            request_iterator: Iterable[bytes], unused_context
        ):
            self.assertEqual(len(list(request_iterator)), _NUM_STREAM_RESPONSES)
            return _REQUEST

        self._adhoc_handlers.set_adhoc_handler(echo_stream_unary)
        request_iterator = iter([_REQUEST] * _NUM_STREAM_RESPONSES)
        response = await self._async_channel.stream_unary(_common.ADHOC_METHOD)(
            request_iterator
        )
        self.assertEqual(_REQUEST, response)

    async def test_sync_stream_unary_error(self):
        @grpc.stream_unary_rpc_method_handler
        def echo_stream_unary(
            request_iterator: Iterable[bytes], unused_context
        ):
            self.assertEqual(len(list(request_iterator)), _NUM_STREAM_RESPONSES)
            raise RuntimeError("Test")

        self._adhoc_handlers.set_adhoc_handler(echo_stream_unary)
        request_iterator = iter([_REQUEST] * _NUM_STREAM_RESPONSES)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            response = await self._async_channel.stream_unary(
                _common.ADHOC_METHOD
            )(request_iterator)
        self.assertEqual(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    async def test_sync_stream_stream_success(self):
        @grpc.stream_stream_rpc_method_handler
        def echo_stream_stream(
            request_iterator: Iterable[bytes], unused_context
        ):
            for request in request_iterator:
                yield request

        self._adhoc_handlers.set_adhoc_handler(echo_stream_stream)
        request_iterator = iter([_REQUEST] * _NUM_STREAM_RESPONSES)
        call = self._async_channel.stream_stream(_common.ADHOC_METHOD)(
            request_iterator
        )
        async for response in call:
            self.assertEqual(_REQUEST, response)

    async def test_sync_stream_stream_error(self):
        @grpc.stream_stream_rpc_method_handler
        def echo_stream_stream(
            request_iterator: Iterable[bytes], unused_context
        ):
            for request in request_iterator:
                yield request
            raise RuntimeError("test")

        self._adhoc_handlers.set_adhoc_handler(echo_stream_stream)
        request_iterator = iter([_REQUEST] * _NUM_STREAM_RESPONSES)
        call = self._async_channel.stream_stream(_common.ADHOC_METHOD)(
            request_iterator
        )
        with self.assertRaises(aio.AioRpcError) as exception_context:
            async for response in call:
                self.assertEqual(_REQUEST, response)
        self.assertEqual(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )


class TestSyncHandlerInvokedOffLoop(unittest.TestCase):
    """Sync response-streaming handlers must be invoked on the thread pool.

    Such a handler is either a generator function or a plain function (no
    `yield`) returning an iterator. Calling the plain function runs its body
    right away. If the function body blocks on the loop (consuming the request
    iterator, sending initial metadata, aborting) while on the loop's owning
    thread, the server deadlocks.
    """

    def setUp(self):
        self._thread_pool = _common.DaemonThreadExecutor()
        self._adhoc_handlers = _common.AdhocGenericHandler()
        self._loop_thread = _common.LoopInThread()

        async def start_server_and_channel():
            server = aio.server(
                options=(("grpc.so_reuseport", 0),),
                migration_thread_pool=self._thread_pool,
            )
            server.add_generic_rpc_handlers((self._adhoc_handlers,))
            port = server.add_insecure_port("[::]:0")
            await server.start()
            channel = aio.insecure_channel(
                "localhost:%d" % port, options=_unique_options()
            )
            return server, channel

        self._async_server, self._async_channel = self._loop_thread.run(
            start_server_and_channel()
        )

    def tearDown(self):
        async def shutdown():
            await self._async_channel.close()
            await self._async_server.stop(None)

        try:
            self._loop_thread.run(
                shutdown(), timeout=test_constants.SHORT_TIMEOUT
            )
        except concurrent.futures.TimeoutError:
            # The server loop is deadlocked, which is what a failing test
            # looks like here; its thread is a daemon.
            pass
        self._loop_thread.stop()

    def test_plain_function_stream_stream_consumes_requests(self):
        @grpc.stream_stream_rpc_method_handler
        def collect_then_echo(
            request_iterator: Iterable[bytes], unused_context
        ):
            # Blocks until the loop delivers the requests
            return iter(list(request_iterator))

        self._adhoc_handlers.set_adhoc_handler(collect_then_echo)

        async def stream_stream():
            call = self._async_channel.stream_stream(_common.ADHOC_METHOD)(
                iter([_REQUEST] * _NUM_STREAM_RESPONSES),
                timeout=test_constants.SHORT_TIMEOUT,
            )
            return [response async for response in call]

        self.assertEqual(
            [_REQUEST] * _NUM_STREAM_RESPONSES,
            self._loop_thread.run(stream_stream()),
        )
        self.assertTrue(
            self._thread_pool.workers_finished(test_constants.TIME_ALLOWANCE),
            "thread pool worker leaked",
        )

    def test_plain_function_unary_stream_sends_initial_metadata(self):
        @grpc.unary_stream_rpc_method_handler
        def metadata_then_echo(request: bytes, context: grpc.ServicerContext):
            # Blocks until the loop has sent the metadata.
            context.send_initial_metadata(_INITIAL_METADATA)
            return iter([request] * _NUM_STREAM_RESPONSES)

        self._adhoc_handlers.set_adhoc_handler(metadata_then_echo)

        async def unary_stream():
            call = self._async_channel.unary_stream(_common.ADHOC_METHOD)(
                _REQUEST, timeout=test_constants.SHORT_TIMEOUT
            )
            responses = [response async for response in call]
            return responses, await call.initial_metadata()

        responses, initial_metadata = self._loop_thread.run(unary_stream())
        self.assertEqual([_REQUEST] * _NUM_STREAM_RESPONSES, responses)
        self.assertTrue(
            _common.seen_metadata(
                aio.Metadata(*_INITIAL_METADATA), initial_metadata
            )
        )
        self.assertTrue(
            self._thread_pool.workers_finished(test_constants.TIME_ALLOWANCE),
            "thread pool worker leaked",
        )

    def test_plain_function_unary_stream_aborts(self):
        @grpc.unary_stream_rpc_method_handler
        def abort_unary_stream(
            unused_request: bytes, context: grpc.ServicerContext
        ):
            # Blocks until the loop has sent the status.
            context.abort(grpc.StatusCode.INTERNAL, "Test")

        self._adhoc_handlers.set_adhoc_handler(abort_unary_stream)

        async def unary_stream():
            call = self._async_channel.unary_stream(_common.ADHOC_METHOD)(
                _REQUEST, timeout=test_constants.SHORT_TIMEOUT
            )
            return [response async for response in call]

        with self.assertRaises(aio.AioRpcError) as exception_context:
            self._loop_thread.run(unary_stream())
        self.assertEqual(
            grpc.StatusCode.INTERNAL, exception_context.exception.code()
        )
        self.assertEqual(
            "Test", exception_context.exception.details()
        )
        self.assertTrue(
            self._thread_pool.workers_finished(test_constants.TIME_ALLOWANCE),
            "thread pool worker leaked",
        )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
