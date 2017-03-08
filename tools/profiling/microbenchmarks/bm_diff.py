#!/usr/bin/env python2.7

import sys
import json
import bm_json
import tabulate

with open(sys.argv[1]) as f:
  js_new_ctr = json.loads(f.read())
with open(sys.argv[2]) as f:
  js_new_opt = json.loads(f.read())
with open(sys.argv[3]) as f:
  js_old_ctr = json.loads(f.read())
with open(sys.argv[4]) as f:
  js_old_opt = json.loads(f.read())

new = {}
old = {}

for row in bm_json.expand_json(js_new_ctr, js_new_opt):
  new[row['cpp_name']] = row
for row in bm_json.expand_json(js_old_ctr, js_old_opt):
  old[row['cpp_name']] = row

def changed_ratio(n, o):
  return float(n-o)/float(o)

def min_change(pct):
  return lambda n, o: abs(changed_ratio(n,o)) > pct/100.0

_INTERESTING = (
  ('cpu_time', min_change(10)),
  ('real_time', min_change(10)),
  ('locks_per_iteration', min_change(5)),
  ('allocs_per_iteration', min_change(5)),
  ('writes_per_iteration', min_change(5)),
  ('atm_cas_per_iteration', min_change(1)),
  ('atm_add_per_iteration', min_change(5)),
)

changed = []
for fld, chk in _INTERESTING:
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
  for fld, chk in _INTERESTING:
    if fld not in n or fld not in o: continue
    if chk(n[fld], o[fld]):
      row.append(changed_ratio(n[fld], o[fld]))
      if details: details += ', '
      details += '%s:%r-->%r' % (fld, o[fld], n[fld])
      any_changed = True
    else:
      row.append('')
  if any_changed:
    row.append(details)
    rows.append(row)
print tabulate.tabulate(rows, headers=headers, floatfmt='+.2f')
