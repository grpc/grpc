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


"""Generates the appropriate build.json data for all the proto files."""
import yaml
import collections
import os
import re
import sys

def update_deps(key, proto_filename, deps, deps_external, is_trans, visited):
  if not proto_filename in visited:
    visited.append(proto_filename)
    with open(proto_filename) as inp:
      for line in inp:
        imp = re.search(r'import "([^"]*)"', line)
        if not imp: continue
        imp_proto = imp.group(1)
        # This indicates an external dependency, which we should handle
        # differently and not traverse recursively
        if imp_proto.startswith('google/'):
          if key not in deps_external:
            deps_external[key] = []
          deps_external[key].append(imp_proto[:-6])
          continue
        if key not in deps: deps[key] = []
        deps[key].append(imp_proto[:-6])
        if is_trans:
          update_deps(key, imp_proto, deps, deps_external, is_trans, visited)

def main():
  proto_dir = os.path.abspath(os.path.dirname(sys.argv[0]))
  os.chdir(os.path.join(proto_dir, '../..'))

  deps = {}
  deps_trans = {}
  deps_external = {}
  deps_external_trans = {}
  for root, dirs, files in os.walk('src/proto'):
    for f in files:
      if f[-6:] != '.proto': continue
      look_at = os.path.join(root, f)
      deps_for = look_at[:-6]
      # First level deps
      update_deps(deps_for, look_at, deps, deps_external, False, [])
      # Transitive deps
      update_deps(deps_for, look_at, deps_trans, deps_external_trans, True, [])

  json = {
    'proto_deps': deps,
    'proto_transitive_deps': deps_trans,
    'proto_external_deps': deps_external,
    'proto_transitive_external_deps': deps_external_trans
  }

  print yaml.dump(json)

if __name__ == '__main__':
  main()
