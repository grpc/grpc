#!/usr/bin/env python2.7
#
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

from scipy import stats
import math

_DEFAULT_THRESHOLD = 1e-10


def scale(a, mul):
    return [x * mul for x in a]


def cmp(a, b):
    return stats.ttest_ind(a, b)


def speedup(new, old, threshold=_DEFAULT_THRESHOLD):
    if (len(set(new))) == 1 and new == old:
        return 0
    s0, p0 = cmp(new, old)
    if math.isnan(p0):
        return 0
    if s0 == 0:
        return 0
    if p0 > threshold:
        return 0
    if s0 < 0:
        pct = 1
        while pct < 100:
            sp, pp = cmp(new, scale(old, 1 - pct / 100.0))
            if sp > 0:
                break
            if pp > threshold:
                break
            pct += 1
        return -(pct - 1)
    else:
        pct = 1
        while pct < 10000:
            sp, pp = cmp(new, scale(old, 1 + pct / 100.0))
            if sp < 0:
                break
            if pp > threshold:
                break
            pct += 1
        return pct - 1


if __name__ == "__main__":
    new = [0.0, 0.0, 0.0, 0.0]
    old = [2.96608e-06, 3.35076e-06, 3.45384e-06, 3.34407e-06]
    print speedup(new, old, 1e-5)
    print speedup(old, new, 1e-5)
