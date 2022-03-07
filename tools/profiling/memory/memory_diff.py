#!/usr/bin/env python3
#
# Copyright 2022 gRPC authors.
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

import argparse
import csv
import glob
import math
import multiprocessing
import os
import pathlib
import shutil
import subprocess
import sys
import re

sys.path.append(
    os.path.join(os.path.dirname(sys.argv[0]), '..', '..', 'run_tests',
                 'python_utils'))
import check_on_pr

argp = argparse.ArgumentParser(description='Perform diff on microbenchmarks')

argp.add_argument('-d',
                  '--diff_base',
                  type=str,
                  help='Commit or branch to compare the current one to')

argp.add_argument('-j', '--jobs', type=int, default=multiprocessing.cpu_count())

args = argp.parse_args()

_INTERESTING = {
    'client call': (rb'client call memory usage: ([0-9\.]+) bytes per call', float),
    'server call': (rb'server call memory usage: ([0-9\.]+) bytes per call', float),
}


def _run():
    """Build with Bazel, then run, and extract interesting lines from the output."""
    subprocess.check_call([
        'bazel', 'build', '-c', 'opt',
        'test/core/memory_usage/memory_usage_test'
    ])
    output = subprocess.check_output([
        'bazel-bin/test/core/memory_usage/memory_usage_test',
        '--warmup=10000',
        '--benchmark=50000',
    ])
    ret = {}
    for line in output.splitlines():
        for key, (pattern, conversion) in _INTERESTING.items():
            m = re.match(pattern, line)
            if m:
                ret[key] = conversion(m.group(1))
    return ret

cur = _run()
new = None

print(cur)

if args.diff_base:
    where_am_i = subprocess.check_output(
        ['git', 'rev-parse', '--abbrev-ref', 'HEAD']).decode().strip()
    # checkout the diff base (="old")
    subprocess.check_call(['git', 'checkout', args.diff_base])
    subprocess.check_call(['git', 'submodule', 'update'])
    try:
        new = _run()
    finally:
        # restore the original revision (="new")
        subprocess.check_call(['git', 'checkout', where_am_i])
        subprocess.check_call(['git', 'submodule', 'update'])

text = ''
if new is None:
    for key, value in cur.items():
        text += '{}: {}\n'.format(key, value)
else:
    diff_size = 0
    for key, value in _INTERESTING.items():
        if key in cur:
            if key not in new:
                text += '{}: {}\n'.format(key, value)
            else:
                diff_size += cur[key] - new[key]
                text += '{}: {} -> {}\n'.format(key, cur[key], new[key])

    print("DIFF_SIZE: %f" % diff_size)
    check_on_pr.label_increase_decrease_on_pr('per-call-memory', diff_size, 64)

print(text)
check_on_pr.check_on_pr('Memory Difference', '```\n%s\n```' % text)
