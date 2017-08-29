#!/usr/bin/env python

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

from __future__ import print_function

import os
import sys
import re

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

errors = 0
tracers = []
pattern = re.compile("GRPC_TRACER_INITIALIZER\((true|false), \"(.*)\"\)")
for root, dirs, files in os.walk('src/core'):
  for filename in files:
    path = os.path.join(root, filename)
    if os.path.splitext(path)[1] != '.c': continue
    with open(path) as f:
      text = f.read()
    for o in pattern.findall(text):
      tracers.append(o[1])

with open('doc/environment_variables.md') as f:
 text = f.read()

for t in tracers:
    if t not in text:
        print("ERROR: tracer \"%s\" is not mentioned in doc/environment_variables.md" % t)
        errors += 1


assert errors == 0
