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

_THRESHOLD = 1e-10

def scale(a, mul):
  return [x*mul for x in a]

def cmp(a, b):
  return stats.ttest_ind(a, b)

def speedup(new, old):
  s0, p0 = cmp(new, old)
  if math.isnan(p0): return 0
  if s0 == 0: return 0
  if p0 > _THRESHOLD: return 0
  if s0 < 0:
    pct = 1
    while pct < 101:
      sp, pp = cmp(new, scale(old, 1 - pct/100.0))
      if sp > 0: break
      if pp > _THRESHOLD: break
      pct += 1
    return -(pct - 1)
  else:
    pct = 1
    while pct < 100000:
      sp, pp = cmp(new, scale(old, 1 + pct/100.0))
      if sp < 0: break
      if pp > _THRESHOLD: break
      pct += 1
    return pct - 1

if __name__ == "__main__":
  new=[66034560.0, 126765693.0, 99074674.0, 98588433.0, 96731372.0, 110179725.0, 103802110.0, 101139800.0, 102357205.0, 99016353.0, 98840824.0, 99585632.0, 98791720.0, 96171521.0, 95327098.0, 95629704.0, 98209772.0, 99779411.0, 100182488.0, 98354192.0, 99644781.0, 98546709.0, 99019176.0, 99543014.0, 99077269.0, 98046601.0, 99319039.0, 98542572.0, 98886614.0, 72560968.0]
  old=[60423464.0, 71249570.0, 73213089.0, 73200055.0, 72911768.0, 72347798.0, 72494672.0, 72756976.0, 72116565.0, 71541342.0, 73442538.0, 74817383.0, 73007780.0, 72499062.0, 72404945.0, 71843504.0, 73245405.0, 72778304.0, 74004519.0, 73694464.0, 72919931.0, 72955481.0, 71583857.0, 71350467.0, 71836817.0, 70064115.0, 70355345.0, 72516202.0, 71716777.0, 71532266.0]
  print speedup(new, old)
  print speedup(old, new)
