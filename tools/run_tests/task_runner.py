#!/usr/bin/env python3
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
"""Runs selected gRPC test/build tasks."""

from __future__ import print_function

import argparse
import multiprocessing
import sys

import artifacts.artifact_targets as artifact_targets
import artifacts.distribtest_targets as distribtest_targets
import artifacts.package_targets as package_targets
import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

_TARGETS = []
_TARGETS += artifact_targets.targets()
_TARGETS += distribtest_targets.targets()
_TARGETS += package_targets.targets()


def _create_build_map():
    """Maps task names and labels to list of tasks to be built."""
    target_build_map = dict([(target.name, [target]) for target in _TARGETS])
    if len(_TARGETS) > len(list(target_build_map.keys())):
        raise Exception('Target names need to be unique')

    label_build_map = {}
    label_build_map['all'] = [t for t in _TARGETS]  # to build all targets
    for target in _TARGETS:
        for label in target.labels:
            if label in label_build_map:
                label_build_map[label].append(target)
            else:
                label_build_map[label] = [target]

    if set(target_build_map.keys()).intersection(list(label_build_map.keys())):
        raise Exception('Target names need to be distinct from label names')
    return dict(list(target_build_map.items()) + list(label_build_map.items()))


_BUILD_MAP = _create_build_map()

argp = argparse.ArgumentParser(description='Runs build/test targets.')
argp.add_argument('-b',
                  '--build',
                  choices=sorted(_BUILD_MAP.keys()),
                  nargs='+',
                  default=['all'],
                  help='Target name or target label to build.')
argp.add_argument('-f',
                  '--filter',
                  choices=sorted(_BUILD_MAP.keys()),
                  nargs='+',
                  default=[],
                  help='Filter targets to build with AND semantics.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('-x',
                  '--xml_report',
                  default='report_taskrunner_sponge_log.xml',
                  type=str,
                  help='Filename for the JUnit-compatible XML report')
argp.add_argument('--dry_run',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Only print what would be run.')
argp.add_argument(
    '--inner_jobs',
    default=None,
    type=int,
    help=
    'Number of parallel jobs to use by each target. Passed as build_jobspec(inner_jobs=N) to each target.'
)

args = argp.parse_args()

# Figure out which targets to build
targets = []
for label in args.build:
    targets += _BUILD_MAP[label]

# Among targets selected by -b, filter out those that don't match the filter
targets = [t for t in targets if all(f in t.labels for f in args.filter)]

print('Will build %d targets:' % len(targets))
for target in targets:
    print('  %s, labels %s' % (target.name, target.labels))
print()

if args.dry_run:
    print('--dry_run was used, exiting')
    sys.exit(1)

# Execute pre-build phase
prebuild_jobs = []
for target in targets:
    prebuild_jobs += target.pre_build_jobspecs()
if prebuild_jobs:
    num_failures, _ = jobset.run(prebuild_jobs,
                                 newline_on_success=True,
                                 maxjobs=args.jobs)
    if num_failures != 0:
        jobset.message('FAILED', 'Pre-build phase failed.', do_newline=True)
        sys.exit(1)

build_jobs = []
for target in targets:
    build_jobs.append(target.build_jobspec(inner_jobs=args.inner_jobs))
if not build_jobs:
    print('Nothing to build.')
    sys.exit(1)

jobset.message('START', 'Building targets.', do_newline=True)
num_failures, resultset = jobset.run(build_jobs,
                                     newline_on_success=True,
                                     maxjobs=args.jobs)
report_utils.render_junit_xml_report(resultset,
                                     args.xml_report,
                                     suite_name='tasks')
if num_failures == 0:
    jobset.message('SUCCESS',
                   'All targets built successfully.',
                   do_newline=True)
else:
    jobset.message('FAILED', 'Failed to build targets.', do_newline=True)
    sys.exit(1)
