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
"""Tests of grpc.channel_ready_future."""

import threading
import unittest

import grpc
from tests.unit.framework.common import test_constants
from tests.unit import _thread_pool


class _Callback(object):

    def __init__(self):
        self._condition = threading.Condition()
        self._value = None

    def accept_value(self, value):
        with self._condition:
            self._value = value
            self._condition.notify_all()

    def block_until_called(self):
        with self._condition:
            while self._value is None:
                self._condition.wait()
            return self._value


class ChannelReadyFutureTest(unittest.TestCase):

    def test_lonely_channel_connectivity(self):
        channel = grpc.insecure_channel('localhost:12345')
        callback = _Callback()

        ready_future = grpc.channel_ready_future(channel)
        ready_future.add_done_callback(callback.accept_value)
        with self.assertRaises(grpc.FutureTimeoutError):
            ready_future.result(timeout=test_constants.SHORT_TIMEOUT)
        self.assertFalse(ready_future.cancelled())
        self.assertFalse(ready_future.done())
        self.assertTrue(ready_future.running())
        ready_future.cancel()
        value_passed_to_callback = callback.block_until_called()
        self.assertIs(ready_future, value_passed_to_callback)
        self.assertTrue(ready_future.cancelled())
        self.assertTrue(ready_future.done())
        self.assertFalse(ready_future.running())

    def test_immediately_connectable_channel_connectivity(self):
        thread_pool = _thread_pool.RecordingThreadPool(max_workers=None)
        server = grpc.server(thread_pool)
        port = server.add_insecure_port('[::]:0')
        server.start()
        channel = grpc.insecure_channel('localhost:{}'.format(port))
        callback = _Callback()

        ready_future = grpc.channel_ready_future(channel)
        ready_future.add_done_callback(callback.accept_value)
        self.assertIsNone(
            ready_future.result(timeout=test_constants.LONG_TIMEOUT))
        value_passed_to_callback = callback.block_until_called()
        self.assertIs(ready_future, value_passed_to_callback)
        self.assertFalse(ready_future.cancelled())
        self.assertTrue(ready_future.done())
        self.assertFalse(ready_future.running())
        # Cancellation after maturity has no effect.
        ready_future.cancel()
        self.assertFalse(ready_future.cancelled())
        self.assertTrue(ready_future.done())
        self.assertFalse(ready_future.running())
        self.assertFalse(thread_pool.was_used())


if __name__ == '__main__':
    unittest.main(verbosity=2)
