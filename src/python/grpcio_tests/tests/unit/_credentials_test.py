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

import unittest
import logging
import six

import grpc


class CredentialsTest(unittest.TestCase):

    def test_call_credentials_composition(self):
        first = grpc.access_token_call_credentials('abc')
        second = grpc.access_token_call_credentials('def')
        third = grpc.access_token_call_credentials('ghi')

        first_and_second = grpc.composite_call_credentials(first, second)
        first_second_and_third = grpc.composite_call_credentials(
            first, second, third)

        self.assertIsInstance(first_and_second, grpc.CallCredentials)
        self.assertIsInstance(first_second_and_third, grpc.CallCredentials)

    def test_channel_credentials_composition(self):
        first_call_credentials = grpc.access_token_call_credentials('abc')
        second_call_credentials = grpc.access_token_call_credentials('def')
        third_call_credentials = grpc.access_token_call_credentials('ghi')
        channel_credentials = grpc.ssl_channel_credentials()

        channel_and_first = grpc.composite_channel_credentials(
            channel_credentials, first_call_credentials)
        channel_first_and_second = grpc.composite_channel_credentials(
            channel_credentials, first_call_credentials,
            second_call_credentials)
        channel_first_second_and_third = grpc.composite_channel_credentials(
            channel_credentials, first_call_credentials,
            second_call_credentials, third_call_credentials)

        self.assertIsInstance(channel_and_first, grpc.ChannelCredentials)
        self.assertIsInstance(channel_first_and_second, grpc.ChannelCredentials)
        self.assertIsInstance(channel_first_second_and_third,
                              grpc.ChannelCredentials)

    @unittest.skipIf(six.PY2, 'only invalid in Python3')
    def test_invalid_string_certificate(self):
        self.assertRaises(
            TypeError,
            grpc.ssl_channel_credentials,
            root_certificates='A Certificate',
            private_key=None,
            certificate_chain=None,
        )


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
