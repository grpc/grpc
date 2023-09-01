# Copyright 2018 gRPC authors.
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

import os
import threading
import unittest

from grpc._cython import cygrpc


def _get_number_active_threads():
    return cygrpc._fork_state.active_thread_count._num_active_threads


@unittest.skipIf(os.name == "nt", "Posix-specific tests")
class ForkPosixTester(unittest.TestCase):
    def setUp(self):
        self._saved_fork_support_flag = cygrpc._GRPC_ENABLE_FORK_SUPPORT
        cygrpc._GRPC_ENABLE_FORK_SUPPORT = True

    def testForkManagedThread(self):
        def cb():
            self.assertEqual(1, _get_number_active_threads())

        thread = cygrpc.ForkManagedThread(cb)
        thread.start()
        thread.join()
        self.assertEqual(0, _get_number_active_threads())

    def testForkManagedThreadThrowsException(self):
        def cb():
            self.assertEqual(1, _get_number_active_threads())
            raise Exception("expected exception")

        thread = cygrpc.ForkManagedThread(cb)
        thread.start()
        thread.join()
        self.assertEqual(0, _get_number_active_threads())

    def tearDown(self):
        cygrpc._GRPC_ENABLE_FORK_SUPPORT = self._saved_fork_support_flag


@unittest.skipUnless(os.name == "nt", "Windows-specific tests")
class ForkWindowsTester(unittest.TestCase):
    def testForkManagedThreadIsNoOp(self):
        def cb():
            pass

        thread = cygrpc.ForkManagedThread(cb)
        thread.start()
        thread.join()


if __name__ == "__main__":
    unittest.main(verbosity=2)
