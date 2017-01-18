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
"""Tests of credentials."""

import unittest

import grpc


class CredentialsTest(unittest.TestCase):

    def test_call_credentials_composition(self):
        first = grpc.access_token_call_credentials('abc')
        second = grpc.access_token_call_credentials('def')
        third = grpc.access_token_call_credentials('ghi')

        first_and_second = grpc.composite_call_credentials(first, second)
        first_second_and_third = grpc.composite_call_credentials(first, second,
                                                                 third)

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


if __name__ == '__main__':
    unittest.main(verbosity=2)
