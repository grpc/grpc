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

# check for directory level 'README.md' files
# check that all implementation and interface files have a \file doxygen comment

import os
import sys

# where do we run
_TARGET_DIRS = [
  'include/grpc',
  'include/grpc++',
  'src/core',
  'src/cpp',
  'test/core',
  'test/cpp'
]

# which file extensions do we care about
_INTERESTING_EXTENSIONS = [
  '.c',
  '.h',
  '.cc'
]

# find our home
_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(sys.argv[0]), '../../..'))
os.chdir(_ROOT)

errors = 0

# walk directories, find things
printed_banner = False
for target_dir in _TARGET_DIRS:
  for root, dirs, filenames in os.walk(target_dir):
    if 'README.md' not in filenames:
      if not printed_banner:
        print 'Missing README.md'
        print '================='
        printed_banner = True
      print root
      errors += 1
if printed_banner: print
printed_banner = False
for target_dir in _TARGET_DIRS:
  for root, dirs, filenames in os.walk(target_dir):
    for filename in filenames:
      if os.path.splitext(filename)[1] not in _INTERESTING_EXTENSIONS:
        continue
      path = os.path.join(root, filename)
      with open(path) as f:
        contents = f.read()
      if '\\file' not in contents:
        if not printed_banner:
          print 'Missing \\file comment'
          print '======================'
          printed_banner = True
        print path
        errors += 1

assert errors == 0, 'error count = %d' % errors
