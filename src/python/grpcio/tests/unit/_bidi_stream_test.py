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

import threading
import unittest

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import test_constants

_REQUEST = b'test_request'
_RESPONSE = b'test_response'

_STREAM_STREAM = b'/test/StreamStream'

# 25 Seems to pass????
_STREAM_COUNT = 26

def handle_stream_stream(request_iterator, servicer_context):
  for request in request_iterator:
    yield _RESPONSE


class StreamStreamHandler(grpc.RpcMethodHandler):

  def __init__(self):
    self.request_streaming = True
    self.response_streaming = True
    self.request_deserializer = None
    self.response_serializer = None
    self.unary_unary = None
    self.unary_stream = None
    self.stream_unary = None
    self.stream_stream = handle_stream_stream


class _GenericHandler(grpc.GenericRpcHandler):

  def service(self, handler_call_details):
    return StreamStreamHandler()


def unclosed_request_iterator(close_event):
  yield _REQUEST
  close_event.wait()


class EmptyMessageTest(unittest.TestCase):

  def setUp(self):
    self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
    self._server = grpc.server((_GenericHandler(),), self._server_pool)
    port = self._server.add_insecure_port('[::]:0')
    self._server.start()
    self._channel = grpc.insecure_channel('localhost:%d' % port)

  def tearDown(self):
    self._server.stop(0)

  @unittest.skip('https://github.com/grpc/grpc/issues/6825')
  def testUnclosedStreamStream(self):
    close_event = threading.Event()

    response_iterators = [None] * _STREAM_COUNT
    for i in range(_STREAM_COUNT):
      response_iterators[i] = self._channel.stream_stream(_STREAM_STREAM)(
        unclosed_request_iterator(close_event))
    
    for i in range(_STREAM_COUNT):
      self.assertEqual(_RESPONSE, next(response_iterators[i]))
    close_event.set()

if __name__ == '__main__':
  unittest.main(verbosity=2)

