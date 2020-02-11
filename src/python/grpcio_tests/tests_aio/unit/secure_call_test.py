import unittest
import logging

import grpc
from grpc.experimental import aio
from src.proto.grpc.testing import messages_pb2, test_pb2_grpc
from tests_aio.unit._test_base import AioTestBase
from tests_aio.unit._test_server import start_test_server
from tests.unit import resources

_SERVER_HOST_OVERRIDE = 'foo.test.google.fr'


class _SecureCallMixin:
    """A Mixin to run the call tests over a secure channel."""

    async def setUp(self):
        server_credentials = grpc.ssl_server_credentials([
            (resources.private_key(), resources.certificate_chain())
        ])
        channel_credentials = grpc.ssl_channel_credentials(
            resources.test_root_certificates())

        self._server_address, self._server = await start_test_server(
            secure=True, server_credentials=server_credentials)
        channel_options = ((
            'grpc.ssl_target_name_override',
            _SERVER_HOST_OVERRIDE,
        ),)
        self._channel = aio.secure_channel(self._server_address,
                                           channel_credentials, channel_options)
        self._stub = test_pb2_grpc.TestServiceStub(self._channel)

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)


class TestUnaryUnarySecureCall(_SecureCallMixin, AioTestBase):
    """Calls made over a secure channel."""

    async def test_call_ok_with_credentials(self):
        call = self._stub.UnaryCall(messages_pb2.SimpleRequest())
        response = await call
        self.assertIsInstance(response, messages_pb2.SimpleResponse)
        self.assertEqual(await call.code(), grpc.StatusCode.OK)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
