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

    def test_with_cert_but_no_key(self):
        with self.assertRaisesRegex(ValueError, "private_key must be provided"):
            grpc.ssl_channel_credentials(certificate_chain=b"cert")

    def test_with_key_but_no_cert(self):
        with self.assertRaisesRegex(
            ValueError, "certificate_chain must be provided"
        ):
            grpc.ssl_channel_credentials(private_key=b"key")

    def test_with_key_but_empty_cert(self):
        with self.assertRaisesRegex(
            ValueError, "certificate_chain must be provided"
        ):
            grpc.ssl_channel_credentials(
                certificate_chain=b"", private_key=b"key"
            )

    def test_with_cert_but_empty_key(self):
        with self.assertRaisesRegex(ValueError, "private_key must be provided"):
            grpc.ssl_channel_credentials(
                certificate_chain=b"cert", private_key=b""
            )

    def test_default_none_credentials(self):
        creds = grpc.ssl_channel_credentials()
        self.assertIsInstance(creds, grpc.ChannelCredentials)

    def test_empty_key_and_cert(self):
        creds = grpc.ssl_channel_credentials(
            certificate_chain=b"", private_key=b""
        )
        self.assertIsInstance(creds, grpc.ChannelCredentials)

    def test_empty_cert_and_none_key(self):
        creds = grpc.ssl_channel_credentials(
            certificate_chain=b"", private_key=None
        )
        self.assertIsInstance(creds, grpc.ChannelCredentials)

    def test_none_cert_and_empty_key(self):
        creds = grpc.ssl_channel_credentials(
            certificate_chain=None, private_key=b""
        )
        self.assertIsInstance(creds, grpc.ChannelCredentials)

    def test_provided_key_and_cert(self):
        creds = grpc.ssl_channel_credentials(
            private_key=b"fake_private_key",
            certificate_chain=b"fake_certificate_chain",
        )
        self.assertIsInstance(creds, grpc.ChannelCredentials)

    def test_only_root_certificates(self):
        creds = grpc.ssl_channel_credentials(
            root_certificates=b"fake_root_certs"
        )
        self.assertIsInstance(creds, grpc.ChannelCredentials)

    def test_all_parameters_provided(self):
        creds = grpc.ssl_channel_credentials(
            root_certificates=b"fake_root_certs",
            private_key=b"fake_private_key",
            certificate_chain=b"fake_certificate_chain",
        )
        self.assertIsInstance(creds, grpc.ChannelCredentials)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
