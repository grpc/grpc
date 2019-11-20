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
import collections
import logging
import unittest

import grpc

from grpc.experimental import aio
from tests_aio.unit._test_server import start_test_server
from tests_aio.unit._test_base import AioTestBase
from src.proto.grpc.testing import messages_pb2


class TestUnaryUnaryClientInterceptor(AioTestBase):

    def test_invalid(self):

        class InvalidInterceptor:
            """Just an invalid Interceptor"""

        with self.assertRaises(ValueError):
            aio.insecure_channel("", interceptors=[InvalidInterceptor()])

    def test_executed_right_order(self):

        interceptors_executed = []

        class Interceptor(aio.UnaryUnaryClientInterceptor):
            """Interceptor used for testing if the interceptor is being called"""

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                interceptors_executed.append(self)
                return await continuation(client_call_details, request)

        async def coro():
            interceptors = [Interceptor() for i in range(2)]

            server_target, _ = await start_test_server()  # pylint: disable=unused-variable

            async with aio.insecure_channel(
                    server_target, interceptors=interceptors) as channel:
                multicallable = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )
                response = await multicallable(messages_pb2.SimpleRequest())

                # Check that all interceptors were executed, and were executed
                # in the right order.
                self.assertEqual(interceptors_executed, interceptors)

                self.assertEqual(type(response), messages_pb2.SimpleResponse)

        self.loop.run_until_complete(coro())

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

    def test_status_code_observability(self):

        class StatusCodeObservabilityInterceptor(
                aio.UnaryUnaryClientInterceptor):
            """Interceptor used for observe the status code returned by the RPC"""

            def __init__(self):
                self.status_codes_observed = {
                    grpc.StatusCode.OK: 0,
                    grpc.StatusCode.CANCELLED: 0,
                    grpc.StatusCode.DEADLINE_EXCEEDED: 0
                }

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                status_code = grpc.StatusCode.OK
                try:
                    return await continuation(client_call_details, request)
                except aio.AioRpcError as aio_rpc_error:
                    status_code = aio_rpc_error.code()
                    raise
                except asyncio.CancelledError:
                    status_code = grpc.StatusCode.CANCELLED
                    raise
                finally:
                    self.status_codes_observed[status_code] += 1

        async def coro():
            interceptor = StatusCodeObservabilityInterceptor()
            server_target, server = await start_test_server()

            async with aio.insecure_channel(
                    server_target, interceptors=[interceptor]) as channel:

                # when no error StatusCode.OK is observed
                multicallable = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )

                await multicallable(messages_pb2.SimpleRequest())

                self.assertEqual(
                    interceptor.status_codes_observed[grpc.StatusCode.OK], 1)
                self.assertEqual(interceptor.status_codes_observed[
                    grpc.StatusCode.CANCELLED], 0)
                self.assertEqual(interceptor.status_codes_observed[
                    grpc.StatusCode.DEADLINE_EXCEEDED], 0)

                # when cancellation StatusCode.CANCELLED is observed
                call = multicallable(messages_pb2.SimpleRequest())

                await asyncio.sleep(0)

                call.cancel()

                try:
                    await call
                except asyncio.CancelledError:
                    pass

                self.assertEqual(
                    interceptor.status_codes_observed[grpc.StatusCode.OK], 1)
                self.assertEqual(interceptor.status_codes_observed[
                    grpc.StatusCode.CANCELLED], 1)
                self.assertEqual(interceptor.status_codes_observed[
                    grpc.StatusCode.DEADLINE_EXCEEDED], 0)

                # when deadline happens StatusCode.DEADLINE_EXCEEDED is observed
                await server.stop(None)

                empty_call_with_sleep = channel.unary_unary(
                    "/grpc.testing.TestService/EmptyCall",
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.
                    FromString,
                )

                try:
                    await empty_call_with_sleep(
                        messages_pb2.SimpleRequest(), timeout=0.1)
                except grpc.RpcError:
                    pass

                self.assertEqual(
                    interceptor.status_codes_observed[grpc.StatusCode.OK], 1)
                self.assertEqual(interceptor.status_codes_observed[
                    grpc.StatusCode.CANCELLED], 1)
                self.assertEqual(interceptor.status_codes_observed[
                    grpc.StatusCode.DEADLINE_EXCEEDED], 1)

        self.loop.run_until_complete(coro())

    def test_add_timeout(self):

        class TimeoutInterceptor(aio.UnaryUnaryClientInterceptor):
            """Interceptor used for adding a timeout to the RPC"""

            async def intercept_unary_unary(self, continuation,
                                            client_call_details, request):
                new_client_call_details = aio.ClientCallDetails(
                    method=client_call_details.method,
                    timeout=0.1,
                    metadata=client_call_details.metadata,
                    credentials=client_call_details.credentials)
                return await continuation(new_client_call_details, request)

        async def coro():
            interceptor = TimeoutInterceptor()
            server_target, server = await start_test_server()

            async with aio.insecure_channel(
                    server_target, interceptors=[interceptor]) as channel:

                multicallable = channel.unary_unary(
                    '/grpc.testing.TestService/UnaryCall',
                    request_serializer=messages_pb2.SimpleRequest.
                    SerializeToString,
                    response_deserializer=messages_pb2.SimpleResponse.FromString
                )

                await server.stop(None)

                with self.assertRaises(grpc.RpcError) as exception_context:
                    await multicallable(messages_pb2.SimpleRequest())

                self.assertEqual(exception_context.exception.code(),
                                 grpc.StatusCode.DEADLINE_EXCEEDED)

        self.loop.run_until_complete(coro())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
