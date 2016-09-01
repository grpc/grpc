#!/usr/bin/env python2.7

# Copyright 2016, Google Inc.
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
import yaml
import os
import re
import subprocess

errors = 0

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

# hack import paths to pick up extra code
sys.path.insert(0, os.path.abspath('tools/buildgen/plugins'))
from expand_version import Version

try:
  branch_name = subprocess.check_output(
    'git rev-parse --abbrev-ref HEAD',
    shell=True)
except:
  print 'WARNING: not a git repository'
  branch_name = None

if branch_name is not None:
  m = re.match(r'^release-([0-9]+)_([0-9]+)$', branch_name)
  if m:
    print 'RELEASE branch'
    # version number should align with the branched version
    check_version = lambda version: (
      version.major == int(m.group(1)) and
      version.minor == int(m.group(2)))
    warning = 'Version key "%%s" value "%%s" should have a major version %s and minor version %s' % (m.group(1), m.group(2))
  elif re.match(r'^debian/.*$', branch_name):
    # no additional version checks for debian branches
    check_version = lambda version: True
  else:
    # all other branches should have a -dev tag
    check_version = lambda version: version.tag == 'dev'
    warning = 'Version key "%s" value "%s" should have a -dev tag'
else:
  check_version = lambda version: True

with open('build.yaml', 'r') as f:
  build_yaml = yaml.load(f.read())

settings = build_yaml['settings']

top_version = Version(settings['version'])
if not check_version(top_version):
  errors += 1
  print warning % ('version', top_version)

for tag, value in settings.iteritems():
  if re.match(r'^[a-z]+_version$', tag):
    value = Version(value)
    if value.major != top_version.major:
      errors += 1
      print 'major version mismatch on %s: %d vs %d' % (tag, value.major, top_version.major)
    if value.minor != top_version.minor:
      errors += 1
      print 'minor version mismatch on %s: %d vs %d' % (tag, value.minor, top_version.minor)
    if not check_version(value):
      errors += 1
      print warning % (tag, value)

sys.exit(errors)

