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
"""Tests of standard AuthMetadataPlugins."""

import collections
import threading
import unittest

from grpc import _auth


class MockGoogleCreds(object):

    def get_access_token(self):
        token = collections.namedtuple('MockAccessTokenInfo',
                                       ('access_token', 'expires_in'))
        token.access_token = 'token'
        return token


class MockExceptionGoogleCreds(object):

    def get_access_token(self):
        raise Exception()


class GoogleCallCredentialsTest(unittest.TestCase):

    def test_google_call_credentials_success(self):
        callback_event = threading.Event()

        def mock_callback(metadata, error):
            self.assertEqual(metadata, (('authorization', 'Bearer token'),))
            self.assertIsNone(error)
            callback_event.set()

        call_creds = _auth.GoogleCallCredentials(MockGoogleCreds())
        call_creds(None, mock_callback)
        self.assertTrue(callback_event.wait(1.0))

    def test_google_call_credentials_error(self):
        callback_event = threading.Event()

        def mock_callback(metadata, error):
            self.assertIsNotNone(error)
            callback_event.set()

        call_creds = _auth.GoogleCallCredentials(MockExceptionGoogleCreds())
        call_creds(None, mock_callback)
        self.assertTrue(callback_event.wait(1.0))


class AccessTokenCallCredentialsTest(unittest.TestCase):

    def test_google_call_credentials_success(self):
        callback_event = threading.Event()

        def mock_callback(metadata, error):
            self.assertEqual(metadata, (('authorization', 'Bearer token'),))
            self.assertIsNone(error)
            callback_event.set()

        call_creds = _auth.AccessTokenCallCredentials('token')
        call_creds(None, mock_callback)
        self.assertTrue(callback_event.wait(1.0))


if __name__ == '__main__':
    unittest.main(verbosity=2)
