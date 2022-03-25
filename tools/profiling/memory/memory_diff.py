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
import re
import shutil
import subprocess
import sys

sys.path.append(
    os.path.join(os.path.dirname(sys.argv[0]), '..', '..', 'run_tests',
                 'python_utils'))
import check_on_pr

argp = argparse.ArgumentParser(description='Perform diff on memory benchmarks')

argp.add_argument('-d',
                  '--diff_base',
                  type=str,
                  help='Commit or branch to compare the current one to')

argp.add_argument('-j', '--jobs', type=int, default=multiprocessing.cpu_count())

args = argp.parse_args()

_INTERESTING = {
    'client call':
        (rb'client call memory usage: ([0-9\.]+) bytes per call', float),
    'server call':
        (rb'server call memory usage: ([0-9\.]+) bytes per call', float),
}

_SCENARIOS = {
    'default': [],
    'minstack': ['--minstack'],
}


def _run():
    """Build with Bazel, then run, and extract interesting lines from the output."""
    subprocess.check_call([
        'tools/bazel', 'build', '-c', 'opt',
        'test/core/memory_usage/memory_usage_test'
    ])
    ret = {}
    for scenario, extra_args in _SCENARIOS.items():
        try:
            output = subprocess.check_output([
                'bazel-bin/test/core/memory_usage/memory_usage_test',
                '--warmup=10000',
                '--benchmark=50000',
            ] + extra_args)
        except subprocess.CalledProcessError as e:
            print('Error running benchmark:', e)
            continue
        for line in output.splitlines():
            for key, (pattern, conversion) in _INTERESTING.items():
                m = re.match(pattern, line)
                if m:
                    ret[scenario + ': ' + key] = conversion(m.group(1))
    return ret


cur = _run()
old = None

if args.diff_base:
    where_am_i = subprocess.check_output(
        ['git', 'rev-parse', '--abbrev-ref', 'HEAD']).decode().strip()
    # checkout the diff base (="old")
    subprocess.check_call(['git', 'checkout', args.diff_base])
    try:
        old = _run()
    finally:
        # restore the original revision (="cur")
        subprocess.check_call(['git', 'checkout', where_am_i])

text = ''
if old is None:
    print(cur)
    for key, value in sorted(cur.items()):
        text += '{}: {}\n'.format(key, value)
else:
    print(cur, old)
    diff_size = 0
    for scenario in _SCENARIOS.keys():
        for key, value in sorted(_INTERESTING.items()):
            key = scenario + ': ' + key
            if key in cur:
                if key not in old:
                    text += '{}: {}\n'.format(key, cur[key])
                else:
                    diff_size += cur[key] - old[key]
                    text += '{}: {} -> {}\n'.format(key, old[key], cur[key])

    print("DIFF_SIZE: %f" % diff_size)
    check_on_pr.label_increase_decrease_on_pr('per-call-memory', diff_size, 64)

print(text)
check_on_pr.check_on_pr('Memory Difference', '```\n%s\n```' % text)
