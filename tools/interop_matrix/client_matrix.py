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

# Dictionary of runtimes per language
LANG_RUNTIME_MATRIX = {
    'cxx': ['cxx'],             # This is actually debian8.
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
        'v1.0.1',
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
        'v1.7.2',
    ],
    'go': [
        'v1.0.5',
        'v1.2.1',
        'v1.3.0',
        'v1.4.2',
        'v1.5.2',
        'v1.6.0',
        'v1.7.0',
        'v1.7.1',
        'v1.7.2',
        'v1.7.3',
        'v1.8.0',
    ],
    'java': [
        'v1.0.3',
        'v1.1.2',
        'v1.2.0',
        'v1.3.1',
        'v1.4.0',
        'v1.5.0',
        'v1.6.1',
        'v1.7.0',
        'v1.8.0',
    ],
    'python': [
        'v1.0.x',
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
    ],
    'python': [
        'v1.0.x', 
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
        'v1.7.2',    
    ],
    'python': [
        'v1.0.x',
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
        'v1.7.2',    
    ],
    'node': [
        'v1.0.1',
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
        'v1.7.1',
    ],
    'ruby': [
        # Ruby v1.0.x doesn't have the fix #8914, therefore not supported.
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
        'v1.7.2',
    ],
    'php': [
        'v1.0.1',
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
        'v1.7.2',
    ],
   'csharp': [
        #'v1.0.1',
        'v1.1.4',
        'v1.2.5',
        'v1.3.9',
        'v1.4.2',
        'v1.6.6',
        'v1.7.2',
    ],
}
