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
"""Tests of RPC-method-not-found behavior."""

import unittest

from grpc.beta import implementations
from grpc.beta import interfaces
from grpc.framework.interfaces.face import face
from tests.unit.framework.common import test_constants


class NotFoundTest(unittest.TestCase):

    def setUp(self):
        self._server = implementations.server({})
        port = self._server.add_insecure_port('[::]:0')
        channel = implementations.insecure_channel('localhost', port)
        self._generic_stub = implementations.generic_stub(channel)
        self._server.start()

    def tearDown(self):
        self._server.stop(0).wait()
        self._generic_stub = None

    def test_blocking_unary_unary_not_found(self):
        with self.assertRaises(face.LocalError) as exception_assertion_context:
            self._generic_stub.blocking_unary_unary(
                'groop',
                'meffod',
                b'abc',
                test_constants.LONG_TIMEOUT,
                with_call=True)
        self.assertIs(exception_assertion_context.exception.code,
                      interfaces.StatusCode.UNIMPLEMENTED)

    def test_future_stream_unary_not_found(self):
        rpc_future = self._generic_stub.future_stream_unary(
            'grupe', 'mevvod', iter([b'def']), test_constants.LONG_TIMEOUT)
        with self.assertRaises(face.LocalError) as exception_assertion_context:
            rpc_future.result()
        self.assertIs(exception_assertion_context.exception.code,
                      interfaces.StatusCode.UNIMPLEMENTED)
        self.assertIs(rpc_future.exception().code,
                      interfaces.StatusCode.UNIMPLEMENTED)


if __name__ == '__main__':
    unittest.main(verbosity=2)
