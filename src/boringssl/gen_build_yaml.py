#!/usr/bin/env python2.7
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
              # We want to include files['fips_fragments'], but not build them as objects.
              # See https://boringssl-review.googlesource.com/c/boringssl/+/16946
              for f in files['ssl_headers'] + files['ssl_internal_headers'] + files['crypto_headers'] + files['crypto_internal_headers'] + files['fips_fragments']
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
          for test in list(sorted(set(files['ssl_test'] + files['crypto_test'])))
      ],
      'targets': [
          {
            'name': 'boringssl_%s' % os.path.splitext(os.path.basename(test))[0],
            'build': 'test',
            'run': False,
            'secure': 'no',
            'language': 'c++',
            'src': ["third_party/boringssl/crypto/test/gtest_main.cc"],
            'vs_proj_dir': 'test/boringssl',
            'boringssl': True,
            'defaults': 'boringssl',
            'deps': [
                'boringssl_%s_lib' % os.path.splitext(os.path.basename(test))[0],
                'boringssl_test_util',
                'boringssl',
            ]
          }
          for test in list(sorted(set(files['ssl_test'] + files['crypto_test'])))
      ],
      'tests': [
          {
            'name': 'boringssl_%s' % os.path.splitext(os.path.basename(test))[0],
            'args': [],
            'exclude_configs': ['asan', 'ubsan'],
            'ci_platforms': ['linux', 'mac', 'posix', 'windows'],
            'platforms': ['linux', 'mac', 'posix', 'windows'],
            'flaky': False,
            'gtest': True,
            'language': 'c++',
            'boringssl': True,
            'defaults': 'boringssl',
            'cpu_cost': 1.0
          }
          for test in list(sorted(set(files['ssl_test'] + files['crypto_test'])))
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
