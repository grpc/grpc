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

import shutil
import sys
import os
import yaml

sys.dont_write_bytecode = True

boring_ssl_root = os.path.abspath(os.path.join(
    os.path.dirname(sys.argv[0]),
    '../../third_party/boringssl'))
sys.path.append(os.path.join(boring_ssl_root, 'util'))

try:
  import generate_build_files
except ImportError:
  print yaml.dump({})
  sys.exit()

def map_dir(filename):
  if filename[0:4] == 'src/':
    return 'third_party/boringssl/' + filename[4:]
  else:
    return 'src/boringssl/' + filename

def map_testarg(arg):
  if '/' in arg:
    return 'third_party/boringssl/' + arg
  else:
    return arg

class Grpc(object):

  yaml = None

  def WriteFiles(self, files, asm_outputs):
    self.yaml = {
      '#': 'generated with tools/buildgen/gen_boring_ssl_build_yaml.py',
      'raw_boringssl_build_output_for_debugging': {
        'files': files,
        'asm_outputs': asm_outputs,
      },
      'libs': [
          {
            'name': 'boringssl',
            'build': 'private',
            'language': 'c',
            'secure': 'no',
            'src': sorted(
              map_dir(f)
              for f in files['ssl'] + files['crypto']
            ),
            'headers': sorted(
              map_dir(f)
              for f in files['ssl_headers'] + files['ssl_internal_headers'] + files['crypto_headers'] + files['crypto_internal_headers']
            ),
            'boringssl': True,
            'defaults': 'boringssl',
          },
          {
            'name': 'boringssl_test_util',
            'build': 'private',
            'language': 'c++',
            'secure': 'no',
            'boringssl': True,
            'defaults': 'boringssl',
            'src': [
              map_dir(f)
              for f in sorted(files['test_support'])
            ],
          }
      ] + [
          {
            'name': 'boringssl_%s_lib' % os.path.splitext(os.path.basename(test))[0],
            'build': 'private',
            'secure': 'no',
            'language': 'c' if os.path.splitext(test)[1] == '.c' else 'c++',
            'src': [map_dir(test)],
            'vs_proj_dir': 'test/boringssl',
            'boringssl': True,
            'defaults': 'boringssl',
            'deps': [
                'boringssl_test_util',
                'boringssl',
            ]
          }
          for test in sorted(files['test'])
      ],
      'targets': [
          {
            'name': 'boringssl_%s' % os.path.splitext(os.path.basename(test))[0],
            'build': 'test',
            'run': False,
            'secure': 'no',
            'language': 'c++',
            'src': [],
            'vs_proj_dir': 'test/boringssl',
            'boringssl': True,
            'defaults': 'boringssl',
            'deps': [
                'boringssl_%s_lib' % os.path.splitext(os.path.basename(test))[0],
                'boringssl_test_util',
                'boringssl',
            ]
          }
          for test in sorted(files['test'])
      ],
      'tests': [
          {
            'name': 'boringssl_%s' % os.path.basename(test[0]),
            'args': [map_testarg(arg) for arg in test[1:]],
            'exclude_configs': ['asan'],
            'ci_platforms': ['linux', 'mac', 'posix', 'windows'],
            'platforms': ['linux', 'mac', 'posix', 'windows'],
            'flaky': False,
            'language': 'c++',
            'boringssl': True,
            'defaults': 'boringssl',
            'cpu_cost': 1.0
          }
          for test in files['tests']
      ]
    }


os.chdir(os.path.dirname(sys.argv[0]))
os.mkdir('src')
try:
  for f in os.listdir(boring_ssl_root):
    os.symlink(os.path.join(boring_ssl_root, f),
               os.path.join('src', f))

  g = Grpc()
  generate_build_files.main([g])

  print yaml.dump(g.yaml)

finally:
  shutil.rmtree('src')
