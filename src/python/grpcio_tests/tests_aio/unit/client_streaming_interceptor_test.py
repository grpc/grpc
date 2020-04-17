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
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc

_SHORT_TIMEOUT_S = datetime.timedelta(seconds=1).total_seconds()

_LOCAL_CANCEL_DETAILS_EXPECTATION = 'Locally cancelled by application!'
_NUM_STREAM_RESPONSES = 5
_REQUEST_PAYLOAD_SIZE = 7
_RESPONSE_PAYLOAD_SIZE = 7
_RESPONSE_INTERVAL_US = int(_SHORT_TIMEOUT_S * 1000 * 1000)


class ResponseIterator:
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


class RequestIterator:
    def __init__(self, resquest_iterator):
        self._request_cnt = 0
        self._request_iterator = request_iterator

    def _forward_requests(self):
        for request in self._request_iterator:
            self._request_cnt += 1
            yield request

    def __iter__(self):
        return self._forward_requests()

    @property
    def request_cnt(self):
        return self._request_cnt


class TestUnaryStreamClientInterceptor(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_none_intercepts_response_iterator(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                return await continuation(client_call_details, request)

        interceptor = Interceptor()

        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        channel = aio.insecure_channel(self._server_target, interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)
        call = stub.StreamingOutputCall(request)

        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertTrue(response_cnt, _NUM_STREAM_RESPONSES)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        await channel.close()

    async def test_raises_exception(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                raise Exception()

        interceptor = Interceptor()

        channel = aio.insecure_channel(self._server_target, interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        call = stub.StreamingOutputCall(request)

        with self.assertRaises(Exception):
            async for response in call:
                pass

        await channel.close()

    async def test_intercepts_response_iterator(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            def __init__(self):
                self.response_iterator = None

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                self.response_iterator = ResponseIterator(call)
                return self.response_iterator

        interceptor = Interceptor()

        channel = aio.insecure_channel(self._server_target, interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        call = stub.StreamingOutputCall(request)

        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertTrue(response_cnt, _NUM_STREAM_RESPONSES)
        self.assertTrue(interceptor.response_iterator.response_cnt, _NUM_STREAM_RESPONSES)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        await channel.close()

    async def test_intercepts_response_iterator_using_read(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            def __init__(self):
                self.response_iterator = None

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                self.response_iterator = ResponseIterator(call)
                return self.response_iterator

        interceptor = Interceptor()

        channel = aio.insecure_channel(self._server_target, interceptors=[interceptor])
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
        self.assertTrue(interceptor.response_iterator.response_cnt, _NUM_STREAM_RESPONSES)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        await channel.close()

    async def test_mulitple_interceptors_response_iterator(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            def __init__(self):
                self.response_iterator = None

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                self.response_iterator = ResponseIterator(call)
                return self.response_iterator

        interceptors = [Interceptor(), Interceptor()]

        channel = aio.insecure_channel(self._server_target, interceptors=interceptors)
        stub = test_pb2_grpc.TestServiceStub(channel)

        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        call = stub.StreamingOutputCall(request)

        response_cnt = 0
        async for response in call:
            response_cnt += 1
            self.assertIs(type(response),
                          messages_pb2.StreamingOutputCallResponse)
            self.assertEqual(_RESPONSE_PAYLOAD_SIZE, len(response.payload.body))

        self.assertTrue(response_cnt, _NUM_STREAM_RESPONSES)
        for interceptor in interceptors:
            self.assertTrue(interceptor.response_iterator.response_cnt, _NUM_STREAM_RESPONSES)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        await channel.close()

    async def test_observes_status_code(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            def __init__(self):
                self.status_code = None

            async def gather_status_code(self, call):

                async for response in call:
                    yield response

                self.status_code = await call.code()

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                return self.gather_status_code(call)

        interceptor = Interceptor()

        channel = aio.insecure_channel(self._server_target, interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        call = stub.StreamingOutputCall(request)

        async for response in call:
            pass

        self.assertEqual(await call.code(), grpc.StatusCode.OK)
        self.assertEqual(interceptor.status_code, grpc.StatusCode.OK)

        await channel.close()

    async def test_intercepts_response_iterator_rpc_error(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            def __init__(self):
                self.response_iterator = None

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                self.response_iterator = ResponseIterator(call)
                return self.response_iterator

        channel = aio.insecure_channel(UNREACHABLE_TARGET, interceptors=[Interceptor()])
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

            def __init__(self):
                self.response_iterator = None

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                interceptor_reached.set()
                await wait_for_ever

        channel = aio.insecure_channel(UNREACHABLE_TARGET, interceptors=[Interceptor()])
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

            def __init__(self):
                self.response_iterator = None

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                interceptor_reached.set()
                await wait_for_ever

        channel = aio.insecure_channel(UNREACHABLE_TARGET, interceptors=[Interceptor()])
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

    async def test_cancel_consuming_reponse_iterator(self):

        class Interceptor(aio.UnaryStreamClientInterceptor):

            def __init__(self):
                self.response_iterator = None

            async def intercept_unary_stream(self, continuation,
                                             client_call_details, request):
                call = await continuation(client_call_details, request)
                self.response_iterator = ResponseIterator(call)
                return self.response_iterator

        request = messages_pb2.StreamingOutputCallRequest()
        for _ in range(_NUM_STREAM_RESPONSES):
            request.response_parameters.append(
                messages_pb2.ResponseParameters(
                    size=_RESPONSE_PAYLOAD_SIZE,
                    interval_us=_RESPONSE_INTERVAL_US
                )
            )

        channel = aio.insecure_channel(self._server_target, interceptors=[Interceptor()])
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
        self.assertEqual(await call.initial_metadata(), None)
        self.assertEqual(await call.trailing_metadata(), None)
        await channel.close()


@unittest.skip("TODO")
class TestStreamUnaryClientInterceptor(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_intercepts_request_iterator(self):

        class Interceptor(aio.StreamUnaryClientInterceptor):
            def __init__(self):
                self._request_iterator = None

            async def intercept_stream_unary(self, continuation,
                                            client_call_details, request_iterator):
                proxied_request_iterator = RequestItearor(request_iterator)
                call = await continuation(client_call_details, proxied_request_iterator)
                return call

            @property
            def request_interceptor(self):
                return self._request_iterator

        interceptor = Interceptor()

        channel = aio.insecure_channel(self._server_target, interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        # Prepares the request
        payload = messages_pb2.Payload(body=b'\0' * _REQUEST_PAYLOAD_SIZE)
        request = messages_pb2.StreamingInputCallRequest(payload=payload)

        async def gen():
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request

        call = stub.StreamingInputCall(gen())
        await call

        self.assertEqual(await call.code(), grpc.StatusCode.OK)

        # Validates that all messages have been intercepted
        self.assertTrue(interceptor.request_iterator.response_cnt, _NUM_STREAM_RESPONSES)

        await channel.close()


@unittest.skip("TODO")
class TestStreamStreamClientInterceptor(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_intercepts_request_response_iterator(self):

        class Interceptor(aio.StreamStreamClientInterceptor):

            def __init__(self):
                self._request_iterator = None
                self._response_iterator = None

            async def intercept_stream_stream(self, continuation,
                                             client_call_details,
                                             request_iterator,
                                             response_iterator):
                proxied_request_iterator = RequestIterator(request_iterator)
                proxied_response_iterator = ResponseIterator(response_iterator)
                call = await continuation(
                    client_call_details, 
                    proxied_request_iterator,
                    proxied_response_iterator
                )
                return call

            @property
            def request_interceptor(self):
                return self._request_iterator

            @property
            def response_interceptor(self):
                return self._response_iterator

        interceptor = Interceptor()

        channel = aio.insecure_channel(self._server_target, interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        # Prepares the request
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.append(
            messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE))

        async def gen():
            for _ in range(_NUM_STREAM_RESPONSES):
                yield request

        call = stub.FullDuplexCall(gen())

        async for response in call:
            pass

        self.assertEqual(grpc.StatusCode.OK, await call.code())
 
        # Validates that all messages have been intercepted
        self.assertTrue(interceptor.request_iterator.response_cnt, _NUM_STREAM_RESPONSES)
        self.assertTrue(interceptor.response_iterator.response_cnt, _NUM_STREAM_RESPONSES)

        await channel.close()


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
