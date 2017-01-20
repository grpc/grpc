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
            except Exception as e:
                traceback.print_exc()
                self._exception_queue.put(
                    Exception("An exception occured during test {}"
                              .format(test_case), e))
