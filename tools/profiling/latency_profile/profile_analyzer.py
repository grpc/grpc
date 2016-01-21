#!/usr/bin/env python2.7
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

import argparse
import collections
import hashlib
import itertools
import json
import math
import tabulate
import time


SELF_TIME = object()
TIME_FROM_SCOPE_START = object()
TIME_TO_SCOPE_END = object()
TIME_FROM_STACK_START = object()
TIME_TO_STACK_END = object()


argp = argparse.ArgumentParser(description='Process output of basic_prof builds')
argp.add_argument('--source', default='latency_trace.txt', type=str)
argp.add_argument('--fmt', choices=tabulate.tabulate_formats, default='simple')
args = argp.parse_args()

class LineItem(object):

  def __init__(self, line, indent):
    self.tag = line['tag']
    self.indent = indent
    self.start_time = line['t']
    self.end_time = None
    self.important = line['imp']
    self.filename = line['file']
    self.fileline = line['line']
    self.times = {}


class ScopeBuilder(object):

  def __init__(self, call_stack_builder, line):
    self.call_stack_builder = call_stack_builder
    self.indent = len(call_stack_builder.stk)
    self.top_line = LineItem(line, self.indent)
    call_stack_builder.lines.append(self.top_line)
    self.first_child_pos = len(call_stack_builder.lines)

  def mark(self, line):
    line_item = LineItem(line, self.indent + 1)
    line_item.end_time = line_item.start_time
    self.call_stack_builder.lines.append(line_item)

  def finish(self, line):
    assert line['tag'] == self.top_line.tag, 'expected %s, got %s; thread=%s; t0=%f t1=%f' % (self.top_line.tag, line['tag'], line['thd'], self.top_line.start_time, line['t'])
    final_time_stamp = line['t']
    assert self.top_line.end_time is None
    self.top_line.end_time = final_time_stamp
    assert SELF_TIME not in self.top_line.times
    self.top_line.times[SELF_TIME] = final_time_stamp - self.top_line.start_time
    for line in self.call_stack_builder.lines[self.first_child_pos:]:
      if TIME_FROM_SCOPE_START not in line.times:
        line.times[TIME_FROM_SCOPE_START] = line.start_time - self.top_line.start_time
        line.times[TIME_TO_SCOPE_END] = final_time_stamp - line.end_time


class CallStackBuilder(object):

  def __init__(self):
    self.stk = []
    self.signature = hashlib.md5()
    self.lines = []

  def finish(self):
    start_time = self.lines[0].start_time
    end_time = self.lines[0].end_time
    self.signature = self.signature.hexdigest()
    for line in self.lines:
      line.times[TIME_FROM_STACK_START] = line.start_time - start_time
      line.times[TIME_TO_STACK_END] = end_time - line.end_time

  def add(self, line):
    line_type = line['type']
    self.signature.update(line_type)
    self.signature.update(line['tag'])
    if line_type == '{':
      self.stk.append(ScopeBuilder(self, line))
      return False
    elif line_type == '}':
      assert self.stk, 'expected non-empty stack for closing %s; thread=%s; t=%f' % (line['tag'], line['thd'], line['t'])
      self.stk.pop().finish(line)
      if not self.stk:
        self.finish()
        return True
      return False
    elif line_type == '.' or line_type == '!':
      self.stk[-1].mark(line)
      return False
    else:
      raise Exception('Unknown line type: \'%s\'' % line_type)


class CallStack(object):

  def __init__(self, initial_call_stack_builder):
    self.count = 1
    self.signature = initial_call_stack_builder.signature
    self.lines = initial_call_stack_builder.lines
    for line in self.lines:
      for key, val in line.times.items():
        line.times[key] = [val]

  def add(self, call_stack_builder):
    assert self.signature == call_stack_builder.signature
    self.count += 1
    assert len(self.lines) == len(call_stack_builder.lines)
    for lsum, line in itertools.izip(self.lines, call_stack_builder.lines):
      assert lsum.tag == line.tag
      assert lsum.times.keys() == line.times.keys()
      for k, lst in lsum.times.iteritems():
        lst.append(line.times[k])

  def finish(self):
    for line in self.lines:
      for lst in line.times.itervalues():
        lst.sort()

builder = collections.defaultdict(CallStackBuilder)
call_stacks = collections.defaultdict(CallStack)

lines = 0
start = time.time()
with open(args.source) as f:
  for line in f:
    lines += 1
    inf = json.loads(line)
    thd = inf['thd']
    cs = builder[thd]
    if cs.add(inf):
      if cs.signature in call_stacks:
        call_stacks[cs.signature].add(cs)
      else:
        call_stacks[cs.signature] = CallStack(cs)
      del builder[thd]
time_taken = time.time() - start

call_stacks = sorted(call_stacks.values(), key=lambda cs: cs.count, reverse=True)
total_stacks = 0
for cs in call_stacks:
  total_stacks += cs.count
  cs.finish()

def percentile(N, percent, key=lambda x:x):
    """
    Find the percentile of a list of values.

    @parameter N - is a list of values. Note N MUST BE already sorted.
    @parameter percent - a float value from 0.0 to 1.0.
    @parameter key - optional key function to compute value from each element of N.

    @return - the percentile of the values
    """
    if not N:
        return None
    k = (len(N)-1) * percent
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return key(N[int(k)])
    d0 = key(N[int(f)]) * (c-k)
    d1 = key(N[int(c)]) * (k-f)
    return d0+d1

def tidy_tag(tag):
  if tag[0:10] == 'GRPC_PTAG_':
    return tag[10:]
  return tag

def time_string(values):
  num_values = len(values)
  return '%.1f/%.1f/%.1f' % (
      1e6 * percentile(values, 0.5),
      1e6 * percentile(values, 0.9),
      1e6 * percentile(values, 0.99))

def time_format(idx):
  def ent(line, idx=idx):
    if idx in line.times:
      return time_string(line.times[idx])
    return ''
  return ent

FORMAT = [
  ('TAG', lambda line: '..'*line.indent + tidy_tag(line.tag)),
  ('LOC', lambda line: '%s:%d' % (line.filename[line.filename.rfind('/')+1:], line.fileline)),
  ('FROM_STACK_START', time_format(TIME_FROM_STACK_START)),
  ('SELF', time_format(SELF_TIME)),
  ('TO_STACK_END', time_format(TIME_TO_STACK_END)),
  ('FROM_SCOPE_START', time_format(TIME_FROM_SCOPE_START)),
  ('SELF', time_format(SELF_TIME)),
  ('TO_SCOPE_END', time_format(TIME_TO_SCOPE_END)),
]

BANNER = {
    'simple': 'Count: %(count)d',
    'html': '<h1>Count: %(count)d</h1>'
}

if args.fmt == 'html':
  print '<html>'
  print '<head>'
  print '<title>Profile Report</title>'
  print '</head>'

accounted_for = 0
for cs in call_stacks:
  if args.fmt in BANNER:
    print BANNER[args.fmt] % {
        'count': cs.count,
    }
  header, _ = zip(*FORMAT)
  table = []
  for line in cs.lines:
    fields = []
    for _, fn in FORMAT:
      fields.append(fn(line))
    table.append(fields)
  print tabulate.tabulate(table, header, tablefmt=args.fmt)
  accounted_for += cs.count
  if accounted_for > .99 * total_stacks:
    break

if args.fmt == 'html':
  print '</html>'

