#!/usr/bin/env python2.7
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

# Reads stdin to find chttp2_refcount log lines, and prints reference leaks
# to stdout

import collections
import sys
import re

def new_obj():
  return ['destroy']

outstanding = collections.defaultdict(new_obj)

# Sample log line:
# chttp2:unref:0x629000005200 2->1 destroy [src/core/ext/transport/chttp2/transport/chttp2_transport.c:599]

for line in sys.stdin:
  m = re.search(r'chttp2:(  ref|unref):0x([a-fA-F0-9]+) [^ ]+ ([^[]+) \[(.*)\]', line)
  if m:
    if m.group(1) == '  ref':
      outstanding[m.group(2)].append(m.group(3))
    else:
      outstanding[m.group(2)].remove(m.group(3))

for obj, remaining in outstanding.items():
  if remaining:
    print 'LEAKED: %s %r' % (obj, remaining)

