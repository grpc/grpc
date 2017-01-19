# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Tests the implementations module of the gRPC Python Beta API."""

import datetime
import unittest

from oauth2client import client as oauth2client_client

from grpc.beta import implementations
from tests.unit import resources


class ChannelCredentialsTest(unittest.TestCase):

    def test_runtime_provided_root_certificates(self):
        channel_credentials = implementations.ssl_channel_credentials()
        self.assertIsInstance(channel_credentials,
                              implementations.ChannelCredentials)

    def test_application_provided_root_certificates(self):
        channel_credentials = implementations.ssl_channel_credentials(
            resources.test_root_certificates())
        self.assertIsInstance(channel_credentials,
                              implementations.ChannelCredentials)


class CallCredentialsTest(unittest.TestCase):

    def test_google_call_credentials(self):
        creds = oauth2client_client.GoogleCredentials(
            'token', 'client_id', 'secret', 'refresh_token',
            datetime.datetime(2008, 6, 24), 'https://refresh.uri.com/',
            'user_agent')
        call_creds = implementations.google_call_credentials(creds)
        self.assertIsInstance(call_creds, implementations.CallCredentials)

    def test_access_token_call_credentials(self):
        call_creds = implementations.access_token_call_credentials('token')
        self.assertIsInstance(call_creds, implementations.CallCredentials)


if __name__ == '__main__':
    unittest.main(verbosity=2)
