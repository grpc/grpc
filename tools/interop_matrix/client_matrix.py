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
  }.get(lang, 'git@github.com:grpc/grpc.git')

# Dictionary of runtimes per language
LANG_RUNTIME_MATRIX = {
    'go': ['go1.7', 'go1.8'],
    'java': ['java_oracle8'],
}

# Dictionary of releases per language.  For each language, we need to provide
# a tuple of release tag (used as the tag for the GCR image) and also github hash.
LANG_RELEASE_MATRIX = {
    'go': ['v1.0.1-GA', 'v1.3.0'],
    'java': ['v1.0.3', 'v1.1.2'],
}
