# Copyright 2015 gRPC authors.
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
