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
import logging
import unittest
import datetime

import grpc

from grpc.experimental import aio
from tests_aio.unit._constants import UNREACHABLE_TARGET
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase
from tests.unit.framework.common import test_constants
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc

_SHORT_TIMEOUT_S = datetime.timedelta(seconds=1).total_seconds()

_LOCAL_CANCEL_DETAILS_EXPECTATION = 'Locally cancelled by application!'
_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 7
_RESPONSE_INTERVAL_US = int(_SHORT_TIMEOUT_S * 1000 * 1000)


class _ResponseIterator:

    def __init__(self, response_iterator):
        self._response_cnt = 0
        self._response_iterator = response_iterator

    async def _forward_responses(self):
        async for response in self._response_iterator:
            self._response_cnt += 1
            yield response

    def __aiter__(self):
        return self._forward_responses()

    @property
    def response_cnt(self):
        return self._response_cnt


def _inject_callbacks(call):
    first_callback_ran = asyncio.Event()

    def first_callback(call):
        # Validate that all resopnses have been received
        # and the call is an end state.
        assert call.done()
        first_callback_ran.set()

    second_callback_ran = asyncio.Event()

    def second_callback(call):
        # Validate that all resopnses have been received
        # and the call is an end state.
        assert call.done()
        second_callback_ran.set()

    call.add_done_callback(first_callback)
    call.add_done_callback(second_callback)

    async def validation():
        await asyncio.wait_for(
            asyncio.gather(first_callback_ran.wait(),
                           second_callback_ran.wait()),
            test_constants.SHORT_TIMEOUT)

    return validation()


class _UnaryStreamInterceptorEmpty(aio.UnaryStreamClientInterceptor):

    async def intercept_unary_stream(self, continuation, client_call_details,
                                     request):
        return await continuation(client_call_details, request)


class _UnaryStreamInterceptorWith_ResponseIterator(
        aio.UnaryStreamClientInterceptor):

    def __init__(self):
        self.response_iterator = None

    async def intercept_unary_stream(self, continuation, client_call_details,
                                     request):
        call = await continuation(client_call_details, request)
        self.response_iterator = _ResponseIterator(call)
        return self.response_iterator


class TestUnaryStreamClientInterceptor(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_intercepts(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWith_ResponseIterator):

            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                request = messages_pb2.StreamingOutputCallRequest()
                for _ in range(_NUM_STREAM_RESPONSES):
                    request.response_parameters.append(
                        messages_pb2.ResponseParameters(
                            size=_RESPONSE_PAYLOAD_SIZE))

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

                self.assertTrue(response_cnt, _NUM_STREAM_RESPONSES)
                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), ())
                self.assertEqual(await call.trailing_metadata(), ())
                self.assertEqual(await call.details(), '')
                self.assertEqual(await call.debug_error_string(), '')
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                if interceptor_class == _UnaryStreamInterceptorWith_ResponseIterator:
                    self.assertTrue(interceptor.response_iterator.response_cnt,
                                    _NUM_STREAM_RESPONSES)

                await channel.close()

    async def test_add_done_callback(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWith_ResponseIterator):

            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                request = messages_pb2.StreamingOutputCallRequest()
                for _ in range(_NUM_STREAM_RESPONSES):
                    request.response_parameters.append(
                        messages_pb2.ResponseParameters(
                            size=_RESPONSE_PAYLOAD_SIZE))

                channel = aio.insecure_channel(self._server_target,
                                               interceptors=[interceptor])
                stub = test_pb2_grpc.TestServiceStub(channel)
                call = stub.StreamingOutputCall(request)

                validation = _inject_callbacks(call)

                async for response in call:
                    pass

                await validation

                await channel.close()

    async def test_add_done_callback_after_connection(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWith_ResponseIterator):

            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()

                request = messages_pb2.StreamingOutputCallRequest()
                for _ in range(_NUM_STREAM_RESPONSES):
                    request.response_parameters.append(
                        messages_pb2.ResponseParameters(
                            size=_RESPONSE_PAYLOAD_SIZE))

                channel = aio.insecure_channel(self._server_target,
                                               interceptors=[interceptor])
                stub = test_pb2_grpc.TestServiceStub(channel)
                call = stub.StreamingOutputCall(request)

                # This ensures that the callbacks will be registered
                # with the intercepted call rather than saving in the
                # pending state list.
                await call.wait_for_connection()

                validation = _inject_callbacks(call)

                async for response in call:
                    pass

                await validation

                await channel.close()

    async def test_response_iterator_using_read(self):
        interceptor = _UnaryStreamInterceptorWith_ResponseIterator()

        channel = aio.insecure_channel(self._server_target,
                                       interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        call = stub.StreamingOutputCall(request)

        response_cnt = 0
        for response in range(_NUM_STREAM_RESPONSES):
            response = await call.read()
            response_cnt += 1
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertTrue(response_cnt, _NUM_STREAM_RESPONSES)
        self.assertTrue(interceptor.response_iterator.response_cnt,
                        _NUM_STREAM_RESPONSES)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        await channel.close()

    async def test_mulitple_interceptors_response_iterator(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWith_ResponseIterator):

            with self.subTest(name=interceptor_class):

                interceptors = [interceptor_class(), interceptor_class()]

                channel = aio.insecure_channel(self._server_target,
                                               interceptors=interceptors)
                stub = test_pb2_grpc.TestServiceStub(channel)

                request = messages_pb2.StreamingOutputCallRequest()
                for _ in range(_NUM_STREAM_RESPONSES):
                    request.response_parameters.append(
                        messages_pb2.ResponseParameters(
                            size=_RESPONSE_PAYLOAD_SIZE))

                call = stub.StreamingOutputCall(request)

                response_cnt = 0
                async for response in call:
                    response_cnt += 1
                    self.assertIs(type(response),
                                  messages_pb2.StreamingOutputCallResponse)
                    self.assertEqual(_RESPONSE_PAYLOAD_SIZE,
                                     len(response.payload.body))

                self.assertTrue(response_cnt, _NUM_STREAM_RESPONSES)
                self.assertEqual(await call.code(), grpc.StatusCode.OK)

                await channel.close()

    async def test_intercepts_response_iterator_rpc_error(self):
        for interceptor_class in (_UnaryStreamInterceptorEmpty,
                                  _UnaryStreamInterceptorWith_ResponseIterator):

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
        self.assertEqual(await call.details(),
                         _LOCAL_CANCEL_DETAILS_EXPECTATION)
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
        self.assertEqual(await call.details(),
                         _LOCAL_CANCEL_DETAILS_EXPECTATION)
        self.assertEqual(await call.initial_metadata(), None)
        self.assertEqual(await call.trailing_metadata(), None)
        await channel.close()

    async def test_cancel_consuming_response_iterator(self):
        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                    interval_us=_RESPONSE_INTERVAL_US))

        channel = aio.insecure_channel(
            self._server_target,
            interceptors=[_UnaryStreamInterceptorWith_ResponseIterator()])
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        with self.assertRaises(asyncio.CancelledError):
            async for response in call:
                call.cancel()

        self.assertTrue(call.cancelled())
        self.assertTrue(call.done())
        self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
        self.assertEqual(await call.details(),
                         _LOCAL_CANCEL_DETAILS_EXPECTATION)
        await channel.close()


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
