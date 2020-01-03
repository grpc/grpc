# Copyright 2019 The gRPC Authors
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
"""Tests for an actual dns resolution."""

import unittest
import logging
import six

import grpc
from tests.unit import test_common
from tests.unit.framework.common import test_constants

_METHOD = '/ANY/METHOD'
_REQUEST = b'\x00\x00\x00'
_RESPONSE = _REQUEST


class GenericHandler(grpc.GenericRpcHandler):

    def service(self, unused_handler_details):
        return grpc.unary_unary_rpc_method_handler(
            lambda request, unused_context: request,
        )


class DNSResolverTest(unittest.TestCase):

    def setUp(self):
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers((GenericHandler(),))
        self._port = self._server.add_insecure_port('[::]:0')
        self._server.start()

    def tearDown(self):
        self._server.stop(None)

    def test_connect_loopback(self):
        # NOTE(https://github.com/grpc/grpc/issues/18422)
        # In short, Gevent + C-Ares = Segfault. The C-Ares driver is not
        # supported by custom io manager like "gevent" or "libuv".
        with grpc.insecure_channel('loopback4.unittest.grpc.io:%d' %
                                   self._port) as channel:
            self.assertEqual(
                channel.unary_unary(_METHOD)(
                    _REQUEST,
                    timeout=test_constants.SHORT_TIMEOUT,
                ), _RESPONSE)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
