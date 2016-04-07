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

# produces cleaner build.yaml files

import collections
import os
import sys
import yaml

TEST = (os.environ.get('TEST', 'false') == 'true')

_TOP_LEVEL_KEYS = ['settings', 'proto_deps', 'filegroups', 'libs', 'targets', 'vspackages']
_ELEM_KEYS = [
    'name',
    'gtest',
    'cpu_cost',
    'flaky',
    'build',
    'run',
    'language',
    'public_headers',
    'headers',
    'src',
    'deps']

def repr_ordered_dict(dumper, odict):
  return dumper.represent_mapping(u'tag:yaml.org,2002:map', odict.items())

yaml.add_representer(collections.OrderedDict, repr_ordered_dict)

def rebuild_as_ordered_dict(indict, special_keys):
  outdict = collections.OrderedDict()
  for key in sorted(indict.keys()):
    if '#' in key:
      outdict[key] = indict[key]
  for key in special_keys:
    if key in indict:
      outdict[key] = indict[key]
  for key in sorted(indict.keys()):
    if key in special_keys: continue
    if '#' in key: continue
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
    js = yaml.load(f)
  js = rebuild_as_ordered_dict(js, _TOP_LEVEL_KEYS)
  for grp in ['filegroups', 'libs', 'targets']:
    if grp not in js: continue
    js[grp] = sorted([clean_elem(x) for x in js[grp]],
                     key=lambda x: (x.get('language', '_'), x['name']))
  output = yaml.dump(js, indent=2, width=80, default_flow_style=False)
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
