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
git_submodules = subprocess.check_output('git submodule', shell=True).strip().split('\n')
git_submodule_hashes = {re.search(git_hash_pattern, s).group() for s in git_submodules}

# Parse git hashes from Bazel WORKSPACE {new_}http_archive rules
with open('WORKSPACE', 'r') as f:
  workspace_rules = [expr.value for expr in ast.parse(f.read()).body]

http_archive_rules = [rule for rule in workspace_rules if rule.func.id.endswith('http_archive')]
archive_urls = [kw.value.s for rule in http_archive_rules for kw in rule.keywords if kw.arg == 'url']
workspace_git_hashes = {re.search(git_hash_pattern, url).group() for url in archive_urls}

# Validate the equivalence of the git submodules and Bazel git dependencies. The
# condition we impose is that there is a git submodule for every dependency in
# the workspace, but not necessarily conversely. E.g. Bloaty is a dependency
# not used by any of the targets built by Bazel.
if len(workspace_git_hashes - git_submodule_hashes) > 0:
    print("Found discrepancies between git submodules and Bazel WORKSPACE dependencies")
    sys.exit(1)

sys.exit(0)
