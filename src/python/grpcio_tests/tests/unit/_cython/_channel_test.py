# Copyright 2016 gRPC authors.
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

import time
import threading
import unittest

from grpc._cython import cygrpc

from tests.unit.framework.common import test_constants


def _channel_and_completion_queue():
    channel = cygrpc.Channel(b'localhost:54321', cygrpc.ChannelArgs(()))
    completion_queue = cygrpc.CompletionQueue()
    return channel, completion_queue


def _connectivity_loop(channel, completion_queue):
    for _ in range(100):
        connectivity = channel.check_connectivity_state(True)
        channel.watch_connectivity_state(connectivity,
                                         cygrpc.Timespec(time.time() + 0.2),
                                         completion_queue, None)
        completion_queue.poll(deadline=cygrpc.Timespec(float('+inf')))


def _create_loop_destroy():
    channel, completion_queue = _channel_and_completion_queue()
    _connectivity_loop(channel, completion_queue)
    completion_queue.shutdown()


def _in_parallel(behavior, arguments):
    threads = tuple(
        threading.Thread(target=behavior, args=arguments)
        for _ in range(test_constants.THREAD_CONCURRENCY))
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()


class ChannelTest(unittest.TestCase):

    def test_single_channel_lonely_connectivity(self):
        channel, completion_queue = _channel_and_completion_queue()
        _in_parallel(_connectivity_loop, (channel, completion_queue,))
        completion_queue.shutdown()

    def test_multiple_channels_lonely_connectivity(self):
        _in_parallel(_create_loop_destroy, ())


if __name__ == '__main__':
    unittest.main(verbosity=2)
