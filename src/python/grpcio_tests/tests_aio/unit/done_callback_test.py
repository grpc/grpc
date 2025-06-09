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
"""Testing the done callbacks mechanism."""

import asyncio
import logging
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests_aio.unit._common import inject_callbacks
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 42
_REQUEST = b"\x01\x02\x03"
_RESPONSE = b"\x04\x05\x06"
_TEST_METHOD = "/test/Test"
_FAKE_METHOD = "/test/Fake"


class TestClientSideDoneCallback(AioTestBase):
    async def setUp(self):
        address, self._server = await start_test_server()
        self._channel = aio.insecure_channel(address)
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def test_add_after_done(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        validation = inject_callbacks(call)
        await validation

    async def test_unary_unary(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        validation = inject_callbacks(call)

        self.assertEqual(grpc.StatusCode.OK, await call.code())

        await validation

    async def test_unary_stream(self):
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
            )

        call = self._stub.StreamingOutputCall(request)
        validation = inject_callbacks(call)

        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertIsInstance(
                response, messages_pb2.StreamingOutputCallResponse
            )
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(_NUM_STREAM_RESPONSES, response_cnt)
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        await validation

    async def test_stream_unary(self):
        payload = messages_pb2.Payload(body=b"\0" * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        async def gen():
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request

        call = self._stub.StreamingInputCall(gen())
        validation = inject_callbacks(call)

        response = await call
        self.assertIsInstance(response, messages_pb2.StreamingInputCallResponse)
        self.assertEqual(
            _NUM_STREAM_RESPONSES * _REQUEST_PAYLOAD_SIZE,
            response.aggregated_payload_size,
        )
        self.assertEqual(grpc.StatusCode.OK, await call.code())

        await validation

    async def test_stream_stream(self):
        call = self._stub.FullDuplexCall()
        validation = inject_callbacks(call)

        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
        )

        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(request)
            response = await call.read()
            self.assertIsInstance(
                response, messages_pb2.StreamingOutputCallResponse
            )
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        await call.done_writing()

        self.assertEqual(grpc.StatusCode.OK, await call.code())
        await validation


class TestServerSideDoneCallback(AioTestBase):
    async def setUp(self):
        self._server = aio.server()
        port = self._server.add_insecure_port("[::]:0")
        self._channel = aio.insecure_channel("localhost:%d" % port)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def _register_method_handler(self, method_handler):
        """Registers method handler and starts the server"""
        generic_handler = grpc.method_handlers_generic_handler(
            "test",
            dict(Test=method_handler),
        )
        self._server.add_generic_rpc_handlers((generic_handler,))
        await self._server.start()

    async def test_unary_unary(self):
        validation_future = self.loop.create_future()

        async def test_handler(request: bytes, context: aio.ServicerContext):
            self.assertEqual(_REQUEST, request)
            validation_future.set_result(inject_callbacks(context))
            return _RESPONSE

        await self._register_method_handler(
            grpc.unary_unary_rpc_method_handler(test_handler)
        )
        response = await self._channel.unary_unary(_TEST_METHOD)(_REQUEST)
        self.assertEqual(_RESPONSE, response)

        validation = await validation_future
        await validation

    async def test_unary_stream(self):
        validation_future = self.loop.create_future()

        async def test_handler(request: bytes, context: aio.ServicerContext):
            self.assertEqual(_REQUEST, request)
            validation_future.set_result(inject_callbacks(context))
            for _ in range(_NUM_STREAM_RESPONSES):
                yield _RESPONSE

        await self._register_method_handler(
            grpc.unary_stream_rpc_method_handler(test_handler)
        )
        call = self._channel.unary_stream(_TEST_METHOD)(_REQUEST)
        async for response in call:
            self.assertEqual(_RESPONSE, response)

        validation = await validation_future
        await validation

    async def test_stream_unary(self):
        validation_future = self.loop.create_future()

        async def test_handler(request_iterator, context: aio.ServicerContext):
            validation_future.set_result(inject_callbacks(context))

            async for request in request_iterator:
                self.assertEqual(_REQUEST, request)
            return _RESPONSE

        await self._register_method_handler(
            grpc.stream_unary_rpc_method_handler(test_handler)
        )
        call = self._channel.stream_unary(_TEST_METHOD)()
        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(_REQUEST)
        await call.done_writing()
        self.assertEqual(_RESPONSE, await call)

        validation = await validation_future
        await validation

    async def test_stream_stream(self):
        validation_future = self.loop.create_future()

        async def test_handler(request_iterator, context: aio.ServicerContext):
            validation_future.set_result(inject_callbacks(context))

            async for request in request_iterator:
                self.assertEqual(_REQUEST, request)
            return _RESPONSE

        await self._register_method_handler(
            grpc.stream_stream_rpc_method_handler(test_handler)
        )
        call = self._channel.stream_stream(_TEST_METHOD)()
        for _ in range(_NUM_STREAM_RESPONSES):
            await call.write(_REQUEST)
        await call.done_writing()
        async for response in call:
            self.assertEqual(_RESPONSE, response)

        validation = await validation_future
        await validation

    async def test_error_in_handler(self):
        """Errors in the handler still triggers callbacks."""
        validation_future = self.loop.create_future()

        async def test_handler(request: bytes, context: aio.ServicerContext):
            self.assertEqual(_REQUEST, request)
            validation_future.set_result(inject_callbacks(context))
            raise RuntimeError("A test RuntimeError")

        await self._register_method_handler(
            grpc.unary_unary_rpc_method_handler(test_handler)
        )
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channel.unary_unary(_TEST_METHOD)(_REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNKNOWN, rpc_error.code())

        validation = await validation_future
        await validation

    async def test_error_in_callback(self):
        """Errors in the callback won't be propagated to client."""
        validation_future = self.loop.create_future()

        async def test_handler(request: bytes, context: aio.ServicerContext):
            self.assertEqual(_REQUEST, request)

            def exception_raiser(unused_context):
                raise RuntimeError("A test RuntimeError")

            context.add_done_callback(exception_raiser)
            validation_future.set_result(inject_callbacks(context))
            return _RESPONSE

        await self._register_method_handler(
            grpc.unary_unary_rpc_method_handler(test_handler)
        )

        response = await self._channel.unary_unary(_TEST_METHOD)(_REQUEST)
        self.assertEqual(_RESPONSE, response)

        # Following callbacks won't be invoked, if one of the callback crashed.
        validation = await validation_future
        with self.assertRaises(asyncio.TimeoutError):
            await validation

        # Invoke RPC one more time to ensure the toxic callback won't break the
        # server.
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channel.unary_unary(_FAKE_METHOD)(_REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(grpc.StatusCode.UNIMPLEMENTED, rpc_error.code())


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
