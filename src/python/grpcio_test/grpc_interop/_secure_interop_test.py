# Copyright 2015, Google Inc.
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

"""Secure client-server interoperability as a unit test."""

import unittest

from grpc.beta import implementations

from grpc_test.beta import test_utilities

from grpc_interop import _interop_test_case
from grpc_interop import methods
from grpc_interop import resources
from grpc_interop import test_pb2

_SERVER_HOST_OVERRIDE = 'foo.test.google.fr'


class SecureInteropTest(
    _interop_test_case.InteropTestCase,
    unittest.TestCase):

  def setUp(self):
    self.server = test_pb2.beta_create_TestService_server(methods.TestService())
    port = self.server.add_secure_port(
        '[::]:0', implementations.ssl_server_credentials(
            [(resources.private_key(), resources.certificate_chain())]))
    self.server.start()
    self.stub = test_pb2.beta_create_TestService_stub(
        test_utilities.not_really_secure_channel(
            '[::]', port, implementations.ssl_client_credentials(
                resources.test_root_certificates(), None, None),
                _SERVER_HOST_OVERRIDE))

  def tearDown(self):
    self.server.stop(0)


if __name__ == '__main__':
  unittest.main(verbosity=2)
