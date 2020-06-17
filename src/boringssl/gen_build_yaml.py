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

from __future__ import print_function
import shutil
import sys
import os
import yaml

sys.dont_write_bytecode = True

boring_ssl_root = os.path.abspath(
    os.path.join(os.path.dirname(sys.argv[0]),
                 '../../third_party/boringssl-with-bazel/src'))
sys.path.append(os.path.join(boring_ssl_root, 'util'))

try:
    import generate_build_files
except ImportError:
    print(yaml.dump({}))
    sys.exit()


def map_dir(filename):
    return 'third_party/boringssl-with-bazel/' + filename


class Grpc(object):
    """Implements a "platform" in the sense of boringssl's generate_build_files.py"""
    yaml = None

    def WriteFiles(self, files, asm_outputs):
        test_binaries = ['ssl_test', 'crypto_test']

        self.yaml = {
            '#':
                'generated with src/boringssl/gen_build_yaml.py',
            'raw_boringssl_build_output_for_debugging': {
                'files': files,
                'asm_outputs': asm_outputs,
            },
            'libs': [
                {
                    'name':
                        'boringssl',
                    'build':
                        'private',
                    'language':
                        'c',
                    'secure':
                        False,
                    'src':
                        sorted(
                            map_dir(f) for f in files['ssl'] + files['crypto']),
                    'headers':
                        sorted(
                            map_dir(f)
                            # We want to include files['fips_fragments'], but not build them as objects.
                            # See https://boringssl-review.googlesource.com/c/boringssl/+/16946
                            for f in files['ssl_headers'] +
                            files['ssl_internal_headers'] +
                            files['crypto_headers'] +
                            files['crypto_internal_headers'] +
                            files['fips_fragments']),
                    'boringssl':
                        True,
                    'defaults':
                        'boringssl',
                },
                {
                    'name': 'boringssl_test_util',
                    'build': 'private',
                    'language': 'c++',
                    'secure': False,
                    'boringssl': True,
                    'defaults': 'boringssl',
                    'src': [map_dir(f) for f in sorted(files['test_support'])],
                }
            ],
            'targets': [{
                'name': 'boringssl_%s' % test,
                'build': 'test',
                'run': False,
                'secure': False,
                'language': 'c++',
                'src': sorted(map_dir(f) for f in files[test]),
                'vs_proj_dir': 'test/boringssl',
                'boringssl': True,
                'defaults': 'boringssl',
                'deps': [
                    'boringssl_test_util',
                    'boringssl',
                ]
            } for test in test_binaries],
            'tests': [{
                'name': 'boringssl_%s' % test,
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
            } for test in test_binaries]
        }


os.chdir(os.path.dirname(sys.argv[0]))
os.mkdir('src')
try:
    for f in os.listdir(boring_ssl_root):
        os.symlink(os.path.join(boring_ssl_root, f), os.path.join('src', f))

    grpc_platform = Grpc()
    # We use a hack to run boringssl's util/generate_build_files.py as part of this script.
    # The call will populate "grpc_platform" with boringssl's source file metadata.
    # As a side effect this script generates err_data.c and crypto_test_data.cc (requires golang)
    # Both of these files are already available under third_party/boringssl-with-bazel
    # so we don't need to generate them again, but there's no option to disable that behavior.
    # - crypto_test_data.cc is required to run boringssl_crypto_test but we already
    #   use the copy under third_party/boringssl-with-bazel so we just delete it
    # - err_data.c is already under third_party/boringssl-with-bazel so we just delete it
    generate_build_files.main([grpc_platform])

    print(yaml.dump(grpc_platform.yaml))

finally:
    # we don't want err_data.c and crypto_test_data.cc (see comment above)
    if os.path.exists('err_data.c'):
        os.remove('err_data.c')
    if os.path.exists('crypto_test_data.cc'):
        os.remove('crypto_test_data.cc')
    shutil.rmtree('src')
