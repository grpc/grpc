# Copyright 2017, Google Inc.
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
"""Tests exposure of SSL auth context"""

import pickle
import unittest

import grpc
from grpc import _channel
from grpc.framework.foundation import logging_pool
import six

from tests.unit import test_common
from tests.unit.framework.common import test_constants
from tests.unit import resources

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x00'

_UNARY_UNARY = '/test/UnaryUnary'

_SERVER_HOST_OVERRIDE = 'foo.test.google.fr'
_CLIENT_IDS = (b'*.test.google.fr', b'waterzooi.test.google.be',
               b'*.test.youtube.com', b'192.168.1.3',)
_ID = 'id'
_ID_KEY = 'id_key'
_AUTH_CTX = 'auth_ctx'

_PRIVATE_KEY = resources.private_key()
_CERTIFICATE_CHAIN = resources.certificate_chain()
_TEST_ROOT_CERTIFICATES = resources.test_root_certificates()
_SERVER_CERTS = ((_PRIVATE_KEY, _CERTIFICATE_CHAIN),)
_PROPERTY_OPTIONS = (('grpc.ssl_target_name_override', _SERVER_HOST_OVERRIDE,),)


def handle_unary_unary(request, servicer_context):
    return pickle.dumps({
        _ID: servicer_context.peer_identities(),
        _ID_KEY: servicer_context.peer_identity_key(),
        _AUTH_CTX: servicer_context.auth_context()
    })


class AuthContextTest(unittest.TestCase):

    def testInsecure(self):
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        handler = grpc.method_handlers_generic_handler('test', {
            'UnaryUnary':
            grpc.unary_unary_rpc_method_handler(handle_unary_unary)
        })
        server = grpc.server(server_pool, (handler,))
        port = server.add_insecure_port('[::]:0')
        server.start()

        channel = grpc.insecure_channel('localhost:%d' % port)
        response = channel.unary_unary(_UNARY_UNARY)(_REQUEST)
        server.stop(None)

        auth_data = pickle.loads(response)
        self.assertIsNone(auth_data[_ID])
        self.assertIsNone(auth_data[_ID_KEY])
        self.assertDictEqual({}, auth_data[_AUTH_CTX])

    def testSecureNoCert(self):
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        handler = grpc.method_handlers_generic_handler('test', {
            'UnaryUnary':
            grpc.unary_unary_rpc_method_handler(handle_unary_unary)
        })
        server = grpc.server(server_pool, (handler,))
        server_cred = grpc.ssl_server_credentials(_SERVER_CERTS)
        port = server.add_secure_port('[::]:0', server_cred)
        server.start()

        channel_creds = grpc.ssl_channel_credentials(
            root_certificates=_TEST_ROOT_CERTIFICATES)
        channel = grpc.secure_channel(
            'localhost:{}'.format(port),
            channel_creds,
            options=_PROPERTY_OPTIONS)
        response = channel.unary_unary(_UNARY_UNARY)(_REQUEST)
        server.stop(None)

        auth_data = pickle.loads(response)
        self.assertIsNone(auth_data[_ID])
        self.assertIsNone(auth_data[_ID_KEY])
        self.assertDictEqual({
            'transport_security_type': [b'ssl']
        }, auth_data[_AUTH_CTX])

    def testSecureClientCert(self):
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        handler = grpc.method_handlers_generic_handler('test', {
            'UnaryUnary':
            grpc.unary_unary_rpc_method_handler(handle_unary_unary)
        })
        server = grpc.server(server_pool, (handler,))
        server_cred = grpc.ssl_server_credentials(
            _SERVER_CERTS,
            root_certificates=_TEST_ROOT_CERTIFICATES,
            require_client_auth=True)
        port = server.add_secure_port('[::]:0', server_cred)
        server.start()

        channel_creds = grpc.ssl_channel_credentials(
            root_certificates=_TEST_ROOT_CERTIFICATES,
            private_key=_PRIVATE_KEY,
            certificate_chain=_CERTIFICATE_CHAIN)
        channel = grpc.secure_channel(
            'localhost:{}'.format(port),
            channel_creds,
            options=_PROPERTY_OPTIONS)

        response = channel.unary_unary(_UNARY_UNARY)(_REQUEST)
        server.stop(None)

        auth_data = pickle.loads(response)
        auth_ctx = auth_data[_AUTH_CTX]
        six.assertCountEqual(self, _CLIENT_IDS, auth_data[_ID])
        self.assertEqual('x509_subject_alternative_name', auth_data[_ID_KEY])
        self.assertSequenceEqual([b'ssl'], auth_ctx['transport_security_type'])
        self.assertSequenceEqual([b'*.test.google.com'],
                                 auth_ctx['x509_common_name'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
