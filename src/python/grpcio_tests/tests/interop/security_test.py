import sys
import unittest

import grpc

from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from tests.interop import _intraop_test_case
from tests.interop import resources
from tests.interop import service
from tests.interop import methods
from tests.unit import test_common
from grpc._cython import cygrpc as _cygrpc
import time
import faulthandler
from functools import partial
import threading

faulthandler.enable()

_SERVER_HOST_OVERRIDE = "foo.test.google.fr"

class SecurityTest(unittest.TestCase):
    def setUp(self):
        self.server = test_common.test_server()
        test_pb2_grpc.add_TestServiceServicer_to_server(
            service.TestService(), self.server
        )
        # Configure the server for mTLS so the client will do Private Key signing
        self.port = self.server.add_secure_port(
            "[::]:0",
            grpc.ssl_server_credentials(
                [(resources.server_private_key(), resources.certificate_chain())],
                resources.test_root_certificates(),
                require_client_auth=True,
            ),
        )
        self.server.start()

    def tearDown(self):
        self.server.stop(None)

    # @unittest.skip(reason="temp")
    def test_success_sync(self):
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.ssl_channel_credentials_with_custom_signer(
                    private_key_sign_fn=resources.sync_client_private_key_signer,
                    root_certificates=resources.test_root_certificates(),
                    certificate_chain=resources.client_certificate_chain(),
                ),
                (
                    (
                        "grpc.ssl_target_name_override",
                        _SERVER_HOST_OVERRIDE,
                    ),
                ),
            )
        )
        response = self.stub.EmptyCall(empty_pb2.Empty())
        self.assertIsInstance(response, empty_pb2.Empty)

    def test_success_async(self):
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.ssl_channel_credentials_with_custom_signer(
                    private_key_sign_fn=resources.async_client_private_key_signer,
                    root_certificates=resources.test_root_certificates(),
                    certificate_chain=resources.client_certificate_chain(),
                ),
                (
                    (
                        "grpc.ssl_target_name_override",
                        _SERVER_HOST_OVERRIDE,
                    ),
                ),
            )
        )
        response = self.stub.EmptyCall(empty_pb2.Empty())
        self.assertIsInstance(response, empty_pb2.Empty)

    # @unittest.skip(reason="temp")
    def test_bad_sync_signer(self):
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.ssl_channel_credentials_with_custom_signer(
                    private_key_sign_fn=resources.sync_bad_client_private_key_signer,
                    root_certificates=resources.test_root_certificates(),
                    certificate_chain=resources.client_certificate_chain(),
                ),
                (
                    (
                        "grpc.ssl_target_name_override",
                        _SERVER_HOST_OVERRIDE,
                    ),
                ),
            )
        )
        with self.assertRaises(Exception) as context:
            response = self.stub.EmptyCall(empty_pb2.Empty())
            # Check result better

    # @unittest.skip(reason="temp")
    def test_bad_async_signer(self):
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.ssl_channel_credentials_with_custom_signer(
                    private_key_sign_fn=resources.bad_async_client_private_key_signer,
                    root_certificates=resources.test_root_certificates(),
                    certificate_chain=resources.client_certificate_chain(),
                ),
                (
                    (
                        "grpc.ssl_target_name_override",
                        _SERVER_HOST_OVERRIDE,
                    ),
                ),
            )
        )
        with self.assertRaises(Exception) as context:
            response = self.stub.EmptyCall(empty_pb2.Empty())
            # TODO check result better

    def test_async_signer_with_cancel(self):
        # test_handle = _cygrpc.create_async_signing_test_handle()
        test_handle = grpc.create_async_handle_for_custom_signer()
        test_handle.cancel_event = threading.Event()
        bound_signing_fn = partial(resources.async_signer_with_test_handle, test_handle)
        channel = grpc.secure_channel(
            "localhost:{}".format(self.port),
            grpc.ssl_channel_credentials_with_custom_signer_with_cancellation(
                private_key_sign_fn=bound_signing_fn,
                root_certificates=resources.test_root_certificates(),
                certificate_chain=resources.client_certificate_chain(),
                cancel_fn=resources.cancel_async,
            ),
            (
                (
                    "grpc.ssl_target_name_override",
                    _SERVER_HOST_OVERRIDE,
                ),
            ),
        )
        self.stub = test_pb2_grpc.TestServiceStub(channel)
        future = self.stub.EmptyCall.future(empty_pb2.Empty())
        # Let it get into the handshake
        time.sleep(1)
        self.assertFalse(test_handle.cancel_event.is_set())
        future.cancel()
        channel.close()
        # Wait until cancel_event is set with a timeout of 1 second for failure
        self.assertTrue(test_handle.cancel_event.wait(timeout=1))

if __name__ == "__main__":
    unittest.main(verbosity=2)
