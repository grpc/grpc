#!/usr/bin/env python

# Copyright 2016 gRPC authors.
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
        'git rev-parse --abbrev-ref HEAD', shell=True)
except:
    print('WARNING: not a git repository')
    branch_name = None

if branch_name is not None:
    m = re.match(r'^release-([0-9]+)_([0-9]+)$', branch_name)
    if m:
        print('RELEASE branch')
        # version number should align with the branched version
        check_version = lambda version: (
          version.major == int(m.group(1)) and
          version.minor == int(m.group(2)))
        warning = 'Version key "%%s" value "%%s" should have a major version %s and minor version %s' % (
            m.group(1), m.group(2))
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
    print(warning % ('version', top_version))

for tag, value in settings.iteritems():
    if re.match(r'^[a-z]+_version$', tag):
        value = Version(value)
        if tag != 'core_version':
            if value.major != top_version.major:
                errors += 1
                print('major version mismatch on %s: %d vs %d' %
                      (tag, value.major, top_version.major))
            if value.minor != top_version.minor:
                errors += 1
                print('minor version mismatch on %s: %d vs %d' %
                      (tag, value.minor, top_version.minor))
        if not check_version(value):
            errors += 1
            print(warning % (tag, value))

sys.exit(errors)
