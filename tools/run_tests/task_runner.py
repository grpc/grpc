#!/usr/bin/env python2.7
# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Runs selected gRPC test/build tasks."""

import argparse
import atexit
import jobset
import multiprocessing
import sys

import artifact_targets
import distribtest_targets
import package_targets

_TARGETS = []
_TARGETS += artifact_targets.targets()
_TARGETS += distribtest_targets.targets()
_TARGETS += package_targets.targets()

def _create_build_map():
  """Maps task names and labels to list of tasks to be built."""
  target_build_map = dict([(target.name, [target])
                           for target in _TARGETS])
  if len(_TARGETS) > len(target_build_map.keys()):
    raise Exception('Target names need to be unique')

  label_build_map = {}
  label_build_map['all'] = [t for t in _TARGETS]  # to build all targets
  for target in _TARGETS:
    for label in target.labels:
      if label in label_build_map:
        label_build_map[label].append(target)
      else:
        label_build_map[label] = [target]

  if set(target_build_map.keys()).intersection(label_build_map.keys()):
    raise Exception('Target names need to be distinct from label names')
  return dict( target_build_map.items() + label_build_map.items())


_BUILD_MAP = _create_build_map()

argp = argparse.ArgumentParser(description='Runs build/test targets.')
argp.add_argument('-b', '--build',
                  choices=sorted(_BUILD_MAP.keys()),
                  nargs='+',
                  default=['all'],
                  help='Target name or target label to build.')
argp.add_argument('-f', '--filter',
                  choices=sorted(_BUILD_MAP.keys()),
                  nargs='+',
                  default=[],
                  help='Filter targets to build with AND semantics.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('-t', '--travis',
                  default=False,
                  action='store_const',
                  const=True)

args = argp.parse_args()

# Figure out which targets to build
targets = []
for label in args.build:
  targets += _BUILD_MAP[label]

# Among targets selected by -b, filter out those that don't match the filter
targets = [t for t in targets if all(f in t.labels for f in args.filter)]
targets = sorted(set(targets))

# Execute pre-build phase
prebuild_jobs = []
for target in targets:
  prebuild_jobs += target.pre_build_jobspecs()
if prebuild_jobs:
  num_failures, _ = jobset.run(
    prebuild_jobs, newline_on_success=True, maxjobs=args.jobs)
  if num_failures != 0:
    jobset.message('FAILED', 'Pre-build phase failed.', do_newline=True)
    sys.exit(1)

build_jobs = []
for target in targets:
  build_jobs.append(target.build_jobspec())
if not build_jobs:
  print 'Nothing to build.'
  sys.exit(1)

jobset.message('START', 'Building targets.', do_newline=True)
num_failures, _ = jobset.run(
    build_jobs, newline_on_success=True, maxjobs=args.jobs)
if num_failures == 0:
  jobset.message('SUCCESS', 'All targets built successfully.',
                 do_newline=True)
else:
  jobset.message('FAILED', 'Failed to build targets.',
                 do_newline=True)
  sys.exit(1)
