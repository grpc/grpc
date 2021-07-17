#!/usr/bin/env python3
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
    os.path.join(os.path.dirname(sys.argv[0]), '..', 'run_tests',
                 'python_utils'))
import jobset

clang_tidy = os.environ.get('CLANG_TIDY', 'clang-tidy')

argp = argparse.ArgumentParser(description='Run clang-tidy against core')
argp.add_argument('files', nargs='+', help='Files to tidy')
argp.add_argument('--fix', dest='fix', action='store_true')
argp.add_argument('-j',
                  '--jobs',
                  type=int,
                  default=multiprocessing.cpu_count(),
                  help='Number of CPUs to use')
argp.set_defaults(fix=False)
args = argp.parse_args()

# Explicitly passing the .clang-tidy config by reading it.
# This is required because source files in the compilation database are
# in a different source tree so clang-tidy cannot find the right config file
# by seeking their parent directories.
with open(".clang-tidy") as f:
    config = f.read()
cmdline = [
    clang_tidy,
    '--config=' + config,
]

if args.fix:
    cmdline.append('--fix')

jobs = []
for filename in args.files:
    jobs.append(
        jobset.JobSpec(
            cmdline + [filename],
            shortname=filename,
            timeout_seconds=15 * 60,
        ))

num_fails, res_set = jobset.run(jobs, maxjobs=args.jobs, quiet_success=True)
sys.exit(num_fails)
