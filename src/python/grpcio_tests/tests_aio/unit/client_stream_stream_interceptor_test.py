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
import logging
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests_aio.unit._common import CountingRequestIterator
from tests_aio.unit._common import CountingResponseIterator
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_NUM_STREAM_RESPONSES = 5
_NUM_STREAM_REQUESTS = 5
_RESPONSE_PAYLOAD_SIZE = 7


class _StreamStreamInterceptorEmpty(aio.StreamStreamClientInterceptor):
    async def intercept_stream_stream(
        self, continuation, client_call_details, request_iterator
    ):
        return await continuation(client_call_details, request_iterator)

    def assert_in_final_state(self, test: unittest.TestCase):
        pass


class _StreamStreamInterceptorWithRequestAndResponseIterator(
    aio.StreamStreamClientInterceptor
):
    async def intercept_stream_stream(
        self, continuation, client_call_details, request_iterator
    ):
        self.request_iterator = CountingRequestIterator(request_iterator)
        call = await continuation(client_call_details, self.request_iterator)
        self.response_iterator = CountingResponseIterator(call)
        return self.response_iterator

    def assert_in_final_state(self, test: unittest.TestCase):
        test.assertEqual(
            _NUM_STREAM_REQUESTS, self.request_iterator.request_cnt
        )
        test.assertEqual(
            _NUM_STREAM_RESPONSES, self.response_iterator.response_cnt
        )


class TestStreamStreamClientInterceptor(AioTestBase):
    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_intercepts(self):
        for interceptor_class in (
            _StreamStreamInterceptorEmpty,
            _StreamStreamInterceptorWithRequestAndResponseIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()
                channel = aio.insecure_channel(
                    self._server_target, interceptors=[interceptor]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                # Prepares the request
                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
                )

                async def request_iterator():
                    for _ in range(_NUM_STREAM_REQUESTS):
                        yield request

                call = stub.FullDuplexCall(request_iterator())

                await call.wait_for_connection()

                response_cnt = 0
                async for response in call:
                    response_cnt += 1
                    self.assertIsInstance(
                        response, messages_pb2.StreamingOutputCallResponse
                    )
                    self.assertEqual(
                        _RESPONSE_PAYLOAD_SIZE, len(response.payload.body)
                    )

                self.assertEqual(response_cnt, _NUM_STREAM_RESPONSES)
                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), aio.Metadata())
                self.assertEqual(await call.trailing_metadata(), aio.Metadata())
                self.assertEqual(await call.details(), "")
                self.assertEqual(await call.debug_error_string(), "")
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                interceptor.assert_in_final_state(self)

                await channel.close()

    async def test_intercepts_using_write_and_read(self):
        for interceptor_class in (
            _StreamStreamInterceptorEmpty,
            _StreamStreamInterceptorWithRequestAndResponseIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptor = interceptor_class()
                channel = aio.insecure_channel(
                    self._server_target, interceptors=[interceptor]
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                # Prepares the request
                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
                )

                call = stub.FullDuplexCall()

                for _ in range(_NUM_STREAM_RESPONSES):
                    await call.write(request)
                    response = await call.read()
                    self.assertIsInstance(
                        response, messages_pb2.StreamingOutputCallResponse
                    )
                    self.assertEqual(
                        _RESPONSE_PAYLOAD_SIZE, len(response.payload.body)
                    )

                await call.done_writing()

                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), aio.Metadata())
                self.assertEqual(await call.trailing_metadata(), aio.Metadata())
                self.assertEqual(await call.details(), "")
                self.assertEqual(await call.debug_error_string(), "")
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                interceptor.assert_in_final_state(self)

                await channel.close()

    async def test_multiple_interceptors_request_iterator(self):
        for interceptor_class in (
            _StreamStreamInterceptorEmpty,
            _StreamStreamInterceptorWithRequestAndResponseIterator,
        ):
            with self.subTest(name=interceptor_class):
                interceptors = [interceptor_class(), interceptor_class()]
                channel = aio.insecure_channel(
                    self._server_target, interceptors=interceptors
                )
                stub = test_pb2_grpc.TestServiceStub(channel)

                # Prepares the request
                request = messages_pb2.StreamingOutputCallRequest()
                request.response_parameters.append(
                    messages_pb2.ResponseParameters(size=_RESPONSE_PAYLOAD_SIZE)
                )

                call = stub.FullDuplexCall()

                for _ in range(_NUM_STREAM_RESPONSES):
                    await call.write(request)
                    response = await call.read()
                    self.assertIsInstance(
                        response, messages_pb2.StreamingOutputCallResponse
                    )
                    self.assertEqual(
                        _RESPONSE_PAYLOAD_SIZE, len(response.payload.body)
                    )

                await call.done_writing()

                self.assertEqual(await call.code(), grpc.StatusCode.OK)
                self.assertEqual(await call.initial_metadata(), aio.Metadata())
                self.assertEqual(await call.trailing_metadata(), aio.Metadata())
                self.assertEqual(await call.details(), "")
                self.assertEqual(await call.debug_error_string(), "")
                self.assertEqual(call.cancel(), False)
                self.assertEqual(call.cancelled(), False)
                self.assertEqual(call.done(), True)

                for interceptor in interceptors:
                    interceptor.assert_in_final_state(self)

                await channel.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
