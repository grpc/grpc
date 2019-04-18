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
