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
        self.assertSequenceEqual(
            (passed_object,), callable_object.passed_values()
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
