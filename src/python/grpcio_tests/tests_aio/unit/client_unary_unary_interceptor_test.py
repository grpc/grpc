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

import grpc

from grpc.experimental import aio
from tests_aio.unit._test_server import start_test_server, _INITIAL_METADATA_KEY, _TRAILING_METADATA_KEY
from tests_aio.unit import _constants
from tests_aio.unit import _common
from tests_aio.unit._test_base import AioTestBase
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc

_LOCAL_CANCEL_DETAILS_EXPECTATION = 'Locally cancelled by application!'
_INITIAL_METADATA_TO_INJECT = aio.Metadata(
    (_INITIAL_METADATA_KEY, 'extra info'),
    (_TRAILING_METADATA_KEY, b'\x13\x37'),
)
_TIMEOUT_CHECK_IF_CALLBACK_WAS_CALLED = 1.0


class TestUnaryUnaryClientInterceptor(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    def test_invalid_interceptor(self):

        class InvalidInterceptor:
            """Just an invalid Interceptor"""

        with self.assertRaises(ValueError):
            aio.insecure_channel("", interceptors=[InvalidInterceptor()])

    async def test_executed_right_order(self):

        interceptors_executed = []

        class Interceptor(aio.UnaryUnaryClientInterceptor):
            """Interceptor used for testing if the interceptor is being called"""

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                interceptors_executed.append(self)
                call = await continuation(client_call_details, request)
                return call

        interceptors = [Interceptor() for i in range(2)]

        async with aio.insecure_channel(self._server_target,
                                        interceptors=interceptors) as channel:
            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())
            response = await call

            # Check that all interceptors were executed, and were executed
            # in the right order.
            self.assertSequenceEqual(interceptors_executed, interceptors)

            self.assertIsInstance(response, messages_pb2.SimpleResponse)

    @unittest.expectedFailure
    # TODO(https://github.com/grpc/grpc/issues/20144) Once metadata support is
    # implemented in the client-side, this test must be implemented.
    def test_modify_metadata(self):
        raise NotImplementedError()

    @unittest.expectedFailure
    # TODO(https://github.com/grpc/grpc/issues/20532) Once credentials support is
    # implemented in the client-side, this test must be implemented.
    def test_modify_credentials(self):
        raise NotImplementedError()

    async def test_status_code_Ok(self):

        class StatusCodeOkInterceptor(aio.UnaryUnaryClientInterceptor):
            """Interceptor used for observing status code Ok returned by the RPC"""

            def __init__(self):
                self.status_code_Ok_observed = False

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                code = await call.code()
                if code == grpc.StatusCode.OK:
                    self.status_code_Ok_observed = True

                return call

        interceptor = StatusCodeOkInterceptor()

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[interceptor]) as channel:

            # when no error StatusCode.OK must be observed
            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)

            await multicallable(messages_pb2.SimpleRequest())

            self.assertTrue(interceptor.status_code_Ok_observed)

    async def test_add_timeout(self):

        class TimeoutInterceptor(aio.UnaryUnaryClientInterceptor):
            """Interceptor used for adding a timeout to the RPC"""

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                new_client_call_details = aio.ClientCallDetails(
                    method=client_call_details.method,
                    timeout=_constants.UNARY_CALL_WITH_SLEEP_VALUE / 2,
                    metadata=client_call_details.metadata,
                    credentials=client_call_details.credentials,
                    wait_for_ready=client_call_details.wait_for_ready)
                return await continuation(new_client_call_details, request)

        interceptor = TimeoutInterceptor()

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[interceptor]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCallWithSleep',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)

            call = multicallable(messages_pb2.SimpleRequest())

            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call

            self.assertEqual(exception_context.exception.code(),
                             grpc.StatusCode.DEADLINE_EXCEEDED)

            self.assertTrue(call.done())
            self.assertEqual(grpc.StatusCode.DEADLINE_EXCEEDED, await
                             call.code())

    async def test_retry(self):

        class RetryInterceptor(aio.UnaryUnaryClientInterceptor):
            """Simulates a Retry Interceptor which ends up by making
            two RPC calls."""

            def __init__(self):
                self.calls = []

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):

                new_client_call_details = aio.ClientCallDetails(
                    method=client_call_details.method,
                    timeout=_constants.UNARY_CALL_WITH_SLEEP_VALUE / 2,
                    metadata=client_call_details.metadata,
                    credentials=client_call_details.credentials,
                    wait_for_ready=client_call_details.wait_for_ready)

                try:
                    call = await continuation(new_client_call_details, request)
                    await call
                except grpc.RpcError:
                    pass

                self.calls.append(call)

                new_client_call_details = aio.ClientCallDetails(
                    method=client_call_details.method,
                    timeout=None,
                    metadata=client_call_details.metadata,
                    credentials=client_call_details.credentials,
                    wait_for_ready=client_call_details.wait_for_ready)

                call = await continuation(new_client_call_details, request)
                self.calls.append(call)
                return call

        interceptor = RetryInterceptor()

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[interceptor]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCallWithSleep',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)

            call = multicallable(messages_pb2.SimpleRequest())

            await call

            self.assertEqual(grpc.StatusCode.OK, await call.code())

            # Check that two calls were made, first one finishing with
            # a deadline and second one finishing ok..
            self.assertEqual(len(interceptor.calls), 2)
            self.assertEqual(await interceptor.calls[0].code(),
                             grpc.StatusCode.DEADLINE_EXCEEDED)
            self.assertEqual(await interceptor.calls[1].code(),
                             grpc.StatusCode.OK)

    async def test_rpcresponse(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):
            """Raw responses are seen as reegular calls"""

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                response = await call
                return call

        class ResponseInterceptor(aio.UnaryUnaryClientInterceptor):
            """Return a raw response"""
            response = messages_pb2.SimpleResponse()

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                return ResponseInterceptor.response

        interceptor, interceptor_response = Interceptor(), ResponseInterceptor()

        async with aio.insecure_channel(
                self._server_target,
                interceptors=[interceptor, interceptor_response]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)

            call = multicallable(messages_pb2.SimpleRequest())
            response = await call

            # Check that the response returned is the one returned by the
            # interceptor
            self.assertEqual(id(response), id(ResponseInterceptor.response))

            # Check all of the UnaryUnaryCallResponse attributes
            self.assertTrue(call.done())
            self.assertFalse(call.cancel())
            self.assertFalse(call.cancelled())
            self.assertEqual(await call.code(), grpc.StatusCode.OK)
            self.assertEqual(await call.details(), '')
            self.assertEqual(await call.initial_metadata(), None)
            self.assertEqual(await call.trailing_metadata(), None)
            self.assertEqual(await call.debug_error_string(), None)


class TestInterceptedUnaryUnaryCall(AioTestBase):

    async def setUp(self):
        self._server_target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_call_ok(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())
            response = await call

            self.assertTrue(call.done())
            self.assertFalse(call.cancelled())
            self.assertEqual(type(response), messages_pb2.SimpleResponse)
            self.assertEqual(await call.code(), grpc.StatusCode.OK)
            self.assertEqual(await call.details(), '')
            self.assertEqual(await call.initial_metadata(), aio.Metadata())
            self.assertEqual(await call.trailing_metadata(), aio.Metadata())

    async def test_call_ok_awaited(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                await call
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())
            response = await call

            self.assertTrue(call.done())
            self.assertFalse(call.cancelled())
            self.assertEqual(type(response), messages_pb2.SimpleResponse)
            self.assertEqual(await call.code(), grpc.StatusCode.OK)
            self.assertEqual(await call.details(), '')
            self.assertEqual(await call.initial_metadata(), aio.Metadata())
            self.assertEqual(await call.trailing_metadata(), aio.Metadata())

    async def test_call_rpc_error(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCallWithSleep',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)

            call = multicallable(
                messages_pb2.SimpleRequest(),
                timeout=_constants.UNARY_CALL_WITH_SLEEP_VALUE / 2)

            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call

            self.assertTrue(call.done())
            self.assertFalse(call.cancelled())
            self.assertEqual(await call.code(),
                             grpc.StatusCode.DEADLINE_EXCEEDED)
            self.assertEqual(await call.details(), 'Deadline Exceeded')
            self.assertEqual(await call.initial_metadata(), aio.Metadata())
            self.assertEqual(await call.trailing_metadata(), aio.Metadata())

    async def test_call_rpc_error_awaited(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                await call
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCallWithSleep',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)

            call = multicallable(
                messages_pb2.SimpleRequest(),
                timeout=_constants.UNARY_CALL_WITH_SLEEP_VALUE / 2)

            with self.assertRaises(aio.AioRpcError) as exception_context:
                await call

            self.assertTrue(call.done())
            self.assertFalse(call.cancelled())
            self.assertEqual(await call.code(),
                             grpc.StatusCode.DEADLINE_EXCEEDED)
            self.assertEqual(await call.details(), 'Deadline Exceeded')
            self.assertEqual(await call.initial_metadata(), aio.Metadata())
            self.assertEqual(await call.trailing_metadata(), aio.Metadata())

    async def test_cancel_before_rpc(self):

        interceptor_reached = asyncio.Event()
        wait_for_ever = self.loop.create_future()

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                interceptor_reached.set()
                await wait_for_ever

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())

            self.assertFalse(call.cancelled())
            self.assertFalse(call.done())

            await interceptor_reached.wait()
            self.assertTrue(call.cancel())

            with self.assertRaises(asyncio.CancelledError):
                await call

            self.assertTrue(call.cancelled())
            self.assertTrue(call.done())
            self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
            self.assertEqual(await call.details(),
                             _LOCAL_CANCEL_DETAILS_EXPECTATION)
            self.assertEqual(await call.initial_metadata(), None)
            self.assertEqual(await call.trailing_metadata(), None)

    async def test_cancel_after_rpc(self):

        interceptor_reached = asyncio.Event()
        wait_for_ever = self.loop.create_future()

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                await call
                interceptor_reached.set()
                await wait_for_ever

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())

            self.assertFalse(call.cancelled())
            self.assertFalse(call.done())

            await interceptor_reached.wait()
            self.assertTrue(call.cancel())

            with self.assertRaises(asyncio.CancelledError):
                await call

            self.assertTrue(call.cancelled())
            self.assertTrue(call.done())
            self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
            self.assertEqual(await call.details(),
                             _LOCAL_CANCEL_DETAILS_EXPECTATION)
            self.assertEqual(await call.initial_metadata(), None)
            self.assertEqual(await call.trailing_metadata(), None)

    async def test_cancel_inside_interceptor_after_rpc_awaiting(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                call.cancel()
                await call
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())

            with self.assertRaises(asyncio.CancelledError):
                await call

            self.assertTrue(call.cancelled())
            self.assertTrue(call.done())
            self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
            self.assertEqual(await call.details(),
                             _LOCAL_CANCEL_DETAILS_EXPECTATION)
            self.assertEqual(await call.initial_metadata(), None)
            self.assertEqual(await call.trailing_metadata(), None)

    async def test_cancel_inside_interceptor_after_rpc_not_awaiting(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                call = await continuation(client_call_details, request)
                call.cancel()
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())

            with self.assertRaises(asyncio.CancelledError):
                await call

            self.assertTrue(call.cancelled())
            self.assertTrue(call.done())
            self.assertEqual(await call.code(), grpc.StatusCode.CANCELLED)
            self.assertEqual(await call.details(),
                             _LOCAL_CANCEL_DETAILS_EXPECTATION)
            self.assertEqual(await call.initial_metadata(), aio.Metadata())
            self.assertEqual(
                await call.trailing_metadata(), aio.Metadata(),
                "When the raw response is None, empty metadata is returned")

    async def test_initial_metadata_modification(self):

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                new_metadata = aio.Metadata(*client_call_details.metadata,
                                            *_INITIAL_METADATA_TO_INJECT)
                new_details = aio.ClientCallDetails(
                    method=client_call_details.method,
                    timeout=client_call_details.timeout,
                    metadata=new_metadata,
                    credentials=client_call_details.credentials,
                    wait_for_ready=client_call_details.wait_for_ready,
                )
                return await continuation(new_details, request)

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            call = stub.UnaryCall(messages_pb2.SimpleRequest())

            # Expected to see the echoed initial metadata
            self.assertTrue(
                _common.seen_metadatum(
                    expected_key=_INITIAL_METADATA_KEY,
                    expected_value=_INITIAL_METADATA_TO_INJECT[
                        _INITIAL_METADATA_KEY],
                    actual=await call.initial_metadata(),
                ))
            # Expected to see the echoed trailing metadata
            self.assertTrue(
                _common.seen_metadatum(
                    expected_key=_TRAILING_METADATA_KEY,
                    expected_value=_INITIAL_METADATA_TO_INJECT[
                        _TRAILING_METADATA_KEY],
                    actual=await call.trailing_metadata(),
                ))
            self.assertEqual(await call.code(), grpc.StatusCode.OK)

    async def test_add_done_callback_before_finishes(self):
        called = asyncio.Event()
        interceptor_can_continue = asyncio.Event()

        def callback(call):
            called.set()

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):

                await interceptor_can_continue.wait()
                call = await continuation(client_call_details, request)
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())
            call.add_done_callback(callback)
            interceptor_can_continue.set()
            await call

            try:
                await asyncio.wait_for(
                    called.wait(),
                    timeout=_TIMEOUT_CHECK_IF_CALLBACK_WAS_CALLED)
            except:
                self.fail("Callback was not called")

    async def test_add_done_callback_after_finishes(self):
        called = asyncio.Event()

        def callback(call):
            called.set()

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):

                call = await continuation(client_call_details, request)
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())

            await call

            call.add_done_callback(callback)

            try:
                await asyncio.wait_for(
                    called.wait(),
                    timeout=_TIMEOUT_CHECK_IF_CALLBACK_WAS_CALLED)
            except:
                self.fail("Callback was not called")

    async def test_add_done_callback_after_finishes_before_await(self):
        called = asyncio.Event()

        def callback(call):
            called.set()

        class Interceptor(aio.UnaryUnaryClientInterceptor):

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):

                call = await continuation(client_call_details, request)
                return call

        async with aio.insecure_channel(self._server_target,
                                        interceptors=[Interceptor()
                                                     ]) as channel:

            multicallable = channel.unary_unary(
                '/grpc.testing.TestService/UnaryCall',
                request_serializer=messages_pb2.SimpleRequest.SerializeToString,
                response_deserializer=messages_pb2.SimpleResponse.FromString)
            call = multicallable(messages_pb2.SimpleRequest())

            call.add_done_callback(callback)

            await call

            try:
                await asyncio.wait_for(
                    called.wait(),
                    timeout=_TIMEOUT_CHECK_IF_CALLBACK_WAS_CALLED)
            except:
                self.fail("Callback was not called")


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
