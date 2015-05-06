#!/usr/bin/env python
# Copyright 2015, Google Inc.
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

"""
Read GRPC basic profiles, analyze the data.

Usage:
  bins/basicprof/qps_smoke_test > log
  cat log | tools/profile_analyzer/profile_analyzer.py
"""


import collections
import itertools
import re
import sys

# Create a regex to parse output of the C core basic profiler,
# as defined in src/core/profiling/basic_timers.c.
_RE_LINE = re.compile(r'GRPC_LAT_PROF ' +
                      r'([0-9]+\.[0-9]+) 0x([0-9a-f]+) ([{}.!]) ([0-9]+) ' +
                      r'([^ ]+) ([^ ]+) ([0-9]+)')

Entry = collections.namedtuple(
    'Entry',
    ['time', 'thread', 'type', 'tag', 'id', 'file', 'line'])


class ImportantMark(object):
  def __init__(self, entry, stack):
    self._entry = entry
    self._pre_stack = stack
    self._post_stack = list()
    self._n = len(stack)  # we'll also compute times to that many closing }s

  @property
  def entry(self):
    return self._entry

  def append_post_entry(self, entry):
    if self._n > 0:
      self._post_stack.append(entry)
      self._n -= 1

  def get_deltas(self):
    pre_and_post_stacks = itertools.chain(self._pre_stack, self._post_stack)
    return collections.OrderedDict((stack_entry,
                                   (self._entry.time - stack_entry.time))
                                   for stack_entry in pre_and_post_stacks)

def entries():
  for line in sys.stdin:
    m = _RE_LINE.match(line)
    if not m: continue
    yield Entry(time=float(m.group(1)),
                thread=m.group(2),
                type=m.group(3),
                tag=int(m.group(4)),
                id=m.group(5),
                file=m.group(6),
                line=m.group(7))

threads = collections.defaultdict(lambda: collections.defaultdict(list))
times = collections.defaultdict(list)

# Indexed by the mark's tag. Items in the value list correspond to the mark in
# different stack situations.
important_marks = collections.defaultdict(list)

for entry in entries():
  thread = threads[entry.thread]
  if entry.type == '{':
    thread[entry.tag].append(entry)
  if entry.type == '!':
    # Save a snapshot of the current stack inside a new ImportantMark instance.
    # Get all entries with type '{' from "thread".
    stack = [e for entries_for_tag in thread.values()
               for e in entries_for_tag if e.type == '{']
    important_marks[entry.tag].append(ImportantMark(entry, stack))
  elif entry.type == '}':
    last = thread[entry.tag].pop()
    times[entry.tag].append(entry.time - last.time)
    # Update accounting for important marks.
    for imarks_for_tag in important_marks.itervalues():
      for imark in imarks_for_tag:
        imark.append_post_entry(entry)

def percentile(vals, pct):
  return sorted(vals)[int(len(vals) * pct / 100.0)]

print 'tag 50%/90%/95%/99% us'
for tag in sorted(times.keys()):
  vals = times[tag]
  print '%d %.2f/%.2f/%.2f/%.2f' % (tag,
                                    percentile(vals, 50),
                                    percentile(vals, 90),
                                    percentile(vals, 95),
                                    percentile(vals, 99))

print
print 'Important marks:'
print '================'
for tag, imark_for_tag in important_marks.iteritems():
  for imark in imarks_for_tag:
    deltas = imark.get_deltas()
    print '{tag} @ {file}:{line}'.format(**imark.entry._asdict())
    for entry, time_delta_us in deltas.iteritems():
      format_dict = entry._asdict()
      format_dict['time_delta_us']  = time_delta_us
      print '{tag} {type} ({file}:{line}): {time_delta_us:12.3f} us'.format(
          **format_dict)
    print
