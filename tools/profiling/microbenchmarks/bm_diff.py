#!/usr/bin/env python2.7
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

import sys
import json
import bm_json
import tabulate
import argparse

def changed_ratio(n, o):
  if float(o) <= .0001: o = 0
  if float(n) <= .0001: n = 0
  if o == 0 and n == 0: return 0
  if o == 0: return 100
  return (float(n)-float(o))/float(o)

def min_change(pct):
  return lambda n, o: abs(changed_ratio(n,o)) > pct/100.0

_INTERESTING = {
  'cpu_time': min_change(10),
  'real_time': min_change(10),
  'locks_per_iteration': min_change(5),
  'allocs_per_iteration': min_change(5),
  'writes_per_iteration': min_change(5),
  'atm_cas_per_iteration': min_change(1),
  'atm_add_per_iteration': min_change(5),
}

argp = argparse.ArgumentParser(description='Perform diff on microbenchmarks')
argp.add_argument('-t', '--track',
                  choices=sorted(_INTERESTING.keys()),
                  nargs='+',
                  default=sorted(_INTERESTING.keys()),
                  help='Which metrics to track')
argp.add_argument('files', metavar='bm_file.json', type=str, nargs=4,
                    help='files to diff. ')
args = argp.parse_args()

with open(args.files[0]) as f:
  js_new_ctr = json.loads(f.read())
with open(args.files[1]) as f:
  js_new_opt = json.loads(f.read())
with open(args.files[2]) as f:
  js_old_ctr = json.loads(f.read())
with open(args.files[3]) as f:
  js_old_opt = json.loads(f.read())

new = {}
old = {}

for row in bm_json.expand_json(js_new_ctr, js_new_opt):
  new[row['cpp_name']] = row
for row in bm_json.expand_json(js_old_ctr, js_old_opt):
  old[row['cpp_name']] = row

changed = []
for fld in args.track:
  chk = _INTERESTING[fld]
  for bm in new.keys():
    if bm not in old: continue
    n = new[bm]
    o = old[bm]
    if fld not in n or fld not in o: continue
    if chk(n[fld], o[fld]):
      changed.append((fld, chk))
      break

headers = ['Benchmark'] + [c[0] for c in changed] + ['Details']
rows = []
for bm in sorted(new.keys()):
  if bm not in old: continue
  row = [bm]
  any_changed = False
  n = new[bm]
  o = old[bm]
  details = ''
  for fld in args.track:
    chk = _INTERESTING[fld]
    if fld not in n or fld not in o: continue
    if chk(n[fld], o[fld]):
      row.append(changed_ratio(n[fld], o[fld]))
      if details: details += ', '
      details += '%s:%r-->%r' % (fld, float(o[fld]), float(n[fld]))
      any_changed = True
    else:
      row.append('')
  if any_changed:
    row.append(details)
    rows.append(row)
print tabulate.tabulate(rows, headers=headers, floatfmt='+.2f')
