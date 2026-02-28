# Copyright 2026 gRPC authors.
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

import faulthandler
from functools import partial
import sys
import threading

import time
import unittest
import weakref

import grpc
import grpc.experimental

from src.proto.grpc.testing import empty_pb2
from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc
from tests.interop import _intraop_test_case
from tests.interop import methods
from tests.interop import resources
from tests.interop import service
from tests.unit import test_common

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
                [
                    (
                        resources.server_private_key(),
                        resources.certificate_chain(),
                    )
                ],
                resources.test_root_certificates(),
                require_client_auth=True,
            ),
        )
        self.server.start()

    def tearDown(self):
        stopped = self.server.stop(None)
        done = stopped.wait(timeout=10)
        self.assertTrue(done)

    def test_success_sync(self):
        """
        Successfully use a custom sync private key signer.
        """
        with grpc.secure_channel(
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
        ) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            response = stub.EmptyCall(empty_pb2.Empty())
            self.assertIsInstance(response, empty_pb2.Empty)

    def test_success_async(self):
        """
        Successfully use a custom async private key signer.
        """
        with grpc.secure_channel(
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
        ) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            response = stub.EmptyCall(empty_pb2.Empty())
            self.assertIsInstance(response, empty_pb2.Empty)

    def test_bad_sync_signer(self):
        """
        Expect failure using a custom sync private key signer.
        """
        with grpc.secure_channel(
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
        ) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            with self.assertRaises(Exception) as context:
                stub.EmptyCall(empty_pb2.Empty())
        self.assertIsNotNone(context.exception)

    def test_bad_async_signer(self):
        """
        Expect failure using a custom async private key signer.
        """
        with grpc.secure_channel(
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
        ) as channel:
            stub = test_pb2_grpc.TestServiceStub(channel)
            with self.assertRaises(Exception) as context:
                stub.EmptyCall(empty_pb2.Empty())
        self.assertIsNotNone(context.exception)

    def test_async_signer_with_cancel(self):
        """
        Test cancellation of an async signer
        """
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
        stub = test_pb2_grpc.TestServiceStub(channel)
        future = stub.EmptyCall.future(empty_pb2.Empty())
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
                stub = test_pb2_grpc.TestServiceStub(channel)
                # Let it timeout and just go out of scope
                response = stub.EmptyCall(empty_pb2.Empty(), timeout=1)
                # As everything goes out of scope, we just want to make sure we don't segfault or anything

    def test_signer_lifetime(self):

        class TrackedSigner:

            def __call__(self, data, algo, cb):
                return b"signature"

        def create_channel():
            signer = TrackedSigner()
            ref = weakref.ref(signer)
            creds = (
                grpc.experimental.ssl_channel_credentials_with_custom_signer(
                    private_key_sign_fn=signer,
                    root_certificates=resources.test_root_certificates(),
                    certificate_chain=resources.client_certificate_chain(),
                )
            )

            secure_channel = grpc.secure_channel(
                "localhost:{}".format(self.port), creds
            )
            return secure_channel, ref

        channel, signer_ref = create_channel()

        self.assertIsNotNone(
            signer_ref(), "Signer was garbage collected prematurely!"
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
