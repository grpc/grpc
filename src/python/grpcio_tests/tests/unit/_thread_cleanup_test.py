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
            kwargs={'arg3': 'arg3'})
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
