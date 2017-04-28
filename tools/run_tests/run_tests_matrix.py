#!/usr/bin/env python
# Copyright 2015, Google Inc.
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

"""Run test matrix."""

from __future__ import print_function

import argparse
import multiprocessing
import os
import sys

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils
from python_utils.filter_pull_request_tests import filter_tests

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(_ROOT)

# Set the timeout high to allow enough time for sanitizers and pre-building
# clang docker.
_RUNTESTS_TIMEOUT = 4*60*60

# Number of jobs assigned to each run_tests.py instance
_DEFAULT_INNER_JOBS = 2

# report suffix is important for reports to get picked up by internal CI
_REPORT_SUFFIX = 'sponge_log.xml'


def _docker_jobspec(name, runtests_args=[], runtests_envs={},
                    inner_jobs=_DEFAULT_INNER_JOBS):
  """Run a single instance of run_tests.py in a docker container"""
  test_job = jobset.JobSpec(
          cmdline=['python', 'tools/run_tests/run_tests.py',
                   '--use_docker',
                   '-t',
                   '-j', str(inner_jobs),
                   '-x', 'report_%s_%s' % (name, _REPORT_SUFFIX),
                   '--report_suite_name', '%s' % name] + runtests_args,
          environ=runtests_envs,
          shortname='run_tests_%s' % name,
          timeout_seconds=_RUNTESTS_TIMEOUT)
  return test_job


def _workspace_jobspec(name, runtests_args=[], workspace_name=None,
                       runtests_envs={}, inner_jobs=_DEFAULT_INNER_JOBS):
  """Run a single instance of run_tests.py in a separate workspace"""
  if not workspace_name:
    workspace_name = 'workspace_%s' % name
  env = {'WORKSPACE_NAME': workspace_name}
  env.update(runtests_envs)
  test_job = jobset.JobSpec(
          cmdline=['bash',
                   'tools/run_tests/helper_scripts/run_tests_in_workspace.sh',
                   '-t',
                   '-j', str(inner_jobs),
                   '-x', '../report_%s_%s' % (name, _REPORT_SUFFIX),
                   '--report_suite_name', '%s' % name] + runtests_args,
          environ=env,
          shortname='run_tests_%s' % name,
          timeout_seconds=_RUNTESTS_TIMEOUT)
  return test_job


def _generate_jobs(languages, configs, platforms, iomgr_platform = 'native',
                  arch=None, compiler=None,
                  labels=[], extra_args=[], extra_envs={},
                  inner_jobs=_DEFAULT_INNER_JOBS):
  result = []
  for language in languages:
    for platform in platforms:
      for config in configs:
        name = '%s_%s_%s_%s' % (language, platform, config, iomgr_platform)
        runtests_args = ['-l', language,
                         '-c', config,
                         '--iomgr_platform', iomgr_platform]
        if arch or compiler:
          name += '_%s_%s' % (arch, compiler)
          runtests_args += ['--arch', arch,
                            '--compiler', compiler]
        for extra_env in extra_envs:
          name += '_%s_%s' % (extra_env, extra_envs[extra_env])

        runtests_args += extra_args
        if platform == 'linux':
          job = _docker_jobspec(name=name, runtests_args=runtests_args,
                                runtests_envs=extra_envs, inner_jobs=inner_jobs)
        else:
          job = _workspace_jobspec(name=name, runtests_args=runtests_args,
                                   runtests_envs=extra_envs, inner_jobs=inner_jobs)

        job.labels = [platform, config, language, iomgr_platform] + labels
        result.append(job)
  return result


def _create_test_jobs(extra_args=[], inner_jobs=_DEFAULT_INNER_JOBS):
  test_jobs = []
  # supported on linux only
  test_jobs += _generate_jobs(languages=['sanity', 'php7'],
                             configs=['dbg', 'opt'],
                             platforms=['linux'],
                             labels=['basictests'],
                             extra_args=extra_args,
                             inner_jobs=inner_jobs)

  # supported on all platforms.
  test_jobs += _generate_jobs(languages=['c', 'csharp', 'node', 'python'],
                             configs=['dbg', 'opt'],
                             platforms=['linux', 'macos', 'windows'],
                             labels=['basictests'],
                             extra_args=extra_args,
                             inner_jobs=inner_jobs)

  # supported on linux and mac.
  test_jobs += _generate_jobs(languages=['c++', 'ruby', 'php'],
                              configs=['dbg', 'opt'],
                              platforms=['linux', 'macos'],
                              labels=['basictests'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  # supported on mac only.
  test_jobs += _generate_jobs(languages=['objc'],
                              configs=['dbg', 'opt'],
                              platforms=['macos'],
                              labels=['basictests'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  # sanitizers
  test_jobs += _generate_jobs(languages=['c'],
                              configs=['msan', 'asan', 'tsan'],
                              platforms=['linux'],
                              labels=['sanitizers'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)
  test_jobs += _generate_jobs(languages=['c++'],
                              configs=['asan', 'tsan'],
                              platforms=['linux'],
                              labels=['sanitizers'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  return test_jobs


def _create_portability_test_jobs(extra_args=[], inner_jobs=_DEFAULT_INNER_JOBS):
  test_jobs = []
  # portability C x86
  test_jobs += _generate_jobs(languages=['c'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='x86',
                              compiler='default',
                              labels=['portability'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  # portability C and C++ on x64
  for compiler in ['gcc4.4', 'gcc4.6', 'gcc5.3', 'gcc_musl',
                   'clang3.5', 'clang3.6', 'clang3.7']:
    test_jobs += _generate_jobs(languages=['c'],
                                configs=['dbg'],
                                platforms=['linux'],
                                arch='x64',
                                compiler=compiler,
                                labels=['portability'],
                                extra_args=extra_args,
                                inner_jobs=inner_jobs)

  for compiler in ['gcc4.8', 'gcc5.3',
                   'clang3.5', 'clang3.6', 'clang3.7']:
    test_jobs += _generate_jobs(languages=['c++'],
                                configs=['dbg'],
                                platforms=['linux'],
                                arch='x64',
                                compiler=compiler,
                                labels=['portability'],
                                extra_args=extra_args,
                                inner_jobs=inner_jobs)

  # portability C on Windows
  for arch in ['x86', 'x64']:
    for compiler in ['vs2013', 'vs2015']:
      test_jobs += _generate_jobs(languages=['c'],
                                  configs=['dbg'],
                                  platforms=['windows'],
                                  arch=arch,
                                  compiler=compiler,
                                  labels=['portability'],
                                  extra_args=extra_args,
                                  inner_jobs=inner_jobs)

  # C and C++ with the c-ares DNS resolver on Linux
  test_jobs += _generate_jobs(languages=['c', 'c++'],
                              configs=['dbg'], platforms=['linux'],
                              labels=['portability'],
                              extra_args=extra_args,
                              extra_envs={'GRPC_DNS_RESOLVER': 'ares'})

  # TODO(zyc): Turn on this test after adding c-ares support on windows.
  # C with the c-ares DNS resolver on Windonws
  # test_jobs += _generate_jobs(languages=['c'],
  #                             configs=['dbg'], platforms=['windows'],
  #                             labels=['portability'],
  #                             extra_args=extra_args,
  #                             extra_envs={'GRPC_DNS_RESOLVER': 'ares'})

  # cmake build for C and C++
  # TODO(jtattermusch): some of the tests are failing, so we force --build_only
  # to make sure it's buildable at least.
  test_jobs += _generate_jobs(languages=['c', 'c++'],
                              configs=['dbg'],
                              platforms=['linux', 'windows'],
                              arch='default',
                              compiler='cmake',
                              labels=['portability'],
                              extra_args=extra_args + ['--build_only'],
                              inner_jobs=inner_jobs)

  test_jobs += _generate_jobs(languages=['python'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='default',
                              compiler='python3.4',
                              labels=['portability'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  test_jobs += _generate_jobs(languages=['csharp'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='default',
                              compiler='coreclr',
                              labels=['portability'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  test_jobs += _generate_jobs(languages=['c'],
                              configs=['dbg'],
                              platforms=['linux'],
                              iomgr_platform='uv',
                              labels=['portability'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  test_jobs += _generate_jobs(languages=['node'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='default',
                              compiler='electron1.6',
                              labels=['portability'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  test_jobs += _generate_jobs(languages=['node'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='default',
                              compiler='node4',
                              labels=['portability'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  test_jobs += _generate_jobs(languages=['node'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='default',
                              compiler='node6',
                              labels=['portability'],
                              extra_args=extra_args,
                              inner_jobs=inner_jobs)

  return test_jobs


def _allowed_labels():
  """Returns a list of existing job labels."""
  all_labels = set()
  for job in _create_test_jobs() + _create_portability_test_jobs():
    for label in job.labels:
      all_labels.add(label)
  return sorted(all_labels)


def _runs_per_test_type(arg_str):
  """Auxiliary function to parse the "runs_per_test" flag."""
  try:
    n = int(arg_str)
    if n <= 0: raise ValueError
    return n
  except:
    msg = '\'{}\' is not a positive integer'.format(arg_str)
    raise argparse.ArgumentTypeError(msg)


if __name__ == "__main__":
  argp = argparse.ArgumentParser(description='Run a matrix of run_tests.py tests.')
  argp.add_argument('-j', '--jobs',
                    default=multiprocessing.cpu_count()/_DEFAULT_INNER_JOBS,
                    type=int,
                    help='Number of concurrent run_tests.py instances.')
  argp.add_argument('-f', '--filter',
                    choices=_allowed_labels(),
                    nargs='+',
                    default=[],
                    help='Filter targets to run by label with AND semantics.')
  argp.add_argument('--exclude',
                    choices=_allowed_labels(),
                    nargs='+',
                    default=[],
                    help='Exclude targets with any of given labels.')
  argp.add_argument('--build_only',
                    default=False,
                    action='store_const',
                    const=True,
                    help='Pass --build_only flag to run_tests.py instances.')
  argp.add_argument('--force_default_poller', default=False, action='store_const', const=True,
                    help='Pass --force_default_poller to run_tests.py instances.')
  argp.add_argument('--dry_run',
                    default=False,
                    action='store_const',
                    const=True,
                    help='Only print what would be run.')
  argp.add_argument('--filter_pr_tests',
                    default=False,
                    action='store_const',
                    const=True,
                    help='Filters out tests irrelevant to pull request changes.')
  argp.add_argument('--base_branch',
                    default='origin/master',
                    type=str,
                    help='Branch that pull request is requesting to merge into')
  argp.add_argument('--inner_jobs',
                    default=_DEFAULT_INNER_JOBS,
                    type=int,
                    help='Number of jobs in each run_tests.py instance')
  argp.add_argument('-n', '--runs_per_test', default=1, type=_runs_per_test_type,
                    help='How many times to run each tests. >1 runs implies ' +
                    'omitting passing test from the output & reports.')
  argp.add_argument('--max_time', default=-1, type=int,
                    help='Maximum amount of time to run tests for' +
                         '(other tests will be skipped)')
  args = argp.parse_args()

  extra_args = []
  if args.build_only:
    extra_args.append('--build_only')
  if args.force_default_poller:
    extra_args.append('--force_default_poller')
  if args.runs_per_test > 1:
    extra_args.append('-n')
    extra_args.append('%s' % args.runs_per_test)
    extra_args.append('--quiet_success')
  if args.max_time > 0:
    extra_args.extend(('--max_time', '%d' % args.max_time))

  all_jobs = _create_test_jobs(extra_args=extra_args, inner_jobs=args.inner_jobs) + \
             _create_portability_test_jobs(extra_args=extra_args, inner_jobs=args.inner_jobs)

  jobs = []
  for job in all_jobs:
    if not args.filter or all(filter in job.labels for filter in args.filter):
      if not any(exclude_label in job.labels for exclude_label in args.exclude):
        jobs.append(job)

  if not jobs:
    jobset.message('FAILED', 'No test suites match given criteria.',
                   do_newline=True)
    sys.exit(1)

  print('IMPORTANT: The changes you are testing need to be locally committed')
  print('because only the committed changes in the current branch will be')
  print('copied to the docker environment or into subworkspaces.')

  skipped_jobs = []

  if args.filter_pr_tests:
    print('Looking for irrelevant tests to skip...')
    relevant_jobs = filter_tests(jobs, args.base_branch)
    if len(relevant_jobs) == len(jobs):
      print('No tests will be skipped.')
    else:
      print('These tests will be skipped:')
      skipped_jobs = list(set(jobs) - set(relevant_jobs))
      # Sort by shortnames to make printing of skipped tests consistent
      skipped_jobs.sort(key=lambda job: job.shortname)
      for job in list(skipped_jobs):
        print('  %s' % job.shortname)
    jobs = relevant_jobs

  print('Will run these tests:')
  for job in jobs:
    if args.dry_run:
      print('  %s: "%s"' % (job.shortname, ' '.join(job.cmdline)))
    else:
      print('  %s' % job.shortname)
  print

  if args.dry_run:
    print('--dry_run was used, exiting')
    sys.exit(1)

  jobset.message('START', 'Running test matrix.', do_newline=True)
  num_failures, resultset = jobset.run(jobs,
                                       newline_on_success=True,
                                       travis=True,
                                       maxjobs=args.jobs)
  # Merge skipped tests into results to show skipped tests on report.xml
  if skipped_jobs:
    ignored_num_skipped_failures, skipped_results = jobset.run(
        skipped_jobs, skip_jobs=True)
    resultset.update(skipped_results)
  report_utils.render_junit_xml_report(resultset, 'report_%s' % _REPORT_SUFFIX,
                                       suite_name='aggregate_tests')

  if num_failures == 0:
    jobset.message('SUCCESS', 'All run_tests.py instance finished successfully.',
                   do_newline=True)
  else:
    jobset.message('FAILED', 'Some run_tests.py instance have failed.',
                   do_newline=True)
    sys.exit(1)
