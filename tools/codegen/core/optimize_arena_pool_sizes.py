#!/usr/bin/env python3

# Copyright 2023 gRPC authors.
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

# USAGE:
# Run some tests with the GRPC_ARENA_TRACE_POOLED_ALLOCATIONS #define turned on.
# Capture the output to a text file.
# Invoke this program with that as an argument, and let it work its magic.

import collections
import heapq
import random
import re
import sys

# A single allocation, negative size => free
Allocation = collections.namedtuple('Allocation', 'size ptr')
Active = collections.namedtuple('Active', 'id size')

# Read through all the captures, and build up scrubbed traces
arenas = []
building = collections.defaultdict(list)
active = {}
biggest = 0
smallest = 1024
sizes = set()
for filename in sys.argv[1:]:
    for line in open(filename):
        m = re.search(r'ARENA 0x([0-9a-f]+) ALLOC ([0-9]+) @ 0x([0-9a-f]+)',
                      line)
        if m:
            size = int(m.group(2))
            if size > biggest:
                biggest = size
            if size < smallest:
                smallest = size
            active[m.group(3)] = Active(m.group(1), size)
            building[m.group(1)].append(size)
            sizes.add(size)
        m = re.search(r'FREE 0x([0-9a-f]+)', line)
        if m:
            # We may have spurious frees, so make sure there's an outstanding allocation
            last = active.pop(m.group(1), None)
            if last is not None:
                building[last.id].append(-last.size)
        m = re.search(r'DESTRUCT_ARENA 0x([0-9a-f]+)', line)
        if m:
            trace = building.pop(m.group(1), None)
            if trace:
                arenas.append(trace)


# Given a list of pool sizes, return which bucket an allocation should go into
def bucket(pool_sizes, size):
    for bucket in sorted(pool_sizes):
        if abs(size) <= bucket:
            return bucket


# Given a list of pool sizes, determine the total outstanding bytes in the arena for once trace
def outstanding_bytes(pool_sizes, trace):
    free_list = collections.defaultdict(int)
    allocated = 0
    for size in trace:
        b = bucket(pool_sizes, size)
        if size < 0:
            free_list[b] += 1
        else:
            if free_list[b] > 0:
                free_list[b] -= 1
            else:
                allocated += b
    return allocated + len(pool_sizes) * 8


# Given a list of pool sizes, determine the maximum outstanding bytes for any seen trace
def measure(pool_sizes):
    max_outstanding = 0
    for trace in arenas:
        max_outstanding = max(max_outstanding,
                              outstanding_bytes(pool_sizes, trace))
    return max_outstanding


ALWAYS_INCLUDE = 1024
best = [ALWAYS_INCLUDE, biggest]
best_measure = measure(best)

testq = []
step = 0


def add(l):
    global testq, best_measure, best
    m = measure(l)
    if m < best_measure:
        best_measure = m
        best = l
    if l[-1] == smallest:
        return
    heapq.heappush(testq, (m, l))


add(best)

while testq:
    top = heapq.heappop(testq)[1]
    m = measure(top)
    step += 1
    if step % 1000 == 0:
        print("iter %d; pending=%d; top=%r/%d" %
              (step, len(testq), top, measure(top)))
    for i in sizes:
        if i >= top[-1]:
            continue
        add(top + [i])

print("SAW SIZES: %r" % sorted(list(sizes)))
print("BEST: %r" % list(reversed(best)))
print("BEST MEASURE: %d" % best_measure)
