#!/usr/bin/python
# produces cleaner build.json files

import collections
import json
import os
import sys

TEST = (os.environ.get('TEST', 'false') == 'true')

_TOP_LEVEL_KEYS = ['settings', 'filegroups', 'libs', 'targets']
_VERSION_KEYS = ['major', 'minor', 'micro', 'build']
_ELEM_KEYS = [
    'name', 
    'build', 
    'run',
    'language', 
    'public_headers', 
    'headers', 
    'src', 
    'deps']

def rebuild_as_ordered_dict(indict, special_keys):
  outdict = collections.OrderedDict()
  for key in special_keys:
    if key in indict:
      outdict[key] = indict[key]
  for key in sorted(indict.keys()):
    if key in special_keys: continue
    outdict[key] = indict[key]
  return outdict

def clean_elem(indict):
  for name in ['public_headers', 'headers', 'src']:
    if name not in indict: continue
    inlist = indict[name]
    protos = list(x for x in inlist if os.path.splitext(x)[1] == '.proto')
    others = set(x for x in inlist if x not in protos)
    indict[name] = protos + sorted(others)
  return rebuild_as_ordered_dict(indict, _ELEM_KEYS)

for filename in sys.argv[1:]:
  with open(filename) as f:
    js = json.load(f)
  js = rebuild_as_ordered_dict(js, _TOP_LEVEL_KEYS)
  js['settings']['version'] = rebuild_as_ordered_dict(
      js['settings']['version'], _VERSION_KEYS)
  for grp in ['filegroups', 'libs', 'targets']:
    if grp not in js: continue
    js[grp] = sorted([clean_elem(x) for x in js[grp]],
                     key=lambda x: (x.get('language', '_'), x['name']))
  output = json.dumps(js, indent = 2)
  # massage out trailing whitespace
  lines = []
  for line in output.splitlines():
    lines.append(line.rstrip() + '\n')
  output = ''.join(lines)
  if TEST:
    with open(filename) as f:
      assert f.read() == output
  else:
    with open(filename, 'w') as f:
      f.write(output)

