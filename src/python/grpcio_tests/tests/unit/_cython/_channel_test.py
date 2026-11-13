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

import threading
import time
import unittest

from grpc._cython import cygrpc

from tests.unit.framework.common import test_constants


def _channel():
    return cygrpc.Channel(b"localhost:54321", (), None)


def _connectivity_loop(channel):
    for _ in range(100):
        connectivity = channel.check_connectivity_state(True)
        channel.watch_connectivity_state(connectivity, time.time() + 0.2)


def _create_loop_destroy():
    channel = _channel()
    _connectivity_loop(channel)
    channel.close(cygrpc.StatusCode.ok, "Channel close!")


def _in_parallel(behavior, arguments):
    threads = tuple(
        threading.Thread(target=behavior, args=arguments)
        for _ in range(test_constants.THREAD_CONCURRENCY)
    )
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()


class ChannelTest(unittest.TestCase):
    def test_single_channel_lonely_connectivity(self):
        channel = _channel()
        _connectivity_loop(channel)
        channel.close(cygrpc.StatusCode.ok, "Channel close!")

    def test_multiple_channels_lonely_connectivity(self):
        _in_parallel(_create_loop_destroy, ())

    def test_negative_deadline_connectivity(self):
        channel = _channel()
        connectivity = channel.check_connectivity_state(True)
        channel.watch_connectivity_state(connectivity, -3.14)
        channel.close(cygrpc.StatusCode.ok, "Channel close!")
        # NOTE(lidiz) The negative timeout should not trigger SIGABRT.
        # Bug report: https://github.com/grpc/grpc/issues/18244

    def test_close_with_outstanding_connectivity_watch(self):
        channel = _channel()
        connectivity = channel.check_connectivity_state(False)
        watch_finished = threading.Event()

        def watch_target():
            channel.watch_connectivity_state(connectivity, time.time() + 300)
            watch_finished.set()

        watch_thread = threading.Thread(target=watch_target)
        watch_thread.daemon = True
        watch_thread.start()

        # Allow the watch to be armed in Core.
        time.sleep(0.1)

        channel_closed = threading.Event()

        def close_target():
            channel.close(cygrpc.StatusCode.ok, "Channel close!")
            channel_closed.set()

        close_thread = threading.Thread(target=close_target)
        close_thread.daemon = True
        close_thread.start()

        self.assertTrue(channel_closed.wait(timeout=5))
        self.assertTrue(watch_finished.wait(timeout=5))


if __name__ == "__main__":
    unittest.main(verbosity=2)
