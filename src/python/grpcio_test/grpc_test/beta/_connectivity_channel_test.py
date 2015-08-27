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

"""Tests of grpc.beta._connectivity_channel."""

import threading
import time
import unittest

from grpc._adapter import _low
from grpc._adapter import _types
from grpc.beta import _connectivity_channel
from grpc_test.framework.common import test_constants

_MAPPING_FUNCTION = lambda integer: integer * 200 + 17
_MAPPING = {
    state: _MAPPING_FUNCTION(state) for state in _types.ConnectivityState}
_IDLE, _CONNECTING, _READY, _TRANSIENT_FAILURE, _FATAL_FAILURE = map(
    _MAPPING_FUNCTION, _types.ConnectivityState)


def _drive_completion_queue(completion_queue):
  while True:
    event = completion_queue.next(time.time() + 24 * 60 * 60)
    if event.type == _types.EventType.QUEUE_SHUTDOWN:
      break


class _Callback(object):

  def __init__(self):
    self._condition = threading.Condition()
    self._connectivities = []

  def update(self, connectivity):
    with self._condition:
      self._connectivities.append(connectivity)
      self._condition.notify()

  def connectivities(self):
    with self._condition:
      return tuple(self._connectivities)

  def block_until_connectivities_satisfy(self, predicate):
    with self._condition:
      while True:
        connectivities = tuple(self._connectivities)
        if predicate(connectivities):
          return connectivities
        else:
          self._condition.wait()


class ChannelConnectivityTest(unittest.TestCase):

  def test_lonely_channel_connectivity(self):
    low_channel = _low.Channel('localhost:12345', ())
    callback = _Callback()

    connectivity_channel = _connectivity_channel.ConnectivityChannel(
        low_channel, _MAPPING)
    connectivity_channel.subscribe(callback.update, try_to_connect=False)
    first_connectivities = callback.block_until_connectivities_satisfy(bool)
    connectivity_channel.subscribe(callback.update, try_to_connect=True)
    second_connectivities = callback.block_until_connectivities_satisfy(
        lambda connectivities: 2 <= len(connectivities))
    # Wait for a connection that will never happen.
    time.sleep(test_constants.SHORT_TIMEOUT)
    third_connectivities = callback.connectivities()
    connectivity_channel.unsubscribe(callback.update)
    fourth_connectivities = callback.connectivities()
    connectivity_channel.unsubscribe(callback.update)
    fifth_connectivities = callback.connectivities()

    self.assertSequenceEqual((_IDLE,), first_connectivities)
    self.assertNotIn(_READY, second_connectivities)
    self.assertNotIn(_READY, third_connectivities)
    self.assertNotIn(_READY, fourth_connectivities)
    self.assertNotIn(_READY, fifth_connectivities)

  def test_immediately_connectable_channel_connectivity(self):
    server_completion_queue = _low.CompletionQueue()
    server = _low.Server(server_completion_queue, [])
    port = server.add_http2_port('[::]:0')
    server.start()
    server_completion_queue_thread = threading.Thread(
        target=_drive_completion_queue, args=(server_completion_queue,))
    server_completion_queue_thread.start()
    low_channel = _low.Channel('localhost:%d' % port, ())
    first_callback = _Callback()
    second_callback = _Callback()

    connectivity_channel = _connectivity_channel.ConnectivityChannel(
        low_channel, _MAPPING)
    connectivity_channel.subscribe(first_callback.update, try_to_connect=False)
    first_connectivities = first_callback.block_until_connectivities_satisfy(
        bool)
    # Wait for a connection that will never happen because try_to_connect=True
    # has not yet been passed.
    time.sleep(test_constants.SHORT_TIMEOUT)
    second_connectivities = first_callback.connectivities()
    connectivity_channel.subscribe(second_callback.update, try_to_connect=True)
    third_connectivities = first_callback.block_until_connectivities_satisfy(
        lambda connectivities: 2 <= len(connectivities))
    fourth_connectivities = second_callback.block_until_connectivities_satisfy(
        bool)
    # Wait for a connection that will happen (or may already have happened).
    first_callback.block_until_connectivities_satisfy(
        lambda connectivities: _READY in connectivities)
    second_callback.block_until_connectivities_satisfy(
        lambda connectivities: _READY in connectivities)
    connectivity_channel.unsubscribe(first_callback.update)
    connectivity_channel.unsubscribe(second_callback.update)

    server.shutdown()
    server_completion_queue.shutdown()
    server_completion_queue_thread.join()

    self.assertSequenceEqual((_IDLE,), first_connectivities)
    self.assertSequenceEqual((_IDLE,), second_connectivities)
    self.assertNotIn(_TRANSIENT_FAILURE, third_connectivities)
    self.assertNotIn(_FATAL_FAILURE, third_connectivities)
    self.assertNotIn(_TRANSIENT_FAILURE, fourth_connectivities)
    self.assertNotIn(_FATAL_FAILURE, fourth_connectivities)

  def test_reachable_then_unreachable_channel_connectivity(self):
    server_completion_queue = _low.CompletionQueue()
    server = _low.Server(server_completion_queue, [])
    port = server.add_http2_port('[::]:0')
    server.start()
    server_completion_queue_thread = threading.Thread(
        target=_drive_completion_queue, args=(server_completion_queue,))
    server_completion_queue_thread.start()
    low_channel = _low.Channel('localhost:%d' % port, ())
    callback = _Callback()

    connectivity_channel = _connectivity_channel.ConnectivityChannel(
        low_channel, _MAPPING)
    connectivity_channel.subscribe(callback.update, try_to_connect=True)
    callback.block_until_connectivities_satisfy(
        lambda connectivities: _READY in connectivities)
    # Now take down the server and confirm that channel readiness is repudiated.
    server.shutdown()
    callback.block_until_connectivities_satisfy(
        lambda connectivities: connectivities[-1] is not _READY)
    connectivity_channel.unsubscribe(callback.update)

    server.shutdown()
    server_completion_queue.shutdown()
    server_completion_queue_thread.join()


if __name__ == '__main__':
  unittest.main(verbosity=2)
