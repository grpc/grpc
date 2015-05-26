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

"""Tests for grpc.framework.base.implementations."""

import unittest

from grpc.framework.base import implementations
from grpc.framework.base import interfaces_test_case
from grpc.framework.base import util
from grpc.framework.foundation import logging_pool

POOL_MAX_WORKERS = 10
DEFAULT_TIMEOUT = 30
MAXIMUM_TIMEOUT = 60


class ImplementationsTest(
    interfaces_test_case.FrontAndBackTest, unittest.TestCase):

  def setUp(self):
    self.memory_transmission_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.front_work_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.front_transmission_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.front_utility_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.back_work_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.back_transmission_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.back_utility_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.test_pool = logging_pool.pool(POOL_MAX_WORKERS)
    self.test_servicer = interfaces_test_case.TestServicer(self.test_pool)
    self.front = implementations.front_link(
        self.front_work_pool, self.front_transmission_pool,
        self.front_utility_pool)
    self.back = implementations.back_link(
        self.test_servicer, self.back_work_pool, self.back_transmission_pool,
        self.back_utility_pool, DEFAULT_TIMEOUT, MAXIMUM_TIMEOUT)
    self.front.join_rear_link(self.back)
    self.back.join_fore_link(self.front)

  def tearDown(self):
    util.wait_for_idle(self.back)
    util.wait_for_idle(self.front)
    self.memory_transmission_pool.shutdown(wait=True)
    self.front_work_pool.shutdown(wait=True)
    self.front_transmission_pool.shutdown(wait=True)
    self.front_utility_pool.shutdown(wait=True)
    self.back_work_pool.shutdown(wait=True)
    self.back_transmission_pool.shutdown(wait=True)
    self.back_utility_pool.shutdown(wait=True)
    self.test_pool.shutdown(wait=True)


if __name__ == '__main__':
  unittest.main()
