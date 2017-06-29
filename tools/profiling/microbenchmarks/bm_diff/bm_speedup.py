# Copyright 2017, Google Inc.
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

from scipy import stats
import math

_DEFAULT_THRESHOLD = 1e-10

def scale(a, mul):
  return [x * mul for x in a]


def cmp(a, b):
  return stats.ttest_ind(a, b)

def speedup(new, old, threshold = _DEFAULT_THRESHOLD):
  if (len(set(new))) == 1 and new == old: return 0
  s0, p0 = cmp(new, old)
  if math.isnan(p0): return 0
  if s0 == 0: return 0
  if p0 > threshold: return 0
  if s0 < 0:
    pct = 1
    while pct < 100:
      sp, pp = cmp(new, scale(old, 1 - pct / 100.0))
      if sp > 0: break
      if pp > threshold: break
      pct += 1
    return -(pct - 1)
  else:
    pct = 1
    while pct < 10000:
      sp, pp = cmp(new, scale(old, 1 + pct / 100.0))
      if sp < 0: break
      if pp > threshold: break
      pct += 1
    return pct - 1


if __name__ == "__main__":
  new = [0.0, 0.0, 0.0, 0.0] 
  old = [2.96608e-06, 3.35076e-06, 3.45384e-06, 3.34407e-06]
  print speedup(new, old, 1e-5)
  print speedup(old, new, 1e-5)
