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

import math
import threading

from src.proto.grpc.testing import stats_pb2


class Histogram(object):
    """Histogram class used for recording performance testing data.

  This class is thread safe.
  """

    def __init__(self, resolution, max_possible):
        self._lock = threading.Lock()
        self._resolution = resolution
        self._max_possible = max_possible
        self._sum = 0
        self._sum_of_squares = 0
        self.multiplier = 1.0 + self._resolution
        self._count = 0
        self._min = self._max_possible
        self._max = 0
        self._buckets = [0] * (self._bucket_for(self._max_possible) + 1)

    def reset(self):
        with self._lock:
            self._sum = 0
            self._sum_of_squares = 0
            self._count = 0
            self._min = self._max_possible
            self._max = 0
            self._buckets = [0] * (self._bucket_for(self._max_possible) + 1)

    def add(self, val):
        with self._lock:
            self._sum += val
            self._sum_of_squares += val * val
            self._count += 1
            self._min = min(self._min, val)
            self._max = max(self._max, val)
            self._buckets[self._bucket_for(val)] += 1

    def get_data(self):
        with self._lock:
            data = stats_pb2.HistogramData()
            data.bucket.extend(self._buckets)
            data.min_seen = self._min
            data.max_seen = self._max
            data.sum = self._sum
            data.sum_of_squares = self._sum_of_squares
            data.count = self._count
            return data

    def _bucket_for(self, val):
        val = min(val, self._max_possible)
        return int(math.log(val, self.multiplier))
