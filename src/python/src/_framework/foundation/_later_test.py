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

"""Tests of the later module."""

import threading
import time
import unittest

from _framework.foundation import future
from _framework.foundation import later

TICK = 0.1


class LaterTest(unittest.TestCase):

  def test_simple_delay(self):
    lock = threading.Lock()
    cell = [0]
    def increment_cell():
      with lock:
        cell[0] += 1
    computation_future = later.later(TICK * 2, increment_cell)
    self.assertFalse(computation_future.done())
    self.assertFalse(computation_future.cancelled())
    time.sleep(TICK)
    self.assertFalse(computation_future.done())
    self.assertFalse(computation_future.cancelled())
    with lock:
      self.assertEqual(0, cell[0])
    time.sleep(TICK * 2)
    self.assertTrue(computation_future.done())
    self.assertFalse(computation_future.cancelled())
    with lock:
      self.assertEqual(1, cell[0])
    outcome = computation_future.outcome()
    self.assertEqual(future.RETURNED, outcome.category)

  def test_callback(self):
    lock = threading.Lock()
    cell = [0]
    callback_called = [False]
    outcome_passed_to_callback = [None]
    def increment_cell():
      with lock:
        cell[0] += 1
    computation_future = later.later(TICK * 2, increment_cell)
    def callback(outcome):
      with lock:
        callback_called[0] = True
        outcome_passed_to_callback[0] = outcome
    computation_future.add_done_callback(callback)
    time.sleep(TICK)
    with lock:
      self.assertFalse(callback_called[0])
    time.sleep(TICK * 2)
    with lock:
      self.assertTrue(callback_called[0])
      self.assertEqual(future.RETURNED, outcome_passed_to_callback[0].category)

      callback_called[0] = False
      outcome_passed_to_callback[0] = None

    computation_future.add_done_callback(callback)
    with lock:
      self.assertTrue(callback_called[0])
      self.assertEqual(future.RETURNED, outcome_passed_to_callback[0].category)

  def test_cancel(self):
    lock = threading.Lock()
    cell = [0]
    callback_called = [False]
    outcome_passed_to_callback = [None]
    def increment_cell():
      with lock:
        cell[0] += 1
    computation_future = later.later(TICK * 2, increment_cell)
    def callback(outcome):
      with lock:
        callback_called[0] = True
        outcome_passed_to_callback[0] = outcome
    computation_future.add_done_callback(callback)
    time.sleep(TICK)
    with lock:
      self.assertFalse(callback_called[0])
    computation_future.cancel()
    self.assertTrue(computation_future.cancelled())
    self.assertFalse(computation_future.done())
    self.assertEqual(future.ABORTED, computation_future.outcome().category)
    with lock:
      self.assertTrue(callback_called[0])
      self.assertEqual(future.ABORTED, outcome_passed_to_callback[0].category)

  def test_outcome(self):
    lock = threading.Lock()
    cell = [0]
    callback_called = [False]
    outcome_passed_to_callback = [None]
    def increment_cell():
      with lock:
        cell[0] += 1
    computation_future = later.later(TICK * 2, increment_cell)
    def callback(outcome):
      with lock:
        callback_called[0] = True
        outcome_passed_to_callback[0] = outcome
    computation_future.add_done_callback(callback)
    returned_outcome = computation_future.outcome()
    self.assertEqual(future.RETURNED, returned_outcome.category)

    # The callback may not yet have been called! Sleep a tick.
    time.sleep(TICK)
    with lock:
      self.assertTrue(callback_called[0])
      self.assertEqual(future.RETURNED, outcome_passed_to_callback[0].category)

if __name__ == '__main__':
  unittest.main()
