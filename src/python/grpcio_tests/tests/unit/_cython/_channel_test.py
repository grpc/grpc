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
