# Copyright 2020 The gRPC authors.
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
"""Tests for Simple Stubs."""

import unittest
import sys

import logging

import grpc
import test_common


_UNARY_UNARY = "/test/UnaryUnary"


def _unary_unary_handler(request, context):
    return request


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return grpc.unary_unary_rpc_method_handler(_unary_unary_handler)
        else:
            raise NotImplementedError()


@unittest.skipIf(sys.version_info[0] < 3, "Unsupported on Python 2.")
class SimpleStubsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        super(SimpleStubsTest, cls).setUpClass()
        cls._server = test_common.test_server()
        cls._port = cls._server.add_insecure_port('[::]:0')
        cls._server.add_generic_rpc_handlers((_GenericHandler(),))
        cls._server.start()

    @classmethod
    def tearDownClass(cls):
        cls._server.stop(None)
        super(SimpleStubsTest, cls).tearDownClass()

    def test_unary_unary(self):
        target = f'localhost:{self._port}'
        request = b'0000'
        response = grpc.unary_unary(request, target, _UNARY_UNARY)
        self.assertEqual(request, response)

if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)


