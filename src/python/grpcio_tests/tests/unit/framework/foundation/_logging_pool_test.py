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
"""Tests for grpc.framework.foundation.logging_pool."""

import threading
import unittest

from grpc.framework.foundation import logging_pool

_POOL_SIZE = 16


class _CallableObject(object):

    def __init__(self):
        self._lock = threading.Lock()
        self._passed_values = []

    def __call__(self, value):
        with self._lock:
            self._passed_values.append(value)

    def passed_values(self):
        with self._lock:
            return tuple(self._passed_values)


class LoggingPoolTest(unittest.TestCase):

    def testUpAndDown(self):
        pool = logging_pool.pool(_POOL_SIZE)
        pool.shutdown(wait=True)

        with logging_pool.pool(_POOL_SIZE) as pool:
            self.assertIsNotNone(pool)

    def testTaskExecuted(self):
        test_list = []

        with logging_pool.pool(_POOL_SIZE) as pool:
            pool.submit(lambda: test_list.append(object())).result()

        self.assertTrue(test_list)

    def testException(self):
        with logging_pool.pool(_POOL_SIZE) as pool:
            raised_exception = pool.submit(lambda: 1 / 0).exception()

        self.assertIsNotNone(raised_exception)

    def testCallableObjectExecuted(self):
        callable_object = _CallableObject()
        passed_object = object()
        with logging_pool.pool(_POOL_SIZE) as pool:
            future = pool.submit(callable_object, passed_object)
        self.assertIsNone(future.result())
        self.assertSequenceEqual((passed_object,),
                                 callable_object.passed_values())


if __name__ == '__main__':
    unittest.main(verbosity=2)
