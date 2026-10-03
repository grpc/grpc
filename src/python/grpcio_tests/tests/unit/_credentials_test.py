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
import unittest

import grpc


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

    def test_invalid_tls_version_types(self):
        for argument in ("minimum_tls_version", "maximum_tls_version"):
            with self.subTest(argument=argument):
                with self.assertRaises(TypeError):
                    grpc.ssl_channel_credentials(**{argument: "TLS1_2"})

    def test_minimum_tls_version_greater_than_maximum(self):
        with self.assertRaises(ValueError):
            grpc.ssl_channel_credentials(
                minimum_tls_version=grpc.TLSVersion.TLS1_3,
                maximum_tls_version=grpc.TLSVersion.TLS1_2,
            )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
