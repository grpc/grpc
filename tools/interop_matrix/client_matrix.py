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

# Dictionaries used for client matrix testing.


def get_github_repo(lang):
    return {
        'go': 'git@github.com:grpc/grpc-go.git',
        'java': 'git@github.com:grpc/grpc-java.git',
        'node': 'git@github.com:grpc/grpc-node.git',
        # all other languages use the grpc.git repo.
    }.get(lang, 'git@github.com:grpc/grpc.git')


def get_release_tags(lang):
    return map(lambda r: get_release_tag_name(r), LANG_RELEASE_MATRIX[lang])


def get_release_tag_name(release_info):
    assert len(release_info.keys()) == 1
    return release_info.keys()[0]


def should_build_docker_interop_image_from_release_tag(lang):
    if lang in ['go', 'java', 'node']:
        return False
    return True


# Dictionary of runtimes per language
LANG_RUNTIME_MATRIX = {
    'cxx': ['cxx'],  # This is actually debian8.
    'go': ['go1.7', 'go1.8'],
    'java': ['java_oracle8'],
    'python': ['python'],
    'node': ['node'],
    'ruby': ['ruby'],
    'php': ['php', 'php7'],
    'csharp': ['csharp', 'csharpcoreclr'],
}

# Dictionary of releases per language.  For each language, we need to provide
# a release tag pointing to the latest build of the branch.
LANG_RELEASE_MATRIX = {
    'cxx': [
        {
            'v1.0.1': None
        },
        {
            'v1.1.4': None
        },
        {
            'v1.2.5': None
        },
        {
            'v1.3.9': None
        },
        {
            'v1.4.2': None
        },
        {
            'v1.6.6': None
        },
        {
            'v1.7.2': None
        },
    ],
    'go': [
        {
            'v1.0.5': None
        },
        {
            'v1.2.1': None
        },
        {
            'v1.3.0': None
        },
        {
            'v1.4.2': None
        },
        {
            'v1.5.2': None
        },
        {
            'v1.6.0': None
        },
        {
            'v1.7.4': None
        },
        {
            'v1.8.2': None
        },
    ],
    'java': [
        {
            'v1.0.3': None
        },
        {
            'v1.1.2': None
        },
        {
            'v1.2.0': None
        },
        {
            'v1.3.1': None
        },
        {
            'v1.4.0': None
        },
        {
            'v1.5.0': None
        },
        {
            'v1.6.1': None
        },
        {
            'v1.7.0': None
        },
        {
            'v1.8.0': None
        },
    ],
    'python': [
        {
            'v1.0.x': None
        },
        {
            'v1.1.4': None
        },
        {
            'v1.2.5': None
        },
        {
            'v1.3.9': None
        },
        {
            'v1.4.2': None
        },
        {
            'v1.6.6': None
        },
        {
            'v1.7.2': None
        },
    ],
    'node': [
        {
            'v1.0.1': None
        },
        {
            'v1.1.4': None
        },
        {
            'v1.2.5': None
        },
        {
            'v1.3.9': None
        },
        {
            'v1.4.2': None
        },
        {
            'v1.6.6': None
        },
        #{'v1.7.1': None}, Failing tests
    ],
    'ruby': [
        {
            'v1.0.1': {
                'patch': [
                    'tools/dockerfile/interoptest/grpc_interop_ruby/Dockerfile',
                    'tools/dockerfile/interoptest/grpc_interop_ruby/build_interop.sh',
                ]
            }
        },
        {
            'v1.1.4': None
        },
        {
            'v1.2.5': None
        },
        {
            'v1.3.9': None
        },
        {
            'v1.4.2': None
        },
        {
            'v1.6.6': None
        },
        {
            'v1.7.2': None
        },
    ],
    'php': [
        {
            'v1.0.1': None
        },
        {
            'v1.1.4': None
        },
        {
            'v1.2.5': None
        },
        {
            'v1.3.9': None
        },
        {
            'v1.4.2': None
        },
        {
            'v1.6.6': None
        },
        {
            'v1.7.2': None
        },
    ],
    'csharp': [
        #{'v1.0.1': None},
        {
            'v1.1.4': None
        },
        {
            'v1.2.5': None
        },
        {
            'v1.3.9': None
        },
        {
            'v1.4.2': None
        },
        {
            'v1.6.6': None
        },
        {
            'v1.7.2': None
        },
    ],
}
