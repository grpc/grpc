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

from grpc._adapter import _c
from grpc._adapter import _datatypes

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

    channel = _c.Channel(
        'test host:12345', None, server_host_override='ignored')
    del channel

    _c.shut_down()

  def testCall(self):
    method = 'test method'
    host = 'test host'

    _c.init()

    channel = _c.Channel('%s:%d' % (host, 12345), None)
    completion_queue = _c.CompletionQueue()
    call = _c.Call(channel, completion_queue, method, host,
                   time.time() + _TIMEOUT)
    del call
    del completion_queue
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

  def test_client_credentials(self):
    root_certificates = b'Trust starts here. Really.'
    private_key = b'This is a really bad private key, yo.'
    certificate_chain = b'Trust me! Do I not look trustworty?'

    _c.init()

    client_credentials = _c.ClientCredentials(
        None, None, None)
    self.assertIsNotNone(client_credentials)
    client_credentials = _c.ClientCredentials(
        root_certificates, None, None)
    self.assertIsNotNone(client_credentials)
    client_credentials = _c.ClientCredentials(
        None, private_key, certificate_chain)
    self.assertIsNotNone(client_credentials)
    client_credentials = _c.ClientCredentials(
        root_certificates, private_key, certificate_chain)
    self.assertIsNotNone(client_credentials)
    del client_credentials

    _c.shut_down()

  def test_server_credentials(self):
    root_certificates = b'Trust starts here. Really.'
    first_private_key = b'This is a really bad private key, yo.'
    first_certificate_chain = b'Trust me! Do I not look trustworty?'
    second_private_key = b'This is another bad private key, yo.'
    second_certificate_chain = b'Look into my eyes; you can totes trust me.'

    _c.init()

    server_credentials = _c.ServerCredentials(
        None, ((first_private_key, first_certificate_chain),))
    del server_credentials
    server_credentials = _c.ServerCredentials(
        root_certificates, ((first_private_key, first_certificate_chain),))
    del server_credentials
    server_credentials = _c.ServerCredentials(
        root_certificates,
        ((first_private_key, first_certificate_chain),
         (second_private_key, second_certificate_chain),))
    del server_credentials
    with self.assertRaises(TypeError):
      _c.ServerCredentials(
          root_certificates, first_private_key, second_certificate_chain)

    _c.shut_down()

  @unittest.skip('TODO(nathaniel): find and use real-enough test credentials')
  def test_secure_server(self):
    _c.init()

    server_credentials = _c.ServerCredentials(
        'root certificate', (('private key', 'certificate chain'),))

    completion_queue = _c.CompletionQueue()
    server = _c.Server(completion_queue, server_credentials)
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
  unittest.main(verbosity=2)
