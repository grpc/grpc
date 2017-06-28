#!/usr/bin/env python
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

"""This sends out a warning if any changes to the bazel dir are made."""

from __future__ import print_function
from subprocess import check_output

import comment_on_pr
import os

_WARNING_MESSAGE = 'WARNING: You are making changes in the Bazel subdirectory. ' \
                   'Please get explicit approval from @nicolasnoble before merging.'


def _get_changed_files(base_branch):
  """
  Get list of changed files between current branch and base of target merge branch
  """
  # Get file changes between branch and merge-base of specified branch
  base_commit = check_output(["git", "merge-base", base_branch, "HEAD"]).rstrip()
  return check_output(["git", "diff", base_commit, "--name-only"]).splitlines()


# ghprbTargetBranch environment variable only available during a Jenkins PR tests
if 'ghprbTargetBranch' in os.environ:
  changed_files = _get_changed_files('origin/%s' % os.environ['ghprbTargetBranch'])
  if any(file.startswith('bazel/') for file in changed_files):
    comment_on_pr.comment_on_pr(_WARNING_MESSAGE)
