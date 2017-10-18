#!/usr/bin/env python
# Copyright 2015 gRPC authors.
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

from __future__ import print_function

import json
import os
import re
import sys

root = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))
with open(os.path.join(root, 'tools', 'run_tests', 'generated', 'sources_and_headers.json')) as f:
  js = json.loads(f.read())

re_inc1 = re.compile(r'^#\s*include\s*"([^"]*)"')
assert re_inc1.match('#include "foo"').group(1) == 'foo'
re_inc2 = re.compile(r'^#\s*include\s*<((grpc|grpc\+\+)/[^"]*)>')
assert re_inc2.match('#include <grpc++/foo>').group(1) == 'grpc++/foo'

def get_target(name):
  for target in js:
    if target['name'] == name:
      return target
  assert False, 'no target %s' % name

def target_has_header(target, name):
  if name.startswith('absl/'): return True
  # print target['name'], name
  if name in target['headers']:
    return True
  for dep in target['deps']:
    if target_has_header(get_target(dep), name):
      return True
  if name in ['src/core/lib/profiling/stap_probes.h',
              'src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h']:
    return True
  return False

def produces_object(name):
  return os.path.splitext(name)[1] in ['.c', '.cc']

c_ish = {}
obj_producer_to_source = {'c': c_ish, 'c++': c_ish, 'csharp': {}}

errors = 0
for target in js:
  if not target['third_party']:
    for fn in target['src']:
      with open(os.path.join(root, fn)) as f:
        src = f.read().splitlines()
      for line in src:
        m = re_inc1.match(line)
        if m:
          if not target_has_header(target, m.group(1)):
            print (
              'target %s (%s) does not name header %s as a dependency' % (
                target['name'], fn, m.group(1)))
            errors += 1
        m = re_inc2.match(line)
        if m:
          if not target_has_header(target, 'include/' + m.group(1)):
            print (
              'target %s (%s) does not name header %s as a dependency' % (
                target['name'], fn, m.group(1)))
            errors += 1
  if target['type'] in ['lib', 'filegroup']:
    for fn in target['src']:
      language = target['language']
      if produces_object(fn):
        obj_base = os.path.splitext(os.path.basename(fn))[0]
        if obj_base in obj_producer_to_source[language]:
          if obj_producer_to_source[language][obj_base] != fn:
            print (
              'target %s (%s) produces an aliased object file with %s' % (
                target['name'], fn, obj_producer_to_source[language][obj_base]))
        else:
          obj_producer_to_source[language][obj_base] = fn

assert errors == 0
