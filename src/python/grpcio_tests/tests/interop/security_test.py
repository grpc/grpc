import sys
import unittest

import grpc
import grpc.experimental

from src.proto.grpc.testing import test_pb2_grpc
from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from tests.interop import _intraop_test_case
from tests.interop import resources
from tests.interop import service
from tests.interop import methods
from tests.unit import test_common
# from grpc._cython import cygrpc as _cygrpc
import time
import faulthandler
from functools import partial
import threading
import weakref

faulthandler.enable()

_SERVER_HOST_OVERRIDE = "foo.test.google.fr"

class SecurityTest(unittest.TestCase):
    """setUp and tearDown start and stop a test server with valid server credentials.
    This server can be called by each `def test_*` function in this class.
    Each test tests a variety of different security configurations."""
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

    def test_success_sync(self):
        """
        Successfully use a custom sync private key signer.
        """
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.experimental.ssl_channel_credentials_with_custom_signer(
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
        """
        Successfully use a custom async private key signer.
        """
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.experimental.ssl_channel_credentials_with_custom_signer(
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

    def test_bad_sync_signer(self):
        """
        Expect failure using a custom sync private key signer.
        """
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.experimental.ssl_channel_credentials_with_custom_signer(
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

    def test_bad_async_signer(self):
        """
        Expect failure using a custom async private key signer.
        """
        self.stub = test_pb2_grpc.TestServiceStub(
            grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.experimental.ssl_channel_credentials_with_custom_signer(
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
        """
        Test cancellation of an async signer
        """
        # Create a handle with a cancellation event and pass it to the signing function..
        # test_handle = grpc.create_async_handle_for_custom_signer()
        # test_handle.cancel_event = threading.Event()
        # test_handle.handshake_started = threading.Event()
        cancel_callable = resources.CancelCallable()
        bound_signing_fn = partial(
            resources.async_signer_with_cancel_injection, cancel_callable
        )
        channel = grpc.secure_channel(
            "localhost:{}".format(self.port),
            grpc.experimental.ssl_channel_credentials_with_custom_signer(
                private_key_sign_fn=bound_signing_fn,
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
        self.stub = test_pb2_grpc.TestServiceStub(channel)
        future = self.stub.EmptyCall.future(empty_pb2.Empty())
        # Let it get into the handshake where it should loop infinitely
        self.assertTrue(cancel_callable.handshake_started_event.wait(timeout=1))
        # Ensure it's not cancelled yet
        self.assertFalse(cancel_callable.cancel_event.is_set())
        # Cancel
        future.cancel()
        channel.close()
        # Ensure the cancel event is set
        self.assertTrue(cancel_callable.cancel_event.wait(timeout=1))

    def test_async_signer_test_times_out(self):
        """
        Similar to the test where we manually cancel, but just let things timeout
        """
        # We should have a timeout here
        with self.assertRaises(Exception) as context:
            with grpc.secure_channel(
                "localhost:{}".format(self.port),
                grpc.experimental.ssl_channel_credentials_with_custom_signer(
                    private_key_sign_fn=resources.async_client_private_key_signer_with_cancel,
                    root_certificates=resources.test_root_certificates(),
                    certificate_chain=resources.client_certificate_chain(),
                ),
                (
                    (
                        "grpc.ssl_target_name_override",
                        _SERVER_HOST_OVERRIDE,
                    ),
                ),
            ) as channel:
                self.stub = test_pb2_grpc.TestServiceStub(channel)
                # Let it timeout and just go out of scope
                response = self.stub.EmptyCall(empty_pb2.Empty(), timeout=1)
                # As everything goes out of scope, we just want to make sure we don't segfault or anything

    def test_signer_lifetime(self):
      class TrackedSigner:

        def __call__(self, data, algo, cb):
          return b"signature"

      def create_channel():
        signer = TrackedSigner()
        ref = weakref.ref(signer)
        creds = grpc.ssl_channel_credentials(
          private_key_signer=signer,
          root_certificates=resources.test_root_certificates(),
          certificate_chain=resources.client_certificate_chain().client_certificate_chain(),
        )

        secure_channel = grpc.secure_channel("localhost:12345", creds)
        return secure_channel, ref

      channel, signer_ref = create_channel()

      self.assertIsNotNone(signer_ref(),
                           "Signer was garbage collected prematurely!")

if __name__ == "__main__":
    unittest.main(verbosity=2)
