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

from __future__ import division

import datetime
from concurrent import futures
import unittest
import time
import threading
import six

import grpc
from tests.unit.framework.common import test_constants

_WAIT_FOR_BLOCKING = datetime.timedelta(seconds=1)


def _block_on_waiting(server, termination_event, timeout=None):
    server.start()
    server.wait_for_termination(timeout=timeout)
    termination_event.set()


class ServerWaitForTerminationTest(unittest.TestCase):

    def test_unblock_by_invoking_stop(self):
        termination_event = threading.Event()
        server = grpc.server(futures.ThreadPoolExecutor())

        wait_thread = threading.Thread(target=_block_on_waiting,
                                       args=(
                                           server,
                                           termination_event,
                                       ))
        wait_thread.daemon = True
        wait_thread.start()
        time.sleep(_WAIT_FOR_BLOCKING.total_seconds())

        server.stop(None)
        termination_event.wait(timeout=test_constants.SHORT_TIMEOUT)
        self.assertTrue(termination_event.is_set())

    def test_unblock_by_del(self):
        termination_event = threading.Event()
        server = grpc.server(futures.ThreadPoolExecutor())

        wait_thread = threading.Thread(target=_block_on_waiting,
                                       args=(
                                           server,
                                           termination_event,
                                       ))
        wait_thread.daemon = True
        wait_thread.start()
        time.sleep(_WAIT_FOR_BLOCKING.total_seconds())

        # Invoke manually here, in Python 2 it will be invoked by GC sometime.
        server.__del__()
        termination_event.wait(timeout=test_constants.SHORT_TIMEOUT)
        self.assertTrue(termination_event.is_set())

    def test_unblock_by_timeout(self):
        termination_event = threading.Event()
        server = grpc.server(futures.ThreadPoolExecutor())

        wait_thread = threading.Thread(target=_block_on_waiting,
                                       args=(
                                           server,
                                           termination_event,
                                           test_constants.SHORT_TIMEOUT / 2,
                                       ))
        wait_thread.daemon = True
        wait_thread.start()

        termination_event.wait(timeout=test_constants.SHORT_TIMEOUT)
        self.assertTrue(termination_event.is_set())


if __name__ == '__main__':
    unittest.main(verbosity=2)
