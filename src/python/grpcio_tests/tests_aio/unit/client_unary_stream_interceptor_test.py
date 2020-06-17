# Copyright 2020 The gRPC Authors.
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
import unittest
import datetime

import grpc

from grpc.experimental import aio
from tests_aio.unit._constants import UNREACHABLE_TARGET
from tests_aio.unit._common import inject_callbacks
from tests_aio.unit._common import CountingResponseIterator
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase
from tests.unit.framework.common import test_constants
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc

_SHORT_TIMEOUT_S = 1.0

_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 7
_RESPONSE_INTERVAL_US = int(_SHORT_TIMEOUT_S * 1000 * 1000)


class _UnaryStreamInterceptorEmpty(aio.UnaryStreamClientInterceptor):

    async def intercept_unary_stream(self, continuation, client_call_details,
                                     request):
        return await continuation(client_call_details, request)

    def assert_in_final_state(self, test: unittest.TestCase):
        pass


class _UnaryStreamInterceptorWithResponseIterator(
        aio.UnaryStreamClientInterceptor):

    async def intercept_unary_stream(self, continuation, client_call_details,
                                     request):
        call = await continuation(client_call_details, request)
        self.response_iterator = CountingResponseIterator(call)
        return self.response_iterator

    def assert_in_final_state(self, test: unittest.TestCase):
        test.assertEqual(_NUM_STREAM_RESPONSES,
                         self.response_iterator.response_cnt)


class TestUnaryStreamClientInterceptor(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_intercepts(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWithResponseIterator):

            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.extend([
                    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
                ] * _NUM_STREAM_RESPONSES)

                channel = aio.insecure_channel(self._server_target,
                                               interceptors=[interceptor])
                stub = test_pb2_grpc.TestServiceStub(channel)
                call = stub.StreamingOutputCall(request)

                await call.wait_for_connection()

                response_cnt = 0
                async for response in call:
                    response_cnt += 1
                    self.assertIs(type(response),
                                  messages_pb2.StreamingOutputCallResponse)
                    self.assertEqual(_RESPONSE_PAYLOAD_SIZE,
                                     len(response.payload.body))

                self.assertEqual(response_cnt, _NUM_STREAM_RESPONSES)
                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), aio.Metadata())
                self.assertEqual(await call.trailing_metadata(), aio.Metadata())
                self.assertEqual(await call.details(), '')
                self.assertEqual(await call.debug_error_string(), '')
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                interceptor.assert_in_final_state(self)

                await channel.close()

    async def test_add_done_callback_interceptor_task_not_finished(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWithResponseIterator):

            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.extend([
                    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
                ] * _NUM_STREAM_RESPONSES)

                channel = aio.insecure_channel(self._server_target,
                                               interceptors=[interceptor])
                stub = test_pb2_grpc.TestServiceStub(channel)
                call = stub.StreamingOutputCall(request)

                validation = inject_callbacks(call)

                async for response in call:
                    pass

                await validation

                await channel.close()

    async def test_add_done_callback_interceptor_task_finished(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWithResponseIterator):

            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.extend([
                    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
                ] * _NUM_STREAM_RESPONSES)

                channel = aio.insecure_channel(self._server_target,
                                               interceptors=[interceptor])
                stub = test_pb2_grpc.TestServiceStub(channel)
                call = stub.StreamingOutputCall(request)

                # This ensures that the callbacks will be registered
                # with the intercepted call rather than saving in the
                # pending state list.
                await call.wait_for_connection()

                validation = inject_callbacks(call)

                async for response in call:
                    pass

                await validation

                await channel.close()

    async def test_response_iterator_using_read(self):
        interceptor = _UnaryStreamInterceptorWithResponseIterator()

        channel = aio.insecure_channel(self._server_target,
                                       interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.extend(
            [messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)] *
            _NUM_STREAM_RESPONSES)

        call = stub.StreamingOutputCall(request)

        response_cnt = 0
        for response in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            response_cnt += 1
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertEqual(response_cnt, _NUM_STREAM_RESPONSES)
        self.assertEqual(interceptor.response_iterator.response_cnt,
                         _NUM_STREAM_RESPONSES)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        await channel.close()

    async def test_multiple_interceptors_response_iterator(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWithResponseIterator):

            with self.subTest(name=interceptor_class):

                interceptors = [interceptor_class(), interceptor_class()]

                channel = aio.insecure_channel(self._server_target,
                                               interceptors=interceptors)
                stub = test_pb2_grpc.TestServiceStub(channel)

                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.extend([
                    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
                ] * _NUM_STREAM_RESPONSES)

                call = stub.StreamingOutputCall(request)

                response_cnt = 0
                async for response in call:
                    response_cnt += 1
                    self.assertIs(type(response),
                                  messages_pb2.StreamingOutputCallResponse)
                    self.assertEqual(_RESPONSE_PAYLOAD_SIZE,
                                     len(response.payload.body))

                self.assertEqual(response_cnt, _NUM_STREAM_RESPONSES)
                self.assertEqual(await call.code(), grpc.StatusCode.OK)

                await channel.close()

    async def test_intercepts_response_iterator_rpc_error(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWithResponseIterator):

            with self.subTest(name=interceptor_class):

                channel = aio.insecure_channel(
                    UNREACHABLE_TARGET, interceptors=[interceptor_class()])
                request = messages_pb2.StreamingOutputCallRequest()
                stub = test_pb2_grpc.TestServiceStub(channel)
                call = stub.StreamingOutputCall(request)

                with self.assertRaises(aio.AioRpcError) as exception_context:
                    async for response in call:
                        pass

                self.assertEqual(grpc.StatusCode.UNAVAILABLE,
                                 exception_context.exception.code())

                self.assertTrue(call.done())
                self.assertEqual(grpc.StatusCode.UNAVAILABLE, await call.code())
                await channel.close()

    async def test_cancel_before_rpc(self):

        interceptor_reached = asyncio.Event()
        wait_for_ever = self.loop.create_future()

        class Interceptor(aio.UnaryStreamClientInterceptor):

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                interceptor_reached.set()
                await wait_for_ever

        channel = aio.insecure_channel(UNREACHABLE_TARGET,
                                       interceptors=[Interceptor()])
        request = messages_pb2.StreamingOutputCallRequest()
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        self.assertFalse(call.cancelled())
        self.assertFalse(call.done())

        await interceptor_reached.wait()
        self.assertTrue(call.cancel())

        with self.assertRaises(asyncio.CancelledError):
            async for response in call:
                pass

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(await call.initial_metadata(), None)
        self.assertEqual(await call.trailing_metadata(), None)
        await channel.close()

    async def test_cancel_after_rpc(self):

        interceptor_reached = asyncio.Event()
        wait_for_ever = self.loop.create_future()

        class Interceptor(aio.UnaryStreamClientInterceptor):

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                interceptor_reached.set()
                await wait_for_ever

        channel = aio.insecure_channel(UNREACHABLE_TARGET,
                                       interceptors=[Interceptor()])
        request = messages_pb2.StreamingOutputCallRequest()
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        self.assertFalse(call.cancelled())
        self.assertFalse(call.done())

        await interceptor_reached.wait()
        self.assertTrue(call.cancel())

        with self.assertRaises(asyncio.CancelledError):
            async for response in call:
                pass

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(await call.initial_metadata(), None)
        self.assertEqual(await call.trailing_metadata(), None)
        await channel.close()

    async def test_cancel_consuming_response_iterator(self):
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.extend(
            [messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)] *
            _NUM_STREAM_RESPONSES)

        channel = aio.insecure_channel(
            self._server_target,
            interceptors=[_UnaryStreamInterceptorWithResponseIterator()])
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        with self.assertRaises(asyncio.CancelledError):
            async for response in call:
                call.cancel()

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        await channel.close()

    async def test_cancel_by_the_interceptor(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                call.cancel()
                return call

        channel = aio.insecure_channel(UNREACHABLE_TARGET,
                                       interceptors=[Interceptor()])
        request = messages_pb2.StreamingOutputCallRequest()
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        with self.assertRaises(asyncio.CancelledError):
            async for response in call:
                pass

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        await channel.close()

    async def test_exception_raised_by_interceptor(self):

        class InterceptorException(Exception):
            pass

        class Interceptor(aio.UnaryStreamClientInterceptor):

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                raise InterceptorException

        channel = aio.insecure_channel(UNREACHABLE_TARGET,
                                       interceptors=[Interceptor()])
        request = messages_pb2.StreamingOutputCallRequest()
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        with self.assertRaises(InterceptorException):
            async for response in call:
                pass

        await channel.close()


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
