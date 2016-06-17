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

"""Tests for the old '_low'."""

import threading
import time
import unittest

import six
from six.moves import queue

from grpc._adapter import _intermediary_low as _low

_STREAM_LENGTH = 300
_TIMEOUT = 5
_AFTER_DELAY = 2
_FUTURE = time.time() + 60 * 60 * 24
_BYTE_SEQUENCE = b'\abcdefghijklmnopqrstuvwxyz0123456789' * 200
_BYTE_SEQUENCE_SEQUENCE = tuple(
    bytes(bytearray((row + column) % 256 for column in range(row)))
    for row in range(_STREAM_LENGTH))


class LonelyClientTest(unittest.TestCase):

  def testLonelyClient(self):
    host = 'nosuchhostexists'
    port = 54321
    method = 'test method'
    deadline = time.time() + _TIMEOUT
    after_deadline = deadline + _AFTER_DELAY
    metadata_tag = object()
    finish_tag = object()

    completion_queue = _low.CompletionQueue()
    channel = _low.Channel('%s:%d' % (host, port), None)
    client_call = _low.Call(channel, completion_queue, method, host, deadline)

    client_call.invoke(completion_queue, metadata_tag, finish_tag)
    first_event = completion_queue.get(after_deadline)
    self.assertIsNotNone(first_event)
    second_event = completion_queue.get(after_deadline)
    self.assertIsNotNone(second_event)
    kinds = [event.kind for event in (first_event, second_event)]
    six.assertCountEqual(self,
        (_low.Event.Kind.METADATA_ACCEPTED, _low.Event.Kind.FINISH),
        kinds)

    self.assertIsNone(completion_queue.get(after_deadline))

    completion_queue.stop()
    stop_event = completion_queue.get(_FUTURE)
    self.assertEqual(_low.Event.Kind.STOP, stop_event.kind)

    del client_call
    del channel
    del completion_queue


def _drive_completion_queue(completion_queue, event_queue):
  while True:
    event = completion_queue.get(_FUTURE)
    if event.kind is _low.Event.Kind.STOP:
      break
    event_queue.put(event)


class EchoTest(unittest.TestCase):

  def setUp(self):
    self.host = 'localhost'

    self.server_completion_queue = _low.CompletionQueue()
    self.server = _low.Server(self.server_completion_queue)
    port = self.server.add_http2_addr('[::]:0')
    self.server.start()
    self.server_events = queue.Queue()
    self.server_completion_queue_thread = threading.Thread(
        target=_drive_completion_queue,
        args=(self.server_completion_queue, self.server_events))
    self.server_completion_queue_thread.start()

    self.client_completion_queue = _low.CompletionQueue()
    self.channel = _low.Channel('%s:%d' % (self.host, port), None)
    self.client_events = queue.Queue()
    self.client_completion_queue_thread = threading.Thread(
        target=_drive_completion_queue,
        args=(self.client_completion_queue, self.client_events))
    self.client_completion_queue_thread.start()

  def tearDown(self):
    self.server.stop()
    self.server.cancel_all_calls()
    self.server_completion_queue.stop()
    self.client_completion_queue.stop()
    self.server_completion_queue_thread.join()
    self.client_completion_queue_thread.join()
    del self.server

  def _perform_echo_test(self, test_data):
    method = 'test method'
    details = 'test details'
    server_leading_metadata_key = 'my_server_leading_key'
    server_leading_metadata_value = 'my_server_leading_value'
    server_trailing_metadata_key = 'my_server_trailing_key'
    server_trailing_metadata_value = 'my_server_trailing_value'
    client_metadata_key = 'my_client_key'
    client_metadata_value = 'my_client_value'
    server_leading_binary_metadata_key = 'my_server_leading_key-bin'
    server_leading_binary_metadata_value = b'\0'*2047
    server_trailing_binary_metadata_key = 'my_server_trailing_key-bin'
    server_trailing_binary_metadata_value = b'\0'*2047
    client_binary_metadata_key = 'my_client_key-bin'
    client_binary_metadata_value = b'\0'*2047
    deadline = _FUTURE
    metadata_tag = object()
    finish_tag = object()
    write_tag = object()
    complete_tag = object()
    service_tag = object()
    read_tag = object()
    status_tag = object()

    server_data = []
    client_data = []

    client_call = _low.Call(self.channel, self.client_completion_queue,
                            method, self.host, deadline)
    client_call.add_metadata(client_metadata_key, client_metadata_value)
    client_call.add_metadata(client_binary_metadata_key,
                             client_binary_metadata_value)

    client_call.invoke(self.client_completion_queue, metadata_tag, finish_tag)

    self.server.service(service_tag)
    service_accepted = self.server_events.get()
    self.assertIsNotNone(service_accepted)
    self.assertIs(service_accepted.kind, _low.Event.Kind.SERVICE_ACCEPTED)
    self.assertIs(service_accepted.tag, service_tag)
    self.assertEqual(method.encode(), service_accepted.service_acceptance.method)
    self.assertEqual(self.host.encode(), service_accepted.service_acceptance.host)
    self.assertIsNotNone(service_accepted.service_acceptance.call)
    metadata = dict(service_accepted.metadata)
    self.assertIn(client_metadata_key.encode(), metadata)
    self.assertEqual(client_metadata_value.encode(), metadata[client_metadata_key.encode()])
    self.assertIn(client_binary_metadata_key.encode(), metadata)
    self.assertEqual(client_binary_metadata_value,
                     metadata[client_binary_metadata_key.encode()])
    server_call = service_accepted.service_acceptance.call
    server_call.accept(self.server_completion_queue, finish_tag)
    server_call.add_metadata(server_leading_metadata_key,
                             server_leading_metadata_value)
    server_call.add_metadata(server_leading_binary_metadata_key,
                             server_leading_binary_metadata_value)
    server_call.premetadata()

    metadata_accepted = self.client_events.get()
    self.assertIsNotNone(metadata_accepted)
    self.assertEqual(_low.Event.Kind.METADATA_ACCEPTED, metadata_accepted.kind)
    self.assertEqual(metadata_tag, metadata_accepted.tag)
    metadata = dict(metadata_accepted.metadata)
    self.assertIn(server_leading_metadata_key.encode(), metadata)
    self.assertEqual(server_leading_metadata_value.encode(),
                     metadata[server_leading_metadata_key.encode()])
    self.assertIn(server_leading_binary_metadata_key.encode(), metadata)
    self.assertEqual(server_leading_binary_metadata_value,
                     metadata[server_leading_binary_metadata_key.encode()])

    for datum in test_data:
      client_call.write(datum, write_tag, _low.WriteFlags.WRITE_NO_COMPRESS)
      write_accepted = self.client_events.get()
      self.assertIsNotNone(write_accepted)
      self.assertIs(write_accepted.kind, _low.Event.Kind.WRITE_ACCEPTED)
      self.assertIs(write_accepted.tag, write_tag)
      self.assertIs(write_accepted.write_accepted, True)

      server_call.read(read_tag)
      read_accepted = self.server_events.get()
      self.assertIsNotNone(read_accepted)
      self.assertEqual(_low.Event.Kind.READ_ACCEPTED, read_accepted.kind)
      self.assertEqual(read_tag, read_accepted.tag)
      self.assertIsNotNone(read_accepted.bytes)
      server_data.append(read_accepted.bytes)

      server_call.write(read_accepted.bytes, write_tag, 0)
      write_accepted = self.server_events.get()
      self.assertIsNotNone(write_accepted)
      self.assertEqual(_low.Event.Kind.WRITE_ACCEPTED, write_accepted.kind)
      self.assertEqual(write_tag, write_accepted.tag)
      self.assertTrue(write_accepted.write_accepted)

      client_call.read(read_tag)
      read_accepted = self.client_events.get()
      self.assertIsNotNone(read_accepted)
      self.assertEqual(_low.Event.Kind.READ_ACCEPTED, read_accepted.kind)
      self.assertEqual(read_tag, read_accepted.tag)
      self.assertIsNotNone(read_accepted.bytes)
      client_data.append(read_accepted.bytes)

    client_call.complete(complete_tag)
    complete_accepted = self.client_events.get()
    self.assertIsNotNone(complete_accepted)
    self.assertIs(complete_accepted.kind, _low.Event.Kind.COMPLETE_ACCEPTED)
    self.assertIs(complete_accepted.tag, complete_tag)
    self.assertIs(complete_accepted.complete_accepted, True)

    server_call.read(read_tag)
    read_accepted = self.server_events.get()
    self.assertIsNotNone(read_accepted)
    self.assertEqual(_low.Event.Kind.READ_ACCEPTED, read_accepted.kind)
    self.assertEqual(read_tag, read_accepted.tag)
    self.assertIsNone(read_accepted.bytes)

    server_call.add_metadata(server_trailing_metadata_key,
                             server_trailing_metadata_value)
    server_call.add_metadata(server_trailing_binary_metadata_key,
                             server_trailing_binary_metadata_value)

    server_call.status(_low.Status(_low.Code.OK, details), status_tag)
    server_terminal_event_one = self.server_events.get()
    server_terminal_event_two = self.server_events.get()
    if server_terminal_event_one.kind == _low.Event.Kind.COMPLETE_ACCEPTED:
      status_accepted = server_terminal_event_one
      rpc_accepted = server_terminal_event_two
    else:
      status_accepted = server_terminal_event_two
      rpc_accepted = server_terminal_event_one
    self.assertIsNotNone(status_accepted)
    self.assertIsNotNone(rpc_accepted)
    self.assertEqual(_low.Event.Kind.COMPLETE_ACCEPTED, status_accepted.kind)
    self.assertEqual(status_tag, status_accepted.tag)
    self.assertTrue(status_accepted.complete_accepted)
    self.assertEqual(_low.Event.Kind.FINISH, rpc_accepted.kind)
    self.assertEqual(finish_tag, rpc_accepted.tag)
    self.assertEqual(_low.Status(_low.Code.OK, ''), rpc_accepted.status)

    client_call.read(read_tag)
    client_terminal_event_one = self.client_events.get()
    client_terminal_event_two = self.client_events.get()
    if client_terminal_event_one.kind == _low.Event.Kind.READ_ACCEPTED:
      read_accepted = client_terminal_event_one
      finish_accepted = client_terminal_event_two
    else:
      read_accepted = client_terminal_event_two
      finish_accepted = client_terminal_event_one
    self.assertIsNotNone(read_accepted)
    self.assertIsNotNone(finish_accepted)
    self.assertEqual(_low.Event.Kind.READ_ACCEPTED, read_accepted.kind)
    self.assertEqual(read_tag, read_accepted.tag)
    self.assertIsNone(read_accepted.bytes)
    self.assertEqual(_low.Event.Kind.FINISH, finish_accepted.kind)
    self.assertEqual(finish_tag, finish_accepted.tag)
    self.assertEqual(_low.Status(_low.Code.OK, details.encode()), finish_accepted.status)
    metadata = dict(finish_accepted.metadata)
    self.assertIn(server_trailing_metadata_key.encode(), metadata)
    self.assertEqual(server_trailing_metadata_value.encode(),
                     metadata[server_trailing_metadata_key.encode()])
    self.assertIn(server_trailing_binary_metadata_key.encode(), metadata)
    self.assertEqual(server_trailing_binary_metadata_value,
                     metadata[server_trailing_binary_metadata_key.encode()])
    self.assertSetEqual(set(key for key, _ in finish_accepted.metadata),
                        set((server_trailing_metadata_key.encode(),
                             server_trailing_binary_metadata_key.encode(),)))

    self.assertSequenceEqual(test_data, server_data)
    self.assertSequenceEqual(test_data, client_data)

  def testNoEcho(self):
    self._perform_echo_test(())

  def testOneByteEcho(self):
    self._perform_echo_test([b'\x07'])

  def testOneManyByteEcho(self):
    self._perform_echo_test([_BYTE_SEQUENCE])

  def testManyOneByteEchoes(self):
    self._perform_echo_test(
        [_BYTE_SEQUENCE[i:i+1] for i in range(len(_BYTE_SEQUENCE))])

  def testManyManyByteEchoes(self):
    self._perform_echo_test(_BYTE_SEQUENCE_SEQUENCE)


class CancellationTest(unittest.TestCase):

  def setUp(self):
    self.host = 'localhost'

    self.server_completion_queue = _low.CompletionQueue()
    self.server = _low.Server(self.server_completion_queue)
    port = self.server.add_http2_addr('[::]:0')
    self.server.start()
    self.server_events = queue.Queue()
    self.server_completion_queue_thread = threading.Thread(
        target=_drive_completion_queue,
        args=(self.server_completion_queue, self.server_events))
    self.server_completion_queue_thread.start()

    self.client_completion_queue = _low.CompletionQueue()
    self.channel = _low.Channel('%s:%d' % (self.host, port), None)
    self.client_events = queue.Queue()
    self.client_completion_queue_thread = threading.Thread(
        target=_drive_completion_queue,
        args=(self.client_completion_queue, self.client_events))
    self.client_completion_queue_thread.start()

  def tearDown(self):
    self.server.stop()
    self.server.cancel_all_calls()
    self.server_completion_queue.stop()
    self.client_completion_queue.stop()
    self.server_completion_queue_thread.join()
    self.client_completion_queue_thread.join()
    del self.server

  def testCancellation(self):
    method = 'test method'
    deadline = _FUTURE
    metadata_tag = object()
    finish_tag = object()
    write_tag = object()
    service_tag = object()
    read_tag = object()
    test_data = _BYTE_SEQUENCE_SEQUENCE

    server_data = []
    client_data = []

    client_call = _low.Call(self.channel, self.client_completion_queue,
                            method, self.host, deadline)

    client_call.invoke(self.client_completion_queue, metadata_tag, finish_tag)

    self.server.service(service_tag)
    service_accepted = self.server_events.get()
    server_call = service_accepted.service_acceptance.call

    server_call.accept(self.server_completion_queue, finish_tag)
    server_call.premetadata()

    metadata_accepted = self.client_events.get()
    self.assertIsNotNone(metadata_accepted)

    for datum in test_data:
      client_call.write(datum, write_tag, 0)
      write_accepted = self.client_events.get()

      server_call.read(read_tag)
      read_accepted = self.server_events.get()
      server_data.append(read_accepted.bytes)

      server_call.write(read_accepted.bytes, write_tag, 0)
      write_accepted = self.server_events.get()
      self.assertIsNotNone(write_accepted)

      client_call.read(read_tag)
      read_accepted = self.client_events.get()
      client_data.append(read_accepted.bytes)

    client_call.cancel()
    # cancel() is idempotent.
    client_call.cancel()
    client_call.cancel()
    client_call.cancel()

    server_call.read(read_tag)

    server_terminal_event_one = self.server_events.get()
    server_terminal_event_two = self.server_events.get()
    if server_terminal_event_one.kind == _low.Event.Kind.READ_ACCEPTED:
      read_accepted = server_terminal_event_one
      rpc_accepted = server_terminal_event_two
    else:
      read_accepted = server_terminal_event_two
      rpc_accepted = server_terminal_event_one
    self.assertIsNotNone(read_accepted)
    self.assertIsNotNone(rpc_accepted)
    self.assertEqual(_low.Event.Kind.READ_ACCEPTED, read_accepted.kind)
    self.assertIsNone(read_accepted.bytes)
    self.assertEqual(_low.Event.Kind.FINISH, rpc_accepted.kind)
    self.assertEqual(_low.Status(_low.Code.CANCELLED, ''), rpc_accepted.status)

    finish_event = self.client_events.get()
    self.assertEqual(_low.Event.Kind.FINISH, finish_event.kind)
    self.assertEqual(_low.Status(_low.Code.CANCELLED, b'Cancelled'),
                                 finish_event.status)

    self.assertSequenceEqual(test_data, server_data)
    self.assertSequenceEqual(test_data, client_data)


class ExpirationTest(unittest.TestCase):

  @unittest.skip('TODO(nathaniel): Expiration test!')
  def testExpiration(self):
    pass


if __name__ == '__main__':
  unittest.main(verbosity=2)

