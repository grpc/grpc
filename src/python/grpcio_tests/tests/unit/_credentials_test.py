# Copyright 2016 gRPC authors.
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
"""Tests of credentials."""

import logging
import pickle
import unittest

import grpc

from tests.unit import resources
from tests.unit import test_common


_SERVER_HOST_OVERRIDE = "foo.test.google.fr"
_SERVER_CERTS = ((resources.private_key(), resources.certificate_chain()),)
_TEST_ROOT_CERTIFICATES = resources.test_root_certificates()
_CHANNEL_OPTIONS = (("grpc.ssl_target_name_override", _SERVER_HOST_OVERRIDE),)
_SERVICE_NAME = "test"
_METHOD_NAME = "UnaryUnary"
_REQUEST = b"request"


def _handle_unary_unary(request, servicer_context):
    del request
    return pickle.dumps(servicer_context.auth_context())


_METHOD_HANDLERS = {
    _METHOD_NAME: grpc.unary_unary_rpc_method_handler(_handle_unary_unary)
}


class CredentialsTest(unittest.TestCase):
    def test_call_credentials_composition(self):
        first = grpc.access_token_call_credentials("abc")
        second = grpc.access_token_call_credentials("def")
        third = grpc.access_token_call_credentials("ghi")

        first_and_second = grpc.composite_call_credentials(first, second)
        first_second_and_third = grpc.composite_call_credentials(
            first, second, third
        )

        self.assertIsInstance(first_and_second, grpc.CallCredentials)
        self.assertIsInstance(first_second_and_third, grpc.CallCredentials)

    def test_channel_credentials_composition(self):
        first_call_credentials = grpc.access_token_call_credentials("abc")
        second_call_credentials = grpc.access_token_call_credentials("def")
        third_call_credentials = grpc.access_token_call_credentials("ghi")
        channel_credentials = grpc.ssl_channel_credentials()

        channel_and_first = grpc.composite_channel_credentials(
            channel_credentials, first_call_credentials
        )
        channel_first_and_second = grpc.composite_channel_credentials(
            channel_credentials, first_call_credentials, second_call_credentials
        )
        channel_first_second_and_third = grpc.composite_channel_credentials(
            channel_credentials,
            first_call_credentials,
            second_call_credentials,
            third_call_credentials,
        )

        self.assertIsInstance(channel_and_first, grpc.ChannelCredentials)
        self.assertIsInstance(channel_first_and_second, grpc.ChannelCredentials)
        self.assertIsInstance(
            channel_first_second_and_third, grpc.ChannelCredentials
        )

    def test_invalid_string_certificate(self):
        self.assertRaises(
            TypeError,
            grpc.ssl_channel_credentials,
            root_certificates="A Certificate",
            private_key=None,
            certificate_chain=None,
        )

    def test_tls_version_values(self):
        self.assertSequenceEqual(
            (grpc.TLSVersion.TLS1_2, grpc.TLSVersion.TLS1_3),
            tuple(grpc.TLSVersion),
        )

    def test_tls_versions_create_channel_credentials(self):
        credentials = grpc.ssl_channel_credentials(
            minimum_tls_version=grpc.TLSVersion.TLS1_2,
            maximum_tls_version=grpc.TLSVersion.TLS1_3,
        )
        self.assertIsInstance(credentials, grpc.ChannelCredentials)

    def test_tls_versions_create_server_credentials(self):
        for versions in (
            {"minimum_tls_version": grpc.TLSVersion.TLS1_3},
            {"maximum_tls_version": grpc.TLSVersion.TLS1_2},
            {
                "minimum_tls_version": grpc.TLSVersion.TLS1_2,
                "maximum_tls_version": grpc.TLSVersion.TLS1_3,
            },
        ):
            with self.subTest(versions=versions):
                credentials = grpc.ssl_server_credentials(
                    _SERVER_CERTS, **versions
                )
                self.assertIsInstance(credentials, grpc.ServerCredentials)

    def test_tls_versions_create_server_credentials_with_multiple_certificates(
        self,
    ):
        credentials = grpc.ssl_server_credentials(
            _SERVER_CERTS + _SERVER_CERTS,
            minimum_tls_version=grpc.TLSVersion.TLS1_2,
            maximum_tls_version=grpc.TLSVersion.TLS1_3,
        )
        self.assertIsInstance(credentials, grpc.ServerCredentials)

    def test_invalid_tls_version_types(self):
        for credential_factory in (
            grpc.ssl_channel_credentials,
            lambda **kwargs: grpc.ssl_server_credentials(
                _SERVER_CERTS, **kwargs
            ),
        ):
            for argument in ("minimum_tls_version", "maximum_tls_version"):
                with self.subTest(
                    credential_factory=credential_factory, argument=argument
                ):
                    with self.assertRaises(TypeError):
                        credential_factory(**{argument: "TLS1_2"})

    def test_minimum_tls_version_greater_than_maximum(self):
        for credential_factory in (
            grpc.ssl_channel_credentials,
            lambda **kwargs: grpc.ssl_server_credentials(
                _SERVER_CERTS, **kwargs
            ),
        ):
            with self.subTest(credential_factory=credential_factory):
                with self.assertRaises(ValueError):
                    credential_factory(
                        minimum_tls_version=grpc.TLSVersion.TLS1_3,
                        maximum_tls_version=grpc.TLSVersion.TLS1_2,
                    )

    def _perform_tls_rpc(
        self,
        client_minimum,
        client_maximum,
        server_minimum,
        server_maximum,
        require_client_auth=False,
    ):
        server = test_common.test_server()
        server.add_registered_method_handlers(_SERVICE_NAME, _METHOD_HANDLERS)
        server_credentials = grpc.ssl_server_credentials(
            _SERVER_CERTS,
            root_certificates=(
                _TEST_ROOT_CERTIFICATES if require_client_auth else None
            ),
            require_client_auth=require_client_auth,
            minimum_tls_version=server_minimum,
            maximum_tls_version=server_maximum,
        )
        port = server.add_secure_port("[::]:0", server_credentials)
        server.start()
        channel_credentials = grpc.ssl_channel_credentials(
            root_certificates=_TEST_ROOT_CERTIFICATES,
            private_key=(
                resources.private_key() if require_client_auth else None
            ),
            certificate_chain=(
                resources.certificate_chain() if require_client_auth else None
            ),
            minimum_tls_version=client_minimum,
            maximum_tls_version=client_maximum,
        )
        channel = grpc.secure_channel(
            "localhost:{}".format(port),
            channel_credentials,
            options=_CHANNEL_OPTIONS,
        )
        try:
            response = channel.unary_unary(
                grpc._common.fully_qualified_method(
                    _SERVICE_NAME, _METHOD_NAME
                ),
                _registered_method=True,
            )(_REQUEST, timeout=5)
            return pickle.loads(response)
        finally:
            channel.close()
            server.stop(None)

    def _assert_tls_handshake_succeeds(
        self,
        client_minimum,
        client_maximum,
        server_minimum,
        server_maximum,
        require_client_auth=False,
    ):
        auth_context = self._perform_tls_rpc(
            client_minimum,
            client_maximum,
            server_minimum,
            server_maximum,
            require_client_auth=require_client_auth,
        )
        self.assertSequenceEqual(
            [b"tls"], auth_context["transport_security_type"]
        )
        return auth_context

    def test_tls_1_2_only_handshake_succeeds(self):
        self._assert_tls_handshake_succeeds(
            grpc.TLSVersion.TLS1_2,
            grpc.TLSVersion.TLS1_2,
            grpc.TLSVersion.TLS1_2,
            grpc.TLSVersion.TLS1_2,
        )

    def test_tls_1_3_only_handshake_succeeds(self):
        self._assert_tls_handshake_succeeds(
            grpc.TLSVersion.TLS1_3,
            grpc.TLSVersion.TLS1_3,
            grpc.TLSVersion.TLS1_3,
            grpc.TLSVersion.TLS1_3,
        )

    def test_one_sided_tls_version_bounds_succeed(self):
        self._assert_tls_handshake_succeeds(
            None,
            grpc.TLSVersion.TLS1_3,
            grpc.TLSVersion.TLS1_3,
            None,
        )

    def test_non_overlapping_tls_versions_fail(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._perform_tls_rpc(
                grpc.TLSVersion.TLS1_3,
                grpc.TLSVersion.TLS1_3,
                grpc.TLSVersion.TLS1_2,
                grpc.TLSVersion.TLS1_2,
            )
        self.assertEqual(
            grpc.StatusCode.UNAVAILABLE, exception_context.exception.code()
        )

    def test_tls_version_bounds_preserve_mutual_tls(self):
        auth_context = self._assert_tls_handshake_succeeds(
            grpc.TLSVersion.TLS1_3,
            grpc.TLSVersion.TLS1_3,
            grpc.TLSVersion.TLS1_3,
            grpc.TLSVersion.TLS1_3,
            require_client_auth=True,
        )
        self.assertSequenceEqual(
            [b"*.test.google.com"], auth_context["x509_common_name"]
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
