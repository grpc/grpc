# Copyright 2019 The gRPC Authors.
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
import socket
import time
import unittest

import grpc
from grpc.experimental import aio

from tests.unit import resources
from tests.unit.framework.common import test_constants
from tests_aio.unit._test_base import AioTestBase

_SIMPLE_UNARY_UNARY = '/test/SimpleUnaryUnary'
_BLOCK_FOREVER = '/test/BlockForever'
_BLOCK_BRIEFLY = '/test/BlockBriefly'
_UNARY_STREAM_ASYNC_GEN = '/test/UnaryStreamAsyncGen'
_UNARY_STREAM_READER_WRITER = '/test/UnaryStreamReaderWriter'
_UNARY_STREAM_EVILLY_MIXED = '/test/UnaryStreamEvillyMixed'
_STREAM_UNARY_ASYNC_GEN = '/test/StreamUnaryAsyncGen'
_STREAM_UNARY_READER_WRITER = '/test/StreamUnaryReaderWriter'
_STREAM_UNARY_EVILLY_MIXED = '/test/StreamUnaryEvillyMixed'
_STREAM_STREAM_ASYNC_GEN = '/test/StreamStreamAsyncGen'
_STREAM_STREAM_READER_WRITER = '/test/StreamStreamReaderWriter'
_STREAM_STREAM_EVILLY_MIXED = '/test/StreamStreamEvillyMixed'
_UNIMPLEMENTED_METHOD = '/test/UnimplementedMethod'
_ERROR_IN_STREAM_STREAM = '/test/ErrorInStreamStream'
_ERROR_WITHOUT_RAISE_IN_UNARY_UNARY = '/test/ErrorWithoutRaiseInUnaryUnary'
_ERROR_WITHOUT_RAISE_IN_STREAM_STREAM = '/test/ErrorWithoutRaiseInStreamStream'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'
_NUM_STREAM_REQUESTS = 3
_NUM_STREAM_RESPONSES = 5
_MAXIMUM_CONCURRENT_RPCS = 5


class _GenericHandler(grpc.GenericRpcHandler):

    def __init__(self):
        self._called = asyncio.get_event_loop().create_future()
        self._routing_table = {
            _SIMPLE_UNARY_UNARY:
                grpc.unary_unary_rpc_method_handler(self._unary_unary),
            _BLOCK_FOREVER:
                grpc.unary_unary_rpc_method_handler(self._block_forever),
            _BLOCK_BRIEFLY:
                grpc.unary_unary_rpc_method_handler(self._block_briefly),
            _UNARY_STREAM_ASYNC_GEN:
                grpc.unary_stream_rpc_method_handler(
                    self._unary_stream_async_gen),
            _UNARY_STREAM_READER_WRITER:
                grpc.unary_stream_rpc_method_handler(
                    self._unary_stream_reader_writer),
            _UNARY_STREAM_EVILLY_MIXED:
                grpc.unary_stream_rpc_method_handler(
                    self._unary_stream_evilly_mixed),
            _STREAM_UNARY_ASYNC_GEN:
                grpc.stream_unary_rpc_method_handler(
                    self._stream_unary_async_gen),
            _STREAM_UNARY_READER_WRITER:
                grpc.stream_unary_rpc_method_handler(
                    self._stream_unary_reader_writer),
            _STREAM_UNARY_EVILLY_MIXED:
                grpc.stream_unary_rpc_method_handler(
                    self._stream_unary_evilly_mixed),
            _STREAM_STREAM_ASYNC_GEN:
                grpc.stream_stream_rpc_method_handler(
                    self._stream_stream_async_gen),
            _STREAM_STREAM_READER_WRITER:
                grpc.stream_stream_rpc_method_handler(
                    self._stream_stream_reader_writer),
            _STREAM_STREAM_EVILLY_MIXED:
                grpc.stream_stream_rpc_method_handler(
                    self._stream_stream_evilly_mixed),
            _ERROR_IN_STREAM_STREAM:
                grpc.stream_stream_rpc_method_handler(
                    self._error_in_stream_stream),
            _ERROR_WITHOUT_RAISE_IN_UNARY_UNARY:
                grpc.unary_unary_rpc_method_handler(
                    self._error_without_raise_in_unary_unary),
            _ERROR_WITHOUT_RAISE_IN_STREAM_STREAM:
                grpc.stream_stream_rpc_method_handler(
                    self._error_without_raise_in_stream_stream),
        }

    @staticmethod
    async def _unary_unary(unused_request, unused_context):
        return _RESPONSE

    async def _block_forever(self, unused_request, unused_context):
        await asyncio.get_event_loop().create_future()

    async def _block_briefly(self, unused_request, unused_context):
        await asyncio.sleep(test_constants.SHORT_TIMEOUT / 2)
        return _RESPONSE

    async def _unary_stream_async_gen(self, unused_request, unused_context):
        for _ in range(_NUM_STREAM_RESPONSES):
            yield _RESPONSE

    async def _unary_stream_reader_writer(self, unused_request, context):
        for _ in range(_NUM_STREAM_RESPONSES):
            await context.write(_RESPONSE)

    async def _unary_stream_evilly_mixed(self, unused_request, context):
        yield _RESPONSE
        for _ in range(_NUM_STREAM_RESPONSES - 1):
            await context.write(_RESPONSE)

    async def _stream_unary_async_gen(self, request_iterator, unused_context):
        request_count = 0
        async for request in request_iterator:
            assert _REQUEST == request
            request_count += 1
        assert _NUM_STREAM_REQUESTS == request_count
        return _RESPONSE

    async def _stream_unary_reader_writer(self, unused_request, context):
        for _ in range(_NUM_STREAM_REQUESTS):
            assert _REQUEST == await context.read()
        return _RESPONSE

    async def _stream_unary_evilly_mixed(self, request_iterator, context):
        assert _REQUEST == await context.read()
        request_count = 0
        async for request in request_iterator:
            assert _REQUEST == request
            request_count += 1
        assert _NUM_STREAM_REQUESTS - 1 == request_count
        return _RESPONSE

    async def _stream_stream_async_gen(self, request_iterator, unused_context):
        request_count = 0
        async for request in request_iterator:
            assert _REQUEST == request
            request_count += 1
        assert _NUM_STREAM_REQUESTS == request_count

        for _ in range(_NUM_STREAM_RESPONSES):
            yield _RESPONSE

    async def _stream_stream_reader_writer(self, unused_request, context):
        for _ in range(_NUM_STREAM_REQUESTS):
            assert _REQUEST == await context.read()
        for _ in range(_NUM_STREAM_RESPONSES):
            await context.write(_RESPONSE)

    async def _stream_stream_evilly_mixed(self, request_iterator, context):
        assert _REQUEST == await context.read()
        request_count = 0
        async for request in request_iterator:
            assert _REQUEST == request
            request_count += 1
        assert _NUM_STREAM_REQUESTS - 1 == request_count

        yield _RESPONSE
        for _ in range(_NUM_STREAM_RESPONSES - 1):
            await context.write(_RESPONSE)

    async def _error_in_stream_stream(self, request_iterator, unused_context):
        async for request in request_iterator:
            assert _REQUEST == request
            raise RuntimeError('A testing RuntimeError!')
        yield _RESPONSE

    async def _error_without_raise_in_unary_unary(self, request, context):
        assert _REQUEST == request
        context.set_code(grpc.StatusCode.INTERNAL)

    async def _error_without_raise_in_stream_stream(self, request_iterator,
                                                    context):
        async for request in request_iterator:
            assert _REQUEST == request
        context.set_code(grpc.StatusCode.INTERNAL)

    def service(self, handler_details):
        if not self._called.done():
            self._called.set_result(None)
        return self._routing_table.get(handler_details.method)

    async def wait_for_call(self):
        await self._called


async def _start_test_server():
    server = aio.server()
    port = server.add_insecure_port('[::]:0')
    generic_handler = _GenericHandler()
    server.add_generic_rpc_handlers((generic_handler,))
    await server.start()
    return 'localhost:%d' % port, server, generic_handler


class TestServer(AioTestBase):

    async def setUp(self):
        addr, self._server, self._generic_handler = await _start_test_server()
        self._channel = aio.insecure_channel(addr)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def test_unary_unary(self):
        unary_unary_call = self._channel.unary_unary(_SIMPLE_UNARY_UNARY)
        response = await unary_unary_call(_REQUEST)
        self.assertEqual(response, _RESPONSE)

    async def test_unary_stream_async_generator(self):
        unary_stream_call = self._channel.unary_stream(_UNARY_STREAM_ASYNC_GEN)
        call = unary_stream_call(_REQUEST)

        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertEqual(_RESPONSE, response)

        self.assertEqual(_NUM_STREAM_RESPONSES, response_cnt)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_unary_stream_reader_writer(self):
        unary_stream_call = self._channel.unary_stream(
            _UNARY_STREAM_READER_WRITER)
        call = unary_stream_call(_REQUEST)

        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertEqual(_RESPONSE, response)

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_unary_stream_evilly_mixed(self):
        unary_stream_call = self._channel.unary_stream(
            _UNARY_STREAM_EVILLY_MIXED)
        call = unary_stream_call(_REQUEST)

        # Uses reader API
        self.assertEqual(_RESPONSE, await call.read())

        # Uses async generator API, mixed!
        with self.assertRaises(aio.UsageError):
            async for response in call:
                self.assertEqual(_RESPONSE, response)

    async def test_stream_unary_async_generator(self):
        stream_unary_call = self._channel.stream_unary(_STREAM_UNARY_ASYNC_GEN)
        call = stream_unary_call()

        for _ in range(_NUM_STREAM_REQUESTS):
            await call.write(_REQUEST)
        await call.done_writing()

        response = await call
        self.assertEqual(_RESPONSE, response)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_stream_unary_reader_writer(self):
        stream_unary_call = self._channel.stream_unary(
            _STREAM_UNARY_READER_WRITER)
        call = stream_unary_call()

        for _ in range(_NUM_STREAM_REQUESTS):
            await call.write(_REQUEST)
        await call.done_writing()

        response = await call
        self.assertEqual(_RESPONSE, response)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_stream_unary_evilly_mixed(self):
        stream_unary_call = self._channel.stream_unary(
            _STREAM_UNARY_EVILLY_MIXED)
        call = stream_unary_call()

        for _ in range(_NUM_STREAM_REQUESTS):
            await call.write(_REQUEST)
        await call.done_writing()

        response = await call
        self.assertEqual(_RESPONSE, response)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_stream_stream_async_generator(self):
        stream_stream_call = self._channel.stream_stream(
            _STREAM_STREAM_ASYNC_GEN)
        call = stream_stream_call()

        for _ in range(_NUM_STREAM_REQUESTS):
            await call.write(_REQUEST)
        await call.done_writing()

        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertEqual(_RESPONSE, response)

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_stream_stream_reader_writer(self):
        stream_stream_call = self._channel.stream_stream(
            _STREAM_STREAM_READER_WRITER)
        call = stream_stream_call()

        for _ in range(_NUM_STREAM_REQUESTS):
            await call.write(_REQUEST)
        await call.done_writing()

        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertEqual(_RESPONSE, response)

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_stream_stream_evilly_mixed(self):
        stream_stream_call = self._channel.stream_stream(
            _STREAM_STREAM_EVILLY_MIXED)
        call = stream_stream_call()

        for _ in range(_NUM_STREAM_REQUESTS):
            await call.write(_REQUEST)
        await call.done_writing()

        for _ in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            self.assertEqual(_RESPONSE, response)

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_shutdown(self):
        await self._server.stop(None)
        # Ensures no SIGSEGV triggered, and ends within timeout.

    async def test_shutdown_after_call(self):
        await self._channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)

        await self._server.stop(None)

    async def test_graceful_shutdown_success(self):
        call = self._channel.unary_unary(_BLOCK_BRIEFLY)(_REQUEST)
        await self._generic_handler.wait_for_call()

        shutdown_start_time = time.time()
        await self._server.stop(test_constants.SHORT_TIMEOUT)
        grace_period_length = time.time() - shutdown_start_time
        self.assertGreater(grace_period_length,
                           test_constants.SHORT_TIMEOUT / 3)

        # Validates the states.
        self.assertEqual(_RESPONSE, await call)
        self.assertTrue(call.done())

    async def test_graceful_shutdown_failed(self):
        call = self._channel.unary_unary(_BLOCK_FOREVER)(_REQUEST)
        await self._generic_handler.wait_for_call()

        await self._server.stop(test_constants.SHORT_TIMEOUT)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        self.assertEqual(grpc.StatusCode.UNAVAILABLE,
                         exception_context.exception.code())

    async def test_concurrent_graceful_shutdown(self):
        call = self._channel.unary_unary(_BLOCK_BRIEFLY)(_REQUEST)
        await self._generic_handler.wait_for_call()

        # Expects the shortest grace period to be effective.
        shutdown_start_time = time.time()
        await asyncio.gather(
            self._server.stop(test_constants.LONG_TIMEOUT),
            self._server.stop(test_constants.SHORT_TIMEOUT),
            self._server.stop(test_constants.LONG_TIMEOUT),
        )
        grace_period_length = time.time() - shutdown_start_time
        self.assertGreater(grace_period_length,
                           test_constants.SHORT_TIMEOUT / 3)

        self.assertEqual(_RESPONSE, await call)
        self.assertTrue(call.done())

    async def test_concurrent_graceful_shutdown_immediate(self):
        call = self._channel.unary_unary(_BLOCK_FOREVER)(_REQUEST)
        await self._generic_handler.wait_for_call()

        # Expects no grace period, due to the "server.stop(None)".
        await asyncio.gather(
            self._server.stop(test_constants.LONG_TIMEOUT),
            self._server.stop(None),
            self._server.stop(test_constants.SHORT_TIMEOUT),
            self._server.stop(test_constants.LONG_TIMEOUT),
        )

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        self.assertEqual(grpc.StatusCode.UNAVAILABLE,
                         exception_context.exception.code())

    async def test_shutdown_before_call(self):
        await self._server.stop(None)

        # Ensures the server is cleaned up at this point.
        # Some proper exception should be raised.
        with self.assertRaises(aio.AioRpcError):
            await self._channel.unary_unary(_SIMPLE_UNARY_UNARY)(_REQUEST)

    async def test_unimplemented(self):
        call = self._channel.unary_unary(_UNIMPLEMENTED_METHOD)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call(_REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNIMPLEMENTED, rpc_error.code())

    async def test_shutdown_during_stream_stream(self):
        stream_stream_call = self._channel.stream_stream(
            _STREAM_STREAM_ASYNC_GEN)
        call = stream_stream_call()

        # Don't half close the RPC yet, keep it alive.
        await call.write(_REQUEST)
        await self._server.stop(None)

        self.assertEqual(grpc.StatusCode.UNAVAILABLE, await call.code())
        # No segfault

    async def test_error_in_stream_stream(self):
        stream_stream_call = self._channel.stream_stream(
            _ERROR_IN_STREAM_STREAM)
        call = stream_stream_call()

        # Don't half close the RPC yet, keep it alive.
        await call.write(_REQUEST)

        # Don't segfault here
        self.assertEqual(grpc.StatusCode.UNKNOWN, await call.code())

    async def test_error_without_raise_in_unary_unary(self):
        call = self._channel.unary_unary(_ERROR_WITHOUT_RAISE_IN_UNARY_UNARY)(
            _REQUEST)

        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call

        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.INTERNAL, rpc_error.code())

    async def test_error_without_raise_in_stream_stream(self):
        call = self._channel.stream_stream(
            _ERROR_WITHOUT_RAISE_IN_STREAM_STREAM)()

        for _ in range(_NUM_STREAM_REQUESTS):
            await call.write(_REQUEST)
        await call.done_writing()

        self.assertEqual(grpc.StatusCode.INTERNAL, await call.code())

    async def test_port_binding_exception(self):
        server = aio.server(options=(('grpc.so_reuseport', 0),))
        port = server.add_insecure_port('localhost:0')
        bind_address = "localhost:%d" % port

        with self.assertRaises(RuntimeError):
            server.add_insecure_port(bind_address)

        server_credentials = grpc.ssl_server_credentials([
            (resources.private_key(), resources.certificate_chain())
        ])
        with self.assertRaises(RuntimeError):
            server.add_secure_port(bind_address, server_credentials)

    async def test_maximum_concurrent_rpcs(self):
        # Build the server with concurrent rpc argument
        server = aio.server(maximum_concurrent_rpcs=_MAXIMUM_CONCURRENT_RPCS)
        port = server.add_insecure_port('localhost:0')
        bind_address = "localhost:%d" % port
        server.add_generic_rpc_handlers((_GenericHandler(),))
        await server.start()
        # Build the channel
        channel = aio.insecure_channel(bind_address)
        # Deplete the concurrent quota with 3 times of max RPCs
        rpcs = []
        for _ in range(3 * _MAXIMUM_CONCURRENT_RPCS):
            rpcs.append(channel.unary_unary(_BLOCK_BRIEFLY)(_REQUEST))
        task = self.loop.create_task(
            asyncio.wait(rpcs, return_when=asyncio.FIRST_EXCEPTION))
        # Each batch took test_constants.SHORT_TIMEOUT /2
        start_time = time.time()
        await task
        elapsed_time = time.time() - start_time
        self.assertGreater(elapsed_time, test_constants.SHORT_TIMEOUT * 3 / 2)
        # Clean-up
        await channel.close()
        await server.stop(0)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
