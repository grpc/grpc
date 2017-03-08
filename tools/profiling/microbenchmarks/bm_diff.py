#!/usr/bin/env python2.7

import sys
import json
import bm_json

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

def min_change(pct):
  return lambda n, o: abs((n-o)/o - 1) > pct/100

_INTERESTING = (
  ('cpu_time', min_change(5)),
  ('real_time', min_change(5)),
)

for bm in sorted(new.keys()):
  if bm not in old: continue
  hdr = False
  n = new[bm]
  o = old[bm]
  print n
  print o
  for fld, chk in _INTERESTING:
    if fld not in n or fld not in o: continue
    if chk(n[fld], o[fld]):
      if not hdr:
        print '%s shows changes:' % bm
        hdr = True
      print '   %s changed %r --> %r' % (fld, o[fld], n[fld])
  sys.exit(0)

