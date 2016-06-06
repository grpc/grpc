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

"""Tests of grpc._channel.Channel connectivity."""

import threading
import time
import unittest
from concurrent import futures

import grpc
from grpc import _channel
from grpc import _server
from tests.unit.framework.common import test_constants


def _ready_in_connectivities(connectivities):
  return grpc.ChannelConnectivity.READY in connectivities


def _last_connectivity_is_not_ready(connectivities):
  return connectivities[-1] is not grpc.ChannelConnectivity.READY


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
    callback = _Callback()

    channel = _channel.Channel('localhost:12345', None, None)
    channel.subscribe(callback.update, try_to_connect=False)
    first_connectivities = callback.block_until_connectivities_satisfy(bool)
    channel.subscribe(callback.update, try_to_connect=True)
    second_connectivities = callback.block_until_connectivities_satisfy(
        lambda connectivities: 2 <= len(connectivities))
    # Wait for a connection that will never happen.
    time.sleep(test_constants.SHORT_TIMEOUT)
    third_connectivities = callback.connectivities()
    channel.unsubscribe(callback.update)
    fourth_connectivities = callback.connectivities()
    channel.unsubscribe(callback.update)
    fifth_connectivities = callback.connectivities()

    self.assertSequenceEqual(
        (grpc.ChannelConnectivity.IDLE,), first_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.READY, second_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.READY, third_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.READY, fourth_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.READY, fifth_connectivities)

  def test_immediately_connectable_channel_connectivity(self):
    server = _server.Server((), futures.ThreadPoolExecutor(max_workers=0))
    port = server.add_insecure_port('[::]:0')
    server.start()
    first_callback = _Callback()
    second_callback = _Callback()

    channel = _channel.Channel('localhost:{}'.format(port), None, None)
    channel.subscribe(first_callback.update, try_to_connect=False)
    first_connectivities = first_callback.block_until_connectivities_satisfy(
        bool)
    # Wait for a connection that will never happen because try_to_connect=True
    # has not yet been passed.
    time.sleep(test_constants.SHORT_TIMEOUT)
    second_connectivities = first_callback.connectivities()
    channel.subscribe(second_callback.update, try_to_connect=True)
    third_connectivities = first_callback.block_until_connectivities_satisfy(
        lambda connectivities: 2 <= len(connectivities))
    fourth_connectivities = second_callback.block_until_connectivities_satisfy(
        bool)
    # Wait for a connection that will happen (or may already have happened).
    first_callback.block_until_connectivities_satisfy(_ready_in_connectivities)
    second_callback.block_until_connectivities_satisfy(_ready_in_connectivities)
    del channel

    self.assertSequenceEqual(
        (grpc.ChannelConnectivity.IDLE,), first_connectivities)
    self.assertSequenceEqual(
        (grpc.ChannelConnectivity.IDLE,), second_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.TRANSIENT_FAILURE, third_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.FATAL_FAILURE, third_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.TRANSIENT_FAILURE,
        fourth_connectivities)
    self.assertNotIn(
        grpc.ChannelConnectivity.FATAL_FAILURE, fourth_connectivities)

  def test_reachable_then_unreachable_channel_connectivity(self):
    server = _server.Server((), futures.ThreadPoolExecutor(max_workers=0))
    port = server.add_insecure_port('[::]:0')
    server.start()
    callback = _Callback()

    channel = _channel.Channel('localhost:{}'.format(port), None, None)
    channel.subscribe(callback.update, try_to_connect=True)
    callback.block_until_connectivities_satisfy(_ready_in_connectivities)
    # Now take down the server and confirm that channel readiness is repudiated.
    server.stop(None)
    callback.block_until_connectivities_satisfy(_last_connectivity_is_not_ready)
    channel.unsubscribe(callback.update)


if __name__ == '__main__':
  unittest.main(verbosity=2)
