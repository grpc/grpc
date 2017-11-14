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

import sys
import os
import subprocess
import argparse
import multiprocessing

sys.path.append(
  os.path.join(
    os.path.dirname(sys.argv[0]), '..', 'run_tests', 'python_utils'))
import jobset

GRPC_CHECKS = [
  'modernize-use-nullptr',
]

extra_args = [
  '-x',
  'c++',
  '-std=c++11',
]
with open('.clang_complete') as f:
  for line in f:
    line = line.strip()
    if line.startswith('-I'):
      extra_args.append(line)

clang_tidy = os.environ.get('CLANG_TIDY', 'clang-tidy')

argp = argparse.ArgumentParser(description='Run clang-tidy against core')
argp.add_argument('files', nargs='+', help='Files to tidy')
argp.add_argument('--fix', dest='fix', action='store_true')
argp.add_argument('-j', '--jobs', type=int, default=multiprocessing.cpu_count(),
                  help='Number of CPUs to use')
argp.set_defaults(fix=False)
args = argp.parse_args()

cmdline = [
    clang_tidy,
    '--checks=-*,%s' % ','.join(GRPC_CHECKS),
    '--warnings-as-errors=%s' % ','.join(GRPC_CHECKS)
] + [
    '--extra-arg-before=%s' % arg
    for arg in extra_args
]

if args.fix:
  cmdline.append('--fix')

jobs = []
for filename in args.files:
  jobs.append(jobset.JobSpec(cmdline + [filename],
                             shortname=filename,
                             ))#verbose_success=True))

jobset.run(jobs, maxjobs=args.jobs)
