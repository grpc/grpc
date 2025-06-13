import logging
import unittest

import grpc
from grpc.experimental import aio

from src.proto.grpc.testing import messages_pb2 
from src.proto.grpc.testing import test_pb2_grpc
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server

_NUM_STREAM_REQUESTS = 3
_NUM_STREAM_RESPONSES = 3


class MultiTypeInterceptor(
    aio.UnaryUnaryClientInterceptor,
    aio.UnaryStreamClientInterceptor,
    aio.StreamUnaryClientInterceptor,
    aio.StreamStreamClientInterceptor,
):
    def __init__(self):
        self.calls = []

    async def intercept_unary_unary(self, continuation, client_call_details, request):
        self.calls.append("unary_unary")
        return await continuation(client_call_details, request)

    async def intercept_unary_stream(self, continuation, client_call_details, request):
        self.calls.append("unary_stream")
        return await continuation(client_call_details, request)

    async def intercept_stream_unary(self, continuation, client_call_details, request_iterator):
        self.calls.append("stream_unary")
        return await continuation(client_call_details, request_iterator)

    async def intercept_stream_stream(self, continuation, client_call_details, request_iterator):
        self.calls.append("stream_stream")
        return await continuation(client_call_details, request_iterator)

    def assert_hooks_invoked(self, test: unittest.TestCase):
        for method in ("unary_unary", "unary_stream", "stream_unary", "stream_stream"):
            test.assertIn(method, self.calls,
                          f"Expected interceptor '{method}' to be invoked")


class TestMultiTypeInterceptor(AioTestBase):
    async def setUp(self):
        self._target, self._server = await start_test_server()

    async def tearDown(self):
        await self._server.stop(None)

    async def test_multi_type_interceptor(self):
        interceptor = MultiTypeInterceptor()
        channel = aio.insecure_channel(self._target, interceptors=[interceptor])
        stub = test_pb2_grpc.TestServiceStub(channel)

        #Unary-Unary
        await stub.UnaryCall(messages_pb2.SimpleRequest())

        #Unary-Stream
        request = messages_pb2.StreamingOutputCallRequest()
        request.response_parameters.extend(
            [messages_pb2.ResponseParameters(size=0)] * _NUM_STREAM_RESPONSES
        )
        call = stub.StreamingOutputCall(request)
        await call.wait_for_connection()
        response_cnt = 0
        async for _ in call:
            response_cnt += 1
        self.assertEqual(response_cnt, _NUM_STREAM_RESPONSES)

        #Stream-Unary
        request_count = 0
        async def request_iterator_su():
            nonlocal request_count
            for _ in range(_NUM_STREAM_REQUESTS):
                request_count += 1
                yield messages_pb2.StreamingInputCallRequest()
        await stub.StreamingInputCall(request_iterator_su())
        self.assertEqual(request_count, _NUM_STREAM_REQUESTS)

        # Stream-Stream
        template = messages_pb2.StreamingOutputCallRequest()
        template.response_parameters.extend(
            [messages_pb2.ResponseParameters(size=0)] * _NUM_STREAM_RESPONSES
        )

        ss_request_count = 0
        ss_response_count = 0

        async def request_iterator_ss():
            nonlocal ss_request_count
            for _ in range(_NUM_STREAM_REQUESTS):
                ss_request_count += 1
                yield template

        bidi = stub.FullDuplexCall(request_iterator_ss())
        await bidi.wait_for_connection()

        async for _ in bidi:
            ss_response_count += 1
        self.assertEqual(ss_request_count, _NUM_STREAM_REQUESTS)
        self.assertEqual(
            ss_response_count,
            _NUM_STREAM_REQUESTS * _NUM_STREAM_RESPONSES,
            f"Expected {_NUM_STREAM_REQUESTS*_NUM_STREAM_RESPONSES} total stream-stream responses"
        )

        interceptor.assert_hooks_invoked(self)
        await channel.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)

