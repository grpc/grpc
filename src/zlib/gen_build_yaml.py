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

import re
import os
import sys
import yaml

os.chdir(os.path.dirname(sys.argv[0])+'/../..')

out = {}

try:
  with open('third_party/zlib/CMakeLists.txt') as f:
    cmake = f.read()

  def cmpath(x):
    return 'third_party/zlib/%s' % x.replace('${CMAKE_CURRENT_BINARY_DIR}/', '')

  def cmvar(name):
    regex = r'set\(\s*'
    regex += name
    regex += r'([^)]*)\)'
    return [cmpath(x) for x in re.search(regex, cmake).group(1).split()]

  out['libs'] = [{
      'name': 'z',
      'zlib': True,
      'defaults': 'zlib',
      'build': 'private',
      'language': 'c',
      'secure': 'no',
      'src': sorted(cmvar('ZLIB_SRCS')),
      'headers': sorted(cmvar('ZLIB_PUBLIC_HDRS') + cmvar('ZLIB_PRIVATE_HDRS')),
  }]
except:
  pass

print yaml.dump(out)

