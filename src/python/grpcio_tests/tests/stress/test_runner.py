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
"""Thread that sends random weighted requests on a TestService stub."""

import random
import threading
import time
import traceback


def _weighted_test_case_generator(weighted_cases):
    weight_sum = sum(weighted_cases.itervalues())

    while True:
        val = random.uniform(0, weight_sum)
        partial_sum = 0
        for case in weighted_cases:
            partial_sum += weighted_cases[case]
            if val <= partial_sum:
                yield case
                break


class TestRunner(threading.Thread):
    def __init__(self, stub, test_cases, hist, exception_queue, stop_event):
        super(TestRunner, self).__init__()
        self._exception_queue = exception_queue
        self._stop_event = stop_event
        self._stub = stub
        self._test_cases = _weighted_test_case_generator(test_cases)
        self._histogram = hist

    def run(self):
        while not self._stop_event.is_set():
            try:
                test_case = next(self._test_cases)
                start_time = time.time()
                test_case.test_interoperability(self._stub, None)
                end_time = time.time()
                self._histogram.add((end_time - start_time) * 1e9)
            except Exception as e:  # pylint: disable=broad-except
                traceback.print_exc()
                self._exception_queue.put(
                    Exception(
                        f"An exception occurred during test {test_case}",
                        e,
                    )
                )
