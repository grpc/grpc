# Copyright 2015 gRPC authors.
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
"""Tests of grpc.channel_ready_future."""

import logging
import threading
import unittest

import grpc

from tests.unit import thread_pool
from tests.unit.framework.common import test_constants


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
        channel = grpc.insecure_channel("localhost:12345")
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

        channel.close()

    def test_immediately_connectable_channel_connectivity(self):
        recording_thread_pool = thread_pool.RecordingThreadPool(
            max_workers=None
        )
        server = grpc.server(
            recording_thread_pool, options=(("grpc.so_reuseport", 0),)
        )
        port = server.add_insecure_port("[::]:0")
        server.start()
        channel = grpc.insecure_channel("localhost:{}".format(port))
        callback = _Callback()

        ready_future = grpc.channel_ready_future(channel)
        ready_future.add_done_callback(callback.accept_value)
        self.assertIsNone(
            ready_future.result(timeout=test_constants.LONG_TIMEOUT)
        )
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
        self.assertFalse(recording_thread_pool.was_used())

        channel.close()
        server.stop(None)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
