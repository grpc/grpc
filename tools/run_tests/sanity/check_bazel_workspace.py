#!/usr/bin/env python

# Copyright 2016 gRPC authors.
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

import ast
import os
import re
import subprocess
import sys

os.chdir(os.path.join(os.path.dirname(sys.argv[0]), '../../..'))

git_hash_pattern = re.compile('[0-9a-f]{40}')

# Parse git hashes from submodules
git_submodules = subprocess.check_output(
    'git submodule', shell=True).strip().split('\n')
git_submodule_hashes = {
    re.search(git_hash_pattern, s).group()
    for s in git_submodules
}

_BAZEL_TOOLCHAINS_DEP_NAME = 'com_github_bazelbuild_bazeltoolchains'
_TWISTED_TWISTED_DEP_NAME = 'com_github_twisted_twisted'
_YAML_PYYAML_DEP_NAME = 'com_github_yaml_pyyaml'
_TWISTED_INCREMENTAL_DEP_NAME = 'com_github_twisted_incremental'
_ZOPEFOUNDATION_ZOPE_INTERFACE_DEP_NAME = 'com_github_zopefoundation_zope_interface'
_TWISTED_CONSTANTLY_DEP_NAME = 'com_github_twisted_constantly'

_GRPC_DEP_NAMES = [
    'boringssl',
    'com_github_madler_zlib',
    'com_google_protobuf',
    'com_github_google_googletest',
    'com_github_gflags_gflags',
    'com_github_google_benchmark',
    'com_github_cares_cares',
    'com_google_absl',
    _BAZEL_TOOLCHAINS_DEP_NAME,
    _TWISTED_TWISTED_DEP_NAME,
    _YAML_PYYAML_DEP_NAME,
    _TWISTED_INCREMENTAL_DEP_NAME,
    _ZOPEFOUNDATION_ZOPE_INTERFACE_DEP_NAME,
    _TWISTED_CONSTANTLY_DEP_NAME,
]

_GRPC_BAZEL_ONLY_DEPS = [
    _BAZEL_TOOLCHAINS_DEP_NAME,
    _TWISTED_TWISTED_DEP_NAME,
    _YAML_PYYAML_DEP_NAME,
    _TWISTED_INCREMENTAL_DEP_NAME,
    _ZOPEFOUNDATION_ZOPE_INTERFACE_DEP_NAME,
    _TWISTED_CONSTANTLY_DEP_NAME,
]


class BazelEvalState(object):

    def __init__(self, names_and_urls, overridden_name=None):
        self.names_and_urls = names_and_urls
        self.overridden_name = overridden_name

    def http_archive(self, **args):
        self.archive(**args)

    def new_http_archive(self, **args):
        self.archive(**args)

    def bind(self, **args):
        pass

    def existing_rules(self):
        if self.overridden_name:
            return [self.overridden_name]
        return []

    def archive(self, **args):
        assert self.names_and_urls.get(args['name']) is None
        if args['name'] in _GRPC_BAZEL_ONLY_DEPS:
            self.names_and_urls[args['name']] = 'dont care'
            return
        self.names_and_urls[args['name']] = args['url']


# Parse git hashes from bazel/grpc_deps.bzl {new_}http_archive rules
with open(os.path.join('bazel', 'grpc_deps.bzl'), 'r') as f:
    names_and_urls = {}
    eval_state = BazelEvalState(names_and_urls)
    bazel_file = f.read()

# grpc_deps.bzl only defines 'grpc_deps' and 'grpc_test_only_deps', add these
# lines to call them.
bazel_file += '\ngrpc_deps()\n'
bazel_file += '\ngrpc_test_only_deps()\n'
build_rules = {
    'native': eval_state,
}
exec bazel_file in build_rules
for name in _GRPC_DEP_NAMES:
    assert name in names_and_urls.keys()
assert len(_GRPC_DEP_NAMES) == len(names_and_urls.keys())

# There are some "bazel-only" deps that are exceptions to this sanity check,
# we don't require that there is a corresponding git module for these.
names_without_bazel_only_deps = names_and_urls.keys()
for dep_name in _GRPC_BAZEL_ONLY_DEPS:
    names_without_bazel_only_deps.remove(dep_name)
archive_urls = [names_and_urls[name] for name in names_without_bazel_only_deps]
workspace_git_hashes = {
    re.search(git_hash_pattern, url).group()
    for url in archive_urls
}
if len(workspace_git_hashes) == 0:
    print("(Likely) parse error, did not find any bazel git dependencies.")
    sys.exit(1)

# Validate the equivalence of the git submodules and Bazel git dependencies. The
# condition we impose is that there is a git submodule for every dependency in
# the workspace, but not necessarily conversely. E.g. Bloaty is a dependency
# not used by any of the targets built by Bazel.
if len(workspace_git_hashes - git_submodule_hashes) > 0:
    print(
        "Found discrepancies between git submodules and Bazel WORKSPACE dependencies"
    )
    sys.exit(1)

# Also check that we can override each dependency
for name in _GRPC_DEP_NAMES:
    names_and_urls_with_overridden_name = {}
    state = BazelEvalState(
        names_and_urls_with_overridden_name, overridden_name=name)
    rules = {
        'native': state,
    }
    exec bazel_file in rules
    assert name not in names_and_urls_with_overridden_name.keys()

sys.exit(0)
