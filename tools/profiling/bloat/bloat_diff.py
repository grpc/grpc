#!/usr/bin/env python2.7
#
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

import argparse
import glob
import multiprocessing
import os
import shutil
import subprocess
import sys

sys.path.append(
    os.path.join(
        os.path.dirname(sys.argv[0]), '..', '..', 'run_tests', 'python_utils'))
import comment_on_pr

argp = argparse.ArgumentParser(description='Perform diff on microbenchmarks')

argp.add_argument(
    '-d',
    '--diff_base',
    type=str,
    help='Commit or branch to compare the current one to')

argp.add_argument('-j', '--jobs', type=int, default=multiprocessing.cpu_count())

args = argp.parse_args()

LIBS = [
    'libgrpc.so',
    'libgrpc++.so',
]


def build(where):
    subprocess.check_call('make -j%d' % args.jobs, shell=True, cwd='.')
    shutil.rmtree('bloat_diff_%s' % where, ignore_errors=True)
    os.rename('libs', 'bloat_diff_%s' % where)


build('new')

if args.diff_base:
    old = 'old'
    where_am_i = subprocess.check_output(
        ['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip()
    subprocess.check_call(['git', 'checkout', args.diff_base])
    subprocess.check_call(['git', 'submodule', 'update'])
    try:
        try:
            build('old')
        except subprocess.CalledProcessError, e:
            subprocess.check_call(['make', 'clean'])
            build('old')
    finally:
        subprocess.check_call(['git', 'checkout', where_am_i])
        subprocess.check_call(['git', 'submodule', 'update'])

subprocess.check_call(
    'make -j%d' % args.jobs, shell=True, cwd='third_party/bloaty')

text = ''
for lib in LIBS:
    text += '****************************************************************\n\n'
    text += lib + '\n\n'
    old_version = glob.glob('bloat_diff_old/opt/%s' % lib)
    new_version = glob.glob('bloat_diff_new/opt/%s' % lib)
    assert len(new_version) == 1
    cmd = 'third_party/bloaty/bloaty -d compileunits,symbols'
    if old_version:
        assert len(old_version) == 1
        text += subprocess.check_output(
            '%s %s -- %s' % (cmd, new_version[0], old_version[0]), shell=True)
    else:
        text += subprocess.check_output(
            '%s %s' % (cmd, new_version[0]), shell=True)
    text += '\n\n'

print text
comment_on_pr.comment_on_pr('```\n%s\n```' % text)
