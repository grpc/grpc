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

import collections


def _threshold_for_count_below(buckets, boundaries, count_below):
    count_so_far = 0
    for lower_idx in range(0, len(buckets)):
        count_so_far += buckets[lower_idx]
        if count_so_far >= count_below:
            break
    if count_so_far == count_below:
        # this bucket hits the threshold exactly... we should be midway through
        # any run of zero values following the bucket
        for upper_idx in range(lower_idx + 1, len(buckets)):
            if buckets[upper_idx] != 0:
                break
        return (boundaries[lower_idx] + boundaries[upper_idx]) / 2.0
    else:
        # treat values as uniform throughout the bucket, and find where this value
        # should lie
        lower_bound = boundaries[lower_idx]
        upper_bound = boundaries[lower_idx + 1]
        return (upper_bound - (upper_bound - lower_bound) *
                (count_so_far - count_below) / float(buckets[lower_idx]))


def percentile(buckets, pctl, boundaries):
    return _threshold_for_count_below(buckets, boundaries,
                                      sum(buckets) * pctl / 100.0)


def counter(core_stats, name):
    for stat in core_stats['metrics']:
        if stat['name'] == name:
            return int(stat.get('count', 0))


Histogram = collections.namedtuple('Histogram', 'buckets boundaries')


def histogram(core_stats, name):
    for stat in core_stats['metrics']:
        if stat['name'] == name:
            buckets = []
            boundaries = []
            for b in stat['histogram']['buckets']:
                buckets.append(int(b.get('count', 0)))
                boundaries.append(int(b.get('start', 0)))
    return Histogram(buckets=buckets, boundaries=boundaries)
