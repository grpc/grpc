# Copyright 2019 The gRPC Authors
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
"""Tests the gRPC Core shutdown path."""

import time
import threading
import unittest
import datetime

import grpc

_TIMEOUT_FOR_SEGFAULT = datetime.timedelta(seconds=10)


class GrpcShutdownTest(unittest.TestCase):

    def test_channel_close_with_connectivity_watcher(self):
        """Originated by https://github.com/grpc/grpc/issues/20299.

        The grpc_shutdown happens synchronously, but there might be Core object
        references left in Cython which might lead to ABORT or SIGSEGV.
        """
        connection_failed = threading.Event()

        def on_state_change(state):
            if state in (grpc.ChannelConnectivity.TRANSIENT_FAILURE,
                         grpc.ChannelConnectivity.SHUTDOWN):
                connection_failed.set()

        # Connects to an void address, and subscribes state changes
        channel = grpc.insecure_channel("0.1.1.1:12345")
        channel.subscribe(on_state_change, True)

        deadline = datetime.datetime.now() + _TIMEOUT_FOR_SEGFAULT

        while datetime.datetime.now() < deadline:
            time.sleep(0.1)
            if connection_failed.is_set():
                channel.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
