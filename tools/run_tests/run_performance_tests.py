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

"""Run performance tests locally or remotely."""

import argparse
import itertools
import jobset
import json
import multiprocessing
import os
import pipes
import re
import subprocess
import sys
import tempfile
import time
import traceback
import uuid
import performance.scenario_config as scenario_config


_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(_ROOT)


_REMOTE_HOST_USERNAME = 'jenkins'


class QpsWorkerJob:
  """Encapsulates a qps worker server job."""

  def __init__(self, spec, language, host_and_port):
    self._spec = spec
    self.language = language
    self.host_and_port = host_and_port
    self._job = jobset.Job(spec, bin_hash=None, newline_on_success=True, travis=True, add_env={})

  def is_running(self):
    """Polls a job and returns True if given job is still running."""
    return self._job.state(jobset.NoCache()) == jobset._RUNNING

  def kill(self):
    return self._job.kill()


def create_qpsworker_job(language, shortname=None,
                         port=10000, remote_host=None):
  # TODO: support more languages
  cmdline = language.worker_cmdline() + ['--driver_port=%s' % port]
  if remote_host:
    user_at_host = '%s@%s' % (_REMOTE_HOST_USERNAME, remote_host)
    cmdline = ['ssh',
               str(user_at_host),
               'cd ~/performance_workspace/grpc/ && %s' % ' '.join(cmdline)]
    host_and_port='%s:%s' % (remote_host, port)
  else:
    host_and_port='localhost:%s' % port

  # TODO(jtattermusch): with some care, we can calculate the right timeout
  # of a worker from the sum of warmup + benchmark times for all the scenarios
  jobspec = jobset.JobSpec(
      cmdline=cmdline,
      shortname=shortname,
      timeout_seconds=30*60)
  return QpsWorkerJob(jobspec, language, host_and_port)


def create_scenario_jobspec(scenario_json, workers, remote_host=None,
                            bq_result_table=None):
  """Runs one scenario using QPS driver."""
  # setting QPS_WORKERS env variable here makes sure it works with SSH too.
  cmd = 'QPS_WORKERS="%s" ' % ','.join(workers)
  if bq_result_table:
    cmd += 'BQ_RESULT_TABLE="%s" ' % bq_result_table
  cmd += 'tools/run_tests/performance/run_qps_driver.sh '
  cmd += '--scenarios_json=%s ' % pipes.quote(json.dumps({'scenarios': [scenario_json]}))
  cmd += '--scenario_result_file=scenario_result.json'
  if remote_host:
    user_at_host = '%s@%s' % (_REMOTE_HOST_USERNAME, remote_host)
    cmd = 'ssh %s "cd ~/performance_workspace/grpc/ && "%s' % (user_at_host, pipes.quote(cmd))

  return jobset.JobSpec(
      cmdline=[cmd],
      shortname='qps_json_driver.%s' % scenario_json['name'],
      timeout_seconds=3*60,
      shell=True,
      verbose_success=True)


def create_quit_jobspec(workers, remote_host=None):
  """Runs quit using QPS driver."""
  # setting QPS_WORKERS env variable here makes sure it works with SSH too.
  cmd = 'QPS_WORKERS="%s" bins/opt/qps_json_driver --quit' % ','.join(workers)
  if remote_host:
    user_at_host = '%s@%s' % (_REMOTE_HOST_USERNAME, remote_host)
    cmd = 'ssh %s "cd ~/performance_workspace/grpc/ && "%s' % (user_at_host, pipes.quote(cmd))

  return jobset.JobSpec(
      cmdline=[cmd],
      shortname='qps_json_driver.quit',
      timeout_seconds=3*60,
      shell=True,
      verbose_success=True)


def archive_repo(languages):
  """Archives local version of repo including submodules."""
  cmdline=['tar', '-cf', '../grpc.tar', '../grpc/']
  if 'java' in languages:
    cmdline.append('../grpc-java')
  if 'go' in languages:
    cmdline.append('../grpc-go')

  archive_job = jobset.JobSpec(
      cmdline=cmdline,
      shortname='archive_repo',
      timeout_seconds=3*60)

  jobset.message('START', 'Archiving local repository.', do_newline=True)
  num_failures, _ = jobset.run(
      [archive_job], newline_on_success=True, maxjobs=1)
  if num_failures == 0:
    jobset.message('SUCCESS',
                   'Archive with local repository created successfully.',
                   do_newline=True)
  else:
    jobset.message('FAILED', 'Failed to archive local repository.',
                   do_newline=True)
    sys.exit(1)


def prepare_remote_hosts(hosts):
  """Prepares remote hosts."""
  prepare_jobs = []
  for host in hosts:
    user_at_host = '%s@%s' % (_REMOTE_HOST_USERNAME, host)
    prepare_jobs.append(
        jobset.JobSpec(
            cmdline=['tools/run_tests/performance/remote_host_prepare.sh'],
            shortname='remote_host_prepare.%s' % host,
            environ = {'USER_AT_HOST': user_at_host},
            timeout_seconds=5*60))
  jobset.message('START', 'Preparing remote hosts.', do_newline=True)
  num_failures, _ = jobset.run(
      prepare_jobs, newline_on_success=True, maxjobs=10)
  if num_failures == 0:
    jobset.message('SUCCESS',
                   'Remote hosts ready to start build.',
                   do_newline=True)
  else:
    jobset.message('FAILED', 'Failed to prepare remote hosts.',
                   do_newline=True)
    sys.exit(1)


def build_on_remote_hosts(hosts, languages=scenario_config.LANGUAGES.keys(), build_local=False):
  """Builds performance worker on remote hosts (and maybe also locally)."""
  build_timeout = 15*60
  build_jobs = []
  for host in hosts:
    user_at_host = '%s@%s' % (_REMOTE_HOST_USERNAME, host)
    build_jobs.append(
        jobset.JobSpec(
            cmdline=['tools/run_tests/performance/remote_host_build.sh'] + languages,
            shortname='remote_host_build.%s' % host,
            environ = {'USER_AT_HOST': user_at_host, 'CONFIG': 'opt'},
            timeout_seconds=build_timeout))
  if build_local:
    # Build locally as well
    build_jobs.append(
        jobset.JobSpec(
            cmdline=['tools/run_tests/performance/build_performance.sh'] + languages,
            shortname='local_build',
            environ = {'CONFIG': 'opt'},
            timeout_seconds=build_timeout))
  jobset.message('START', 'Building.', do_newline=True)
  num_failures, _ = jobset.run(
      build_jobs, newline_on_success=True, maxjobs=10)
  if num_failures == 0:
    jobset.message('SUCCESS',
                   'Built successfully.',
                   do_newline=True)
  else:
    jobset.message('FAILED', 'Build failed.',
                   do_newline=True)
    sys.exit(1)


def start_qpsworkers(languages, worker_hosts):
  """Starts QPS workers as background jobs."""
  if not worker_hosts:
    # run two workers locally (for each language)
    workers=[(None, 10000), (None, 10010)]
  elif len(worker_hosts) == 1:
    # run two workers on the remote host (for each language)
    workers=[(worker_hosts[0], 10000), (worker_hosts[0], 10010)]
  else:
    # run one worker per each remote host (for each language)
    workers=[(worker_host, 10000) for worker_host in worker_hosts]

  return [create_qpsworker_job(language,
                               shortname= 'qps_worker_%s_%s' % (language,
                                                                worker_idx),
                               port=worker[1] + language.worker_port_offset(),
                               remote_host=worker[0])
          for language in languages
          for worker_idx, worker in enumerate(workers)]


def create_scenarios(languages, workers_by_lang, remote_host=None, regex='.*',
                     bq_result_table=None):
  """Create jobspecs for scenarios to run."""
  all_workers = [worker
                 for workers in workers_by_lang.values()
                 for worker in workers]
  scenarios = []
  for language in languages:
    for scenario_json in language.scenarios():
      if re.search(args.regex, scenario_json['name']):
        workers = workers_by_lang[str(language)]
        # 'SERVER_LANGUAGE' is an indicator for this script to pick
        # a server in different language. It doesn't belong to the Scenario
        # schema, so we also need to remove it.
        custom_server_lang = scenario_json.pop('SERVER_LANGUAGE', None)
        if custom_server_lang:
          if not workers_by_lang.get(custom_server_lang, []):
            print 'Warning: Skipping scenario %s as' % scenario_json['name']
            print('SERVER_LANGUAGE is set to %s yet the language has '
                  'not been selected with -l' % custom_server_lang)
            continue
          for idx in range(0, scenario_json['num_servers']):
            # replace first X workers by workers of a different language
            workers[idx] = workers_by_lang[custom_server_lang][idx]
        scenario = create_scenario_jobspec(scenario_json,
                                           workers,
                                           remote_host=remote_host,
                                           bq_result_table=bq_result_table)
        scenarios.append(scenario)

  # the very last scenario requests shutting down the workers.
  scenarios.append(create_quit_jobspec(all_workers, remote_host=remote_host))
  return scenarios


def finish_qps_workers(jobs):
  """Waits for given jobs to finish and eventually kills them."""
  retries = 0
  while any(job.is_running() for job in jobs):
    for job in qpsworker_jobs:
      if job.is_running():
        print 'QPS worker "%s" is still running.' % job.host_and_port
    if retries > 10:
      print 'Killing all QPS workers.'
      for job in jobs:
        job.kill()
    retries += 1
    time.sleep(3)
  print 'All QPS workers finished.'


argp = argparse.ArgumentParser(description='Run performance tests.')
argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(scenario_config.LANGUAGES.keys()),
                  nargs='+',
                  default=['all'],
                  help='Languages to benchmark.')
argp.add_argument('--remote_driver_host',
                  default=None,
                  help='Run QPS driver on given host. By default, QPS driver is run locally.')
argp.add_argument('--remote_worker_host',
                  nargs='+',
                  default=[],
                  help='Worker hosts where to start QPS workers.')
argp.add_argument('-r', '--regex', default='.*', type=str,
                  help='Regex to select scenarios to run.')
argp.add_argument('--bq_result_table', default=None, type=str,
                  help='Bigquery "dataset.table" to upload results to.')

args = argp.parse_args()

languages = set(scenario_config.LANGUAGES[l]
                for l in itertools.chain.from_iterable(
                      scenario_config.LANGUAGES.iterkeys() if x == 'all' else [x]
                      for x in args.language))


# Put together set of remote hosts where to run and build
remote_hosts = set()
if args.remote_worker_host:
  for host in args.remote_worker_host:
    remote_hosts.add(host)
if args.remote_driver_host:
  remote_hosts.add(args.remote_driver_host)

if remote_hosts:
  archive_repo(languages=[str(l) for l in languages])
  prepare_remote_hosts(remote_hosts)

build_local = False
if not args.remote_driver_host:
  build_local = True
build_on_remote_hosts(remote_hosts, languages=[str(l) for l in languages], build_local=build_local)

qpsworker_jobs = start_qpsworkers(languages, args.remote_worker_host)

# TODO(jtattermusch): see https://github.com/grpc/grpc/issues/6174
time.sleep(5)

# get list of worker addresses for each language.
worker_addresses = dict([(str(language), []) for language in languages])
for job in qpsworker_jobs:
  worker_addresses[str(job.language)].append(job.host_and_port)

try:
  scenarios = create_scenarios(languages,
                               workers_by_lang=worker_addresses,
                               remote_host=args.remote_driver_host,
                               regex=args.regex,
                               bq_result_table=args.bq_result_table)
  if not scenarios:
    raise Exception('No scenarios to run')

  jobset.message('START', 'Running scenarios.', do_newline=True)
  num_failures, _ = jobset.run(
      scenarios, newline_on_success=True, maxjobs=1)
  if num_failures == 0:
    jobset.message('SUCCESS',
                   'All scenarios finished successfully.',
                   do_newline=True)
  else:
    jobset.message('FAILED', 'Some of the scenarios failed.',
                   do_newline=True)
    sys.exit(1)
except:
  traceback.print_exc()
  raise
finally:
  finish_qps_workers(qpsworker_jobs)
