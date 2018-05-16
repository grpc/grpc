# Copyright 2017 gRPC authors.
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

import random
import threading
import time
import unittest

import grpc_testing

_QUANTUM = 0.3
_MANY = 10000
# Tests that run in real time can either wait for the scheduler to
# eventually run what needs to be run (and risk timing out) or declare
# that the scheduler didn't schedule work reasonably fast enough. We
# choose the latter for this test.
_PATHOLOGICAL_SCHEDULING = 'pathological thread scheduling!'


class _TimeNoter(object):

    def __init__(self, time):
        self._condition = threading.Condition()
        self._time = time
        self._call_times = []

    def __call__(self):
        with self._condition:
            self._call_times.append(self._time.time())

    def call_times(self):
        with self._condition:
            return tuple(self._call_times)


class TimeTest(object):

    def test_sleep_for(self):
        start_time = self._time.time()
        self._time.sleep_for(_QUANTUM)
        end_time = self._time.time()

        self.assertLessEqual(start_time + _QUANTUM, end_time)

    def test_sleep_until(self):
        start_time = self._time.time()
        self._time.sleep_until(start_time + _QUANTUM)
        end_time = self._time.time()

        self.assertLessEqual(start_time + _QUANTUM, end_time)

    def test_call_in(self):
        time_noter = _TimeNoter(self._time)

        start_time = self._time.time()
        self._time.call_in(time_noter, _QUANTUM)
        self._time.sleep_for(_QUANTUM * 2)
        call_times = time_noter.call_times()

        self.assertTrue(call_times, msg=_PATHOLOGICAL_SCHEDULING)
        self.assertLessEqual(start_time + _QUANTUM, call_times[0])

    def test_call_at(self):
        time_noter = _TimeNoter(self._time)

        start_time = self._time.time()
        self._time.call_at(time_noter, self._time.time() + _QUANTUM)
        self._time.sleep_for(_QUANTUM * 2)
        call_times = time_noter.call_times()

        self.assertTrue(call_times, msg=_PATHOLOGICAL_SCHEDULING)
        self.assertLessEqual(start_time + _QUANTUM, call_times[0])

    def test_cancel(self):
        time_noter = _TimeNoter(self._time)

        future = self._time.call_in(time_noter, _QUANTUM * 2)
        self._time.sleep_for(_QUANTUM)
        cancelled = future.cancel()
        self._time.sleep_for(_QUANTUM * 2)
        call_times = time_noter.call_times()

        self.assertFalse(call_times, msg=_PATHOLOGICAL_SCHEDULING)
        self.assertTrue(cancelled)
        self.assertTrue(future.cancelled())

    def test_many(self):
        test_events = tuple(threading.Event() for _ in range(_MANY))
        possibly_cancelled_futures = {}
        background_noise_futures = []

        for test_event in test_events:
            possibly_cancelled_futures[test_event] = self._time.call_in(
                test_event.set, _QUANTUM * (2 + random.random()))
        for _ in range(_MANY):
            background_noise_futures.append(
                self._time.call_in(threading.Event().set,
                                   _QUANTUM * 1000 * random.random()))
        self._time.sleep_for(_QUANTUM)
        cancelled = set()
        for test_event, test_future in possibly_cancelled_futures.items():
            if bool(random.randint(0, 1)) and test_future.cancel():
                cancelled.add(test_event)
        self._time.sleep_for(_QUANTUM * 3)

        for test_event in test_events:
            (self.assertFalse if test_event in cancelled else
             self.assertTrue)(test_event.is_set())
        for background_noise_future in background_noise_futures:
            background_noise_future.cancel()

    def test_same_behavior_used_several_times(self):
        time_noter = _TimeNoter(self._time)

        start_time = self._time.time()
        first_future_at_one = self._time.call_in(time_noter, _QUANTUM)
        second_future_at_one = self._time.call_in(time_noter, _QUANTUM)
        first_future_at_three = self._time.call_in(time_noter, _QUANTUM * 3)
        second_future_at_three = self._time.call_in(time_noter, _QUANTUM * 3)
        self._time.sleep_for(_QUANTUM * 2)
        first_future_at_one_cancelled = first_future_at_one.cancel()
        second_future_at_one_cancelled = second_future_at_one.cancel()
        first_future_at_three_cancelled = first_future_at_three.cancel()
        self._time.sleep_for(_QUANTUM * 2)
        second_future_at_three_cancelled = second_future_at_three.cancel()
        first_future_at_three_cancelled_again = first_future_at_three.cancel()
        call_times = time_noter.call_times()

        self.assertEqual(3, len(call_times), msg=_PATHOLOGICAL_SCHEDULING)
        self.assertFalse(first_future_at_one_cancelled)
        self.assertFalse(second_future_at_one_cancelled)
        self.assertTrue(first_future_at_three_cancelled)
        self.assertFalse(second_future_at_three_cancelled)
        self.assertTrue(first_future_at_three_cancelled_again)
        self.assertLessEqual(start_time + _QUANTUM, call_times[0])
        self.assertLessEqual(start_time + _QUANTUM, call_times[1])
        self.assertLessEqual(start_time + _QUANTUM * 3, call_times[2])


class StrictRealTimeTest(TimeTest, unittest.TestCase):

    def setUp(self):
        self._time = grpc_testing.strict_real_time()


class StrictFakeTimeTest(TimeTest, unittest.TestCase):

    def setUp(self):
        self._time = grpc_testing.strict_fake_time(
            random.randint(0, int(time.time())))


if __name__ == '__main__':
    unittest.main(verbosity=2)
