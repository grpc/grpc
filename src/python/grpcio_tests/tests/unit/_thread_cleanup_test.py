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
"""Tests for CleanupThread."""

import threading
import time
import unittest

from grpc import _common

_SHORT_TIME = 0.5
_LONG_TIME = 5.0
_EPSILON = 0.5


def cleanup(timeout):
    if timeout is not None:
        time.sleep(timeout)
    else:
        time.sleep(_LONG_TIME)


def slow_cleanup(timeout):
    # Don't respect timeout
    time.sleep(_LONG_TIME)


class CleanupThreadTest(unittest.TestCase):

    def testTargetInvocation(self):
        event = threading.Event()

        def target(arg1, arg2, arg3=None):
            self.assertEqual('arg1', arg1)
            self.assertEqual('arg2', arg2)
            self.assertEqual('arg3', arg3)
            event.set()

        cleanup_thread = _common.CleanupThread(
            behavior=lambda x: None,
            target=target,
            name='test-name',
            args=('arg1', 'arg2'),
            kwargs={
                'arg3': 'arg3'
            })
        cleanup_thread.start()
        cleanup_thread.join()
        self.assertEqual(cleanup_thread.name, 'test-name')
        self.assertTrue(event.is_set())

    def testJoinNoTimeout(self):
        cleanup_thread = _common.CleanupThread(behavior=cleanup)
        cleanup_thread.start()
        start_time = time.time()
        cleanup_thread.join()
        end_time = time.time()
        self.assertAlmostEqual(
            _LONG_TIME, end_time - start_time, delta=_EPSILON)

    def testJoinTimeout(self):
        cleanup_thread = _common.CleanupThread(behavior=cleanup)
        cleanup_thread.start()
        start_time = time.time()
        cleanup_thread.join(_SHORT_TIME)
        end_time = time.time()
        self.assertAlmostEqual(
            _SHORT_TIME, end_time - start_time, delta=_EPSILON)

    def testJoinTimeoutSlowBehavior(self):
        cleanup_thread = _common.CleanupThread(behavior=slow_cleanup)
        cleanup_thread.start()
        start_time = time.time()
        cleanup_thread.join(_SHORT_TIME)
        end_time = time.time()
        self.assertAlmostEqual(
            _LONG_TIME, end_time - start_time, delta=_EPSILON)

    def testJoinTimeoutSlowTarget(self):
        event = threading.Event()

        def target():
            event.wait(_LONG_TIME)

        cleanup_thread = _common.CleanupThread(behavior=cleanup, target=target)
        cleanup_thread.start()
        start_time = time.time()
        cleanup_thread.join(_SHORT_TIME)
        end_time = time.time()
        self.assertAlmostEqual(
            _SHORT_TIME, end_time - start_time, delta=_EPSILON)
        event.set()

    def testJoinZeroTimeout(self):
        cleanup_thread = _common.CleanupThread(behavior=cleanup)
        cleanup_thread.start()
        start_time = time.time()
        cleanup_thread.join(0)
        end_time = time.time()
        self.assertAlmostEqual(0, end_time - start_time, delta=_EPSILON)


if __name__ == '__main__':
    unittest.main(verbosity=2)
