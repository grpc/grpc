#!/usr/bin/env python2.7
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

import argparse
import jobset
import multiprocessing
import os
import report_utils
import sys
from filter_pull_request_tests import filter_tests

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(_ROOT)

# Set the timeout high to allow enough time for sanitizers and pre-building
# clang docker.
_RUNTESTS_TIMEOUT = 2*60*60

# Number of jobs assigned to each run_tests.py instance
_INNER_JOBS = 2


def _docker_jobspec(name, runtests_args=[]):
  """Run a single instance of run_tests.py in a docker container"""
  test_job = jobset.JobSpec(
          cmdline=['python', 'tools/run_tests/run_tests.py',
                   '--use_docker',
                   '-t',
                   '-j', str(_INNER_JOBS),
                   '-x', 'report_%s.xml' % name,
                   '--report_suite_name', '%s' % name] + runtests_args,
          shortname='run_tests_%s' % name,
          timeout_seconds=_RUNTESTS_TIMEOUT)
  return test_job


def _workspace_jobspec(name, runtests_args=[], workspace_name=None):
  """Run a single instance of run_tests.py in a separate workspace"""
  if not workspace_name:
    workspace_name = 'workspace_%s' % name
  env = {'WORKSPACE_NAME': workspace_name}
  test_job = jobset.JobSpec(
          cmdline=['tools/run_tests/run_tests_in_workspace.sh',
                   '-t',
                   '-j', str(_INNER_JOBS),
                   '-x', '../report_%s.xml' % name,
                   '--report_suite_name', '%s' % name] + runtests_args,
          environ=env,
          shortname='run_tests_%s' % name,
          timeout_seconds=_RUNTESTS_TIMEOUT)
  return test_job


def _generate_jobs(languages, configs, platforms,
                  arch=None, compiler=None,
                  labels=[], extra_args=[]):
  result = []
  for language in languages:
    for platform in platforms:
      for config in configs:
        name = '%s_%s_%s' % (language, platform, config)
        runtests_args = ['-l', language,
                         '-c', config]
        if arch or compiler:
          name += '_%s_%s' % (arch, compiler)
          runtests_args += ['--arch', arch,
                            '--compiler', compiler]

        runtests_args += extra_args
        if platform == 'linux':
          job = _docker_jobspec(name=name, runtests_args=runtests_args)
        else:
          job = _workspace_jobspec(name=name, runtests_args=runtests_args)

        job.labels = [platform, config, language] + labels
        result.append(job)
  return result


def _create_test_jobs(extra_args=[]):
  test_jobs = []
  # supported on linux only
  test_jobs += _generate_jobs(languages=['sanity', 'php7'],
                             configs=['dbg', 'opt'],
                             platforms=['linux'],
                             labels=['basictests'],
                             extra_args=extra_args)
  
  # supported on all platforms.
  test_jobs += _generate_jobs(languages=['c', 'csharp', 'node', 'python'],
                             configs=['dbg', 'opt'],
                             platforms=['linux', 'macos', 'windows'],
                             labels=['basictests'],
                             extra_args=extra_args)
  
  # supported on linux and mac.
  test_jobs += _generate_jobs(languages=['c++', 'ruby', 'php'],
                              configs=['dbg', 'opt'],
                              platforms=['linux', 'macos'],
                              labels=['basictests'],
                              extra_args=extra_args)
  
  # supported on mac only.
  test_jobs += _generate_jobs(languages=['objc'],
                              configs=['dbg', 'opt'],
                              platforms=['macos'],
                              labels=['basictests'],
                              extra_args=extra_args)
  
  # sanitizers
  test_jobs += _generate_jobs(languages=['c'],
                              configs=['msan', 'asan', 'tsan'],
                              platforms=['linux'],
                              labels=['sanitizers'],
                              extra_args=extra_args)
  test_jobs += _generate_jobs(languages=['c++'],
                              configs=['asan', 'tsan'],
                              platforms=['linux'],
                              labels=['sanitizers'],
                              extra_args=extra_args)
  return test_jobs

  
def _create_portability_test_jobs(extra_args=[]):
  test_jobs = []
  # portability C x86
  test_jobs += _generate_jobs(languages=['c'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='x86',
                              compiler='default',
                              labels=['portability'],
                              extra_args=extra_args)
  
  # portability C and C++ on x64
  for compiler in ['gcc4.4', 'gcc4.6', 'gcc5.3',
                   'clang3.5', 'clang3.6', 'clang3.7']:
    test_jobs += _generate_jobs(languages=['c', 'c++'],
                                configs=['dbg'],
                                platforms=['linux'],
                                arch='x64',
                                compiler=compiler,
                                labels=['portability'],
                                extra_args=extra_args)
  
  # portability C on Windows
  for arch in ['x86', 'x64']:
    for compiler in ['vs2013', 'vs2015']:
      test_jobs += _generate_jobs(languages=['c'],
                                  configs=['dbg'],
                                  platforms=['windows'],
                                  arch=arch,
                                  compiler=compiler,
                                  labels=['portability'],
                                  extra_args=extra_args)
  
  test_jobs += _generate_jobs(languages=['python'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='default',
                              compiler='python3.4',
                              labels=['portability'],
                              extra_args=extra_args)
  
  test_jobs += _generate_jobs(languages=['csharp'],
                              configs=['dbg'],
                              platforms=['linux'],
                              arch='default',
                              compiler='coreclr',
                              labels=['portability'],
                              extra_args=extra_args)
  return test_jobs  


def _allowed_labels():
  """Returns a list of existing job labels."""
  all_labels = set()
  for job in _create_test_jobs() + _create_portability_test_jobs():
    for label in job.labels:
      all_labels.add(label)
  return sorted(all_labels)


argp = argparse.ArgumentParser(description='Run a matrix of run_tests.py tests.')
argp.add_argument('-j', '--jobs',
                  default=multiprocessing.cpu_count()/_INNER_JOBS,
                  type=int,
                  help='Number of concurrent run_tests.py instances.')
argp.add_argument('-f', '--filter',
                  choices=_allowed_labels(),
                  nargs='+',
                  default=[],
                  help='Filter targets to run by label with AND semantics.')
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
		  help='Filters out tests irrelavant to pull request changes.')
argp.add_argument('--base_branch',
                  default='origin/master',
                  type=str,
                  help='Branch that pull request is requesting to merge into')
args = argp.parse_args()

extra_args = []
if args.build_only:
  extra_args.append('--build_only')
if args.force_default_poller:
  extra_args.append('--force_default_poller')

all_jobs = _create_test_jobs(extra_args=extra_args) + _create_portability_test_jobs(extra_args=extra_args)

jobs = []
for job in all_jobs:
  if not args.filter or all(filter in job.labels for filter in args.filter):
    jobs.append(job)

if not jobs:
  jobset.message('FAILED', 'No test suites match given criteria.',
                 do_newline=True)
  sys.exit(1)
  
print('IMPORTANT: The changes you are testing need to be locally committed')
print('because only the committed changes in the current branch will be')
print('copied to the docker environment or into subworkspaces.')

print 
print 'Will run these tests:'
for job in jobs:
  if args.dry_run:
    print '  %s: "%s"' % (job.shortname, ' '.join(job.cmdline))
  else:
    print '  %s' % job.shortname
print

if args.filter_pr_tests:
  print 'IMPORTANT: Test filtering is not active; this is only for testing.'
  relevant_jobs = filter_tests(jobs, args.base_branch)
  # todo(mattkwong): add skipped tests to report.xml
  print
  if len(relevant_jobs) == len(jobs):
    print '(TESTING) No tests will be skipped.'
  else:
    print '(TESTING) These tests will be skipped:'
    for job in list(set(jobs) - set(relevant_jobs)):
      print '  %s' % job.shortname
  print

if args.dry_run:
  print '--dry_run was used, exiting'
  sys.exit(1)

jobset.message('START', 'Running test matrix.', do_newline=True)
num_failures, resultset = jobset.run(jobs,
                                     newline_on_success=True,
                                     travis=True,
                                     maxjobs=args.jobs)
report_utils.render_junit_xml_report(resultset, 'report.xml',
                                     suite_name='aggregate_tests')

if num_failures == 0:
  jobset.message('SUCCESS', 'All run_tests.py instance finished successfully.',
                 do_newline=True)
else:
  jobset.message('FAILED', 'Some run_tests.py instance have failed.',
                 do_newline=True)
  sys.exit(1)
