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
"""Tests that a channel will reconnect if a connection is dropped"""

import unittest

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import test_constants

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x01'

_UNARY_UNARY = '/test/UnaryUnary'


def _handle_unary_unary(unused_request, unused_servicer_context):
    return _RESPONSE


class ReconnectTest(unittest.TestCase):

    def test_reconnect(self):
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        handler = grpc.method_handlers_generic_handler('test', {
            'UnaryUnary':
            grpc.unary_unary_rpc_method_handler(_handle_unary_unary)
        })
        server = grpc.server(server_pool, (handler,))
        port = server.add_insecure_port('[::]:0')
        server.start()
        channel = grpc.insecure_channel('localhost:%d' % port)
        multi_callable = channel.unary_unary(_UNARY_UNARY)
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))
        server.stop(None)
        server = grpc.server(server_pool, (handler,))
        server.add_insecure_port('[::]:{}'.format(port))
        server.start()
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))


if __name__ == '__main__':
    unittest.main(verbosity=2)
