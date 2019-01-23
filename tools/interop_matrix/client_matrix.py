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

# Defines languages, runtimes and releases for backward compatibility testing

from collections import OrderedDict


def get_github_repo(lang):
    return {
        'dart': 'https://github.com/grpc/grpc-dart.git',
        'go': 'https://github.com/grpc/grpc-go.git',
        'java': 'https://github.com/grpc/grpc-java.git',
        'node': 'https://github.com/grpc/grpc-node.git',
        # all other languages use the grpc.git repo.
    }.get(lang, 'https://github.com/grpc/grpc.git')


def get_release_tags(lang):
    """Returns list of known releases for given language."""
    return list(LANG_RELEASE_MATRIX[lang].keys())


def get_runtimes_for_lang_release(lang, release):
    """Get list of valid runtimes for given release of lang."""
    runtimes = list(LANG_RUNTIME_MATRIX[lang])
    release_info = LANG_RELEASE_MATRIX[lang].get(release)
    if release_info and release_info.runtime_subset:
        runtimes = list(release_info.runtime_subset)

    # check that all selected runtimes are valid for given language
    for runtime in runtimes:
        assert runtime in LANG_RUNTIME_MATRIX[lang]
    return runtimes


def should_build_docker_interop_image_from_release_tag(lang):
    # All dockerfile definitions live in grpc/grpc repository.
    # For language that have a separate repo, we need to use
    # dockerfile definitions from head of grpc/grpc.
    if lang in ['go', 'java', 'node']:
        return False
    return True


# Dictionary of runtimes per language
LANG_RUNTIME_MATRIX = {
    'cxx': ['cxx'],  # This is actually debian8.
    'go': ['go1.8', 'go1.11'],
    'java': ['java_oracle8'],
    'python': ['python'],
    'node': ['node'],
    'ruby': ['ruby'],
    'php': ['php', 'php7'],
    'csharp': ['csharp', 'csharpcoreclr'],
}


class ReleaseInfo:
    """Info about a single release of a language"""

    def __init__(self, patch=[], runtime_subset=[], testcases_file=None):
        self.patch = patch
        self.runtime_subset = runtime_subset
        self.testcases_file = testcases_file


# Dictionary of known releases for given language.
LANG_RELEASE_MATRIX = {
    'cxx':
    OrderedDict([
        ('v1.0.1', ReleaseInfo()),
        ('v1.1.4', ReleaseInfo()),
        ('v1.2.5', ReleaseInfo()),
        ('v1.3.9', ReleaseInfo()),
        ('v1.4.2', ReleaseInfo()),
        ('v1.6.6', ReleaseInfo()),
        ('v1.7.2', ReleaseInfo()),
        ('v1.8.0', ReleaseInfo()),
        ('v1.9.1', ReleaseInfo()),
        ('v1.10.1', ReleaseInfo()),
        ('v1.11.1', ReleaseInfo()),
        ('v1.12.0', ReleaseInfo()),
        ('v1.13.0', ReleaseInfo()),
        ('v1.14.1', ReleaseInfo()),
        ('v1.15.0', ReleaseInfo()),
        ('v1.16.0', ReleaseInfo()),
        ('v1.17.1', ReleaseInfo()),
        ('v1.18.0', ReleaseInfo()),
    ]),
    'go':
    OrderedDict([
        ('v1.0.5', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.2.1', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.3.0', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.4.2', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.5.2', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.6.0', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.7.4', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.8.2', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.9.2', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.10.1', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.11.3', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.12.2', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.13.0', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.14.0', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.15.0', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.16.0', ReleaseInfo(runtime_subset=['go1.8'])),
        ('v1.17.0', ReleaseInfo(runtime_subset=['go1.11'])),
        ('v1.18.0', ReleaseInfo(runtime_subset=['go1.11'])),
    ]),
    'java':
    OrderedDict([
        ('v1.0.3', ReleaseInfo()),
        ('v1.1.2', ReleaseInfo()),
        ('v1.2.0', ReleaseInfo()),
        ('v1.3.1', ReleaseInfo()),
        ('v1.4.0', ReleaseInfo()),
        ('v1.5.0', ReleaseInfo()),
        ('v1.6.1', ReleaseInfo()),
        ('v1.7.0', ReleaseInfo()),
        ('v1.8.0', ReleaseInfo()),
        ('v1.9.1', ReleaseInfo()),
        ('v1.10.1', ReleaseInfo()),
        ('v1.11.0', ReleaseInfo()),
        ('v1.12.0', ReleaseInfo()),
        ('v1.13.1', ReleaseInfo()),
        ('v1.14.0', ReleaseInfo()),
        ('v1.15.0', ReleaseInfo()),
        ('v1.16.1', ReleaseInfo()),
        ('v1.17.1', ReleaseInfo()),
        ('v1.18.0', ReleaseInfo()),
    ]),
    'python':
    OrderedDict([
        ('v1.0.x', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.1.4', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.2.5', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.3.9', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.4.2', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.6.6', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.7.2', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.8.1', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.9.1', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.10.1', ReleaseInfo(testcases_file='python__v1.0.x')),
        ('v1.11.1', ReleaseInfo(testcases_file='python__v1.11.1')),
        ('v1.12.0', ReleaseInfo(testcases_file='python__v1.11.1')),
        ('v1.13.0', ReleaseInfo(testcases_file='python__v1.11.1')),
        ('v1.14.1', ReleaseInfo(testcases_file='python__v1.11.1')),
        ('v1.15.0', ReleaseInfo(testcases_file='python__v1.11.1')),
        ('v1.16.0', ReleaseInfo(testcases_file='python__v1.11.1')),
        ('v1.17.1', ReleaseInfo(testcases_file='python__v1.11.1')),
        ('v1.18.0', ReleaseInfo()),
    ]),
    'node':
    OrderedDict([
        ('v1.0.1', ReleaseInfo(testcases_file='node__v1.0.1')),
        ('v1.1.4', ReleaseInfo(testcases_file='node__v1.1.4')),
        ('v1.2.5', ReleaseInfo(testcases_file='node__v1.1.4')),
        ('v1.3.9', ReleaseInfo(testcases_file='node__v1.1.4')),
        ('v1.4.2', ReleaseInfo(testcases_file='node__v1.1.4')),
        ('v1.6.6', ReleaseInfo(testcases_file='node__v1.1.4')),
        # TODO: https://github.com/grpc/grpc-node/issues/235.
        # ('v1.7.2', ReleaseInfo()),
        ('v1.8.4', ReleaseInfo()),
        ('v1.9.1', ReleaseInfo()),
        ('v1.10.0', ReleaseInfo()),
        ('v1.11.3', ReleaseInfo()),
        ('v1.12.4', ReleaseInfo()),
    ]),
    'ruby':
    OrderedDict([
        ('v1.0.1',
         ReleaseInfo(
             patch=[
                 'tools/dockerfile/interoptest/grpc_interop_ruby/Dockerfile',
                 'tools/dockerfile/interoptest/grpc_interop_ruby/build_interop.sh',
             ],
             testcases_file='ruby__v1.0.1')),
        ('v1.1.4', ReleaseInfo()),
        ('v1.2.5', ReleaseInfo()),
        ('v1.3.9', ReleaseInfo()),
        ('v1.4.2', ReleaseInfo()),
        ('v1.6.6', ReleaseInfo()),
        ('v1.7.2', ReleaseInfo()),
        ('v1.8.0', ReleaseInfo()),
        ('v1.9.1', ReleaseInfo()),
        ('v1.10.1', ReleaseInfo()),
        ('v1.11.1', ReleaseInfo()),
        ('v1.12.0', ReleaseInfo()),
        ('v1.13.0', ReleaseInfo()),
        ('v1.14.1', ReleaseInfo()),
        ('v1.15.0', ReleaseInfo()),
        ('v1.16.0', ReleaseInfo()),
        ('v1.17.1', ReleaseInfo()),
        ('v1.18.0',
         ReleaseInfo(patch=[
             'tools/dockerfile/interoptest/grpc_interop_ruby/build_interop.sh',
         ])),
    ]),
    'php':
    OrderedDict([
        ('v1.0.1', ReleaseInfo()),
        ('v1.1.4', ReleaseInfo()),
        ('v1.2.5', ReleaseInfo()),
        ('v1.3.9', ReleaseInfo()),
        ('v1.4.2', ReleaseInfo()),
        ('v1.6.6', ReleaseInfo()),
        ('v1.7.2', ReleaseInfo()),
        ('v1.8.0', ReleaseInfo()),
        ('v1.9.1', ReleaseInfo()),
        ('v1.10.1', ReleaseInfo()),
        ('v1.11.1', ReleaseInfo()),
        ('v1.12.0', ReleaseInfo()),
        ('v1.13.0', ReleaseInfo()),
        ('v1.14.1', ReleaseInfo()),
        ('v1.15.0', ReleaseInfo()),
        ('v1.16.0', ReleaseInfo()),
        ('v1.17.1', ReleaseInfo()),
        ('v1.18.0', ReleaseInfo()),
    ]),
    'csharp':
    OrderedDict([
        ('v1.0.1',
         ReleaseInfo(
             patch=[
                 'tools/dockerfile/interoptest/grpc_interop_csharp/Dockerfile',
                 'tools/dockerfile/interoptest/grpc_interop_csharpcoreclr/Dockerfile',
             ],
             testcases_file='csharp__v1.1.4')),
        ('v1.1.4', ReleaseInfo(testcases_file='csharp__v1.1.4')),
        ('v1.2.5', ReleaseInfo(testcases_file='csharp__v1.1.4')),
        ('v1.3.9', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.4.2', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.6.6', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.7.2', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.8.0', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.9.1', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.10.1', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.11.1', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.12.0', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.13.0', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.14.1', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.15.0', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.16.0', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.17.1', ReleaseInfo(testcases_file='csharp__v1.3.9')),
        ('v1.18.0', ReleaseInfo()),
    ]),
}
