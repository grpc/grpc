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

import json
import os
import sys
import yaml

run_dir = os.path.dirname(sys.argv[0])
sources_path = os.path.abspath(
    os.path.join(run_dir,
                 '../../third_party/boringssl-with-bazel/sources.json'))
try:
    with open(sources_path, 'r') as s:
        sources = json.load(s)
except IOError:
    sources_path = os.path.abspath(
        os.path.join(run_dir,
                     '../../../../third_party/openssl/boringssl/sources.json'))
    with open(sources_path, 'r') as s:
        sources = json.load(s)


def map_dir(filename):
    return 'third_party/boringssl-with-bazel/' + filename


class Grpc(object):
    """Adapter for boring-SSL json sources files. """

    def __init__(self, sources):
        self.yaml = None
        self.WriteFiles(sources)

    def WriteFiles(self, files):
        test_binaries = ['ssl_test', 'crypto_test']
        asm_outputs = {
            key: value for key, value in files.items() if any(
                f.endswith(".S") or f.endswith(".asm") for f in value)
        }
        self.yaml = {
            '#':
                'generated with src/boringssl/gen_build_yaml.py',
            'raw_boringssl_build_output_for_debugging': {
                'files': files,
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
                    'asm_src': {
                        k: [map_dir(f) for f in value
                           ] for k, value in asm_outputs.items()
                    },
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


grpc_platform = Grpc(sources)
print(yaml.dump(grpc_platform.yaml))
