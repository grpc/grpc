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
"""Tests the implementations module of the gRPC Python Beta API."""

import datetime
import unittest

from grpc.beta import implementations
from oauth2client import client as oauth2client_client

from tests.unit import resources


class ChannelCredentialsTest(unittest.TestCase):
    def test_runtime_provided_root_certificates(self):
        channel_credentials = implementations.ssl_channel_credentials()
        self.assertIsInstance(
            channel_credentials, implementations.ChannelCredentials
        )

    def test_application_provided_root_certificates(self):
        channel_credentials = implementations.ssl_channel_credentials(
            resources.test_root_certificates()
        )
        self.assertIsInstance(
            channel_credentials, implementations.ChannelCredentials
        )


class CallCredentialsTest(unittest.TestCase):
    def test_google_call_credentials(self):
        creds = oauth2client_client.GoogleCredentials(
            "token",
            "client_id",
            "secret",
            "refresh_token",
            datetime.datetime(2008, 6, 24),
            "https://refresh.uri.com/",
            "user_agent",
        )
        call_creds = implementations.google_call_credentials(creds)
        self.assertIsInstance(call_creds, implementations.CallCredentials)

    def test_access_token_call_credentials(self):
        call_creds = implementations.access_token_call_credentials("token")
        self.assertIsInstance(call_creds, implementations.CallCredentials)


if __name__ == "__main__":
    unittest.main(verbosity=2)
