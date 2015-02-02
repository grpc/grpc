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

"""Tests for _adapter._c."""

import threading
import time
import unittest

from _adapter import _c
from _adapter import _datatypes

_TIMEOUT = 3
_FUTURE = time.time() + 60 * 60 * 24
_IDEMPOTENCE_DEMONSTRATION = 7


class _CTest(unittest.TestCase):

  def testUpAndDown(self):
    _c.init()
    _c.shut_down()

  def testCompletionQueue(self):
    _c.init()

    completion_queue = _c.CompletionQueue()
    event = completion_queue.get(0)
    self.assertIsNone(event)
    event = completion_queue.get(time.time())
    self.assertIsNone(event)
    event = completion_queue.get(time.time() + _TIMEOUT)
    self.assertIsNone(event)
    completion_queue.stop()
    for _ in range(_IDEMPOTENCE_DEMONSTRATION):
      event = completion_queue.get(time.time() + _TIMEOUT)
      self.assertIs(event.kind, _datatypes.Event.Kind.STOP)

    del completion_queue
    del event

    _c.shut_down()

  def testChannel(self):
    _c.init()

    channel = _c.Channel('test host:12345')
    del channel

    _c.shut_down()

  def testCall(self):
    method = 'test method'
    host = 'test host'

    _c.init()

    channel = _c.Channel('%s:%d' % (host, 12345))
    call = _c.Call(channel, method, host, time.time() + _TIMEOUT)
    del call
    del channel

    _c.shut_down()

  def testServer(self):
    _c.init()

    completion_queue = _c.CompletionQueue()
    server = _c.Server(completion_queue)
    server.add_http2_addr('[::]:0')
    server.start()
    server.stop()
    completion_queue.stop()
    del server
    del completion_queue

    service_tag = object()
    completion_queue = _c.CompletionQueue()
    server = _c.Server(completion_queue)
    server.add_http2_addr('[::]:0')
    server.start()
    server.service(service_tag)
    server.stop()
    completion_queue.stop()
    event = completion_queue.get(time.time() + _TIMEOUT)
    self.assertIs(event.kind, _datatypes.Event.Kind.SERVICE_ACCEPTED)
    self.assertIs(event.tag, service_tag)
    self.assertIsNone(event.service_acceptance)
    for _ in range(_IDEMPOTENCE_DEMONSTRATION):
      event = completion_queue.get(time.time() + _TIMEOUT)
      self.assertIs(event.kind, _datatypes.Event.Kind.STOP)
    del server
    del completion_queue

    completion_queue = _c.CompletionQueue()
    server = _c.Server(completion_queue)
    server.add_http2_addr('[::]:0')
    server.start()
    thread = threading.Thread(target=completion_queue.get, args=(_FUTURE,))
    thread.start()
    time.sleep(1)
    server.stop()
    completion_queue.stop()
    for _ in range(_IDEMPOTENCE_DEMONSTRATION):
      event = completion_queue.get(time.time() + _TIMEOUT)
      self.assertIs(event.kind, _datatypes.Event.Kind.STOP)
    thread.join()
    del server
    del completion_queue

    _c.shut_down()


if __name__ == '__main__':
  unittest.main()
