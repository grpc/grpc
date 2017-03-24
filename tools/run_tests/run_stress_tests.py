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
"""Run stress test in C++"""

from __future__ import print_function

import argparse
import atexit
import itertools
import json
import multiprocessing
import os
import re
import subprocess
import sys
import tempfile
import time
import uuid
import six

import python_utils.dockerjob as dockerjob
import python_utils.jobset as jobset

# Docker doesn't clean up after itself, so we do it on exit.
atexit.register(lambda: subprocess.call(['stty', 'echo']))

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

_DEFAULT_SERVER_PORT = 8080
_DEFAULT_METRICS_PORT = 8081
_DEFAULT_TEST_CASES = 'empty_unary:20,large_unary:20,client_streaming:20,server_streaming:20,empty_stream:20'
_DEFAULT_NUM_CHANNELS_PER_SERVER = 5
_DEFAULT_NUM_STUBS_PER_CHANNEL = 10

# 15 mins default
_DEFAULT_TEST_DURATION_SECS = 900

class CXXLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = 'cxx'

  def client_cmd(self, args):
    return ['bins/opt/stress_test'] + args

  def server_cmd(self, args):
    return ['bins/opt/interop_server'] + args

  def global_env(self):
    return {}

  def __str__(self):
    return 'c++'


_LANGUAGES = {'c++': CXXLanguage(),}

# languages supported as cloud_to_cloud servers
_SERVERS = ['c++']

DOCKER_WORKDIR_ROOT = '/var/local/git/grpc'


def docker_run_cmdline(cmdline, image, docker_args=[], cwd=None, environ=None):
  """Wraps given cmdline array to create 'docker run' cmdline from it."""
  docker_cmdline = ['docker', 'run', '-i', '--rm=true']

  # turn environ into -e docker args
  if environ:
    for k, v in environ.items():
      docker_cmdline += ['-e', '%s=%s' % (k, v)]

  # set working directory
  workdir = DOCKER_WORKDIR_ROOT
  if cwd:
    workdir = os.path.join(workdir, cwd)
  docker_cmdline += ['-w', workdir]

  docker_cmdline += docker_args + [image] + cmdline
  return docker_cmdline


def bash_login_cmdline(cmdline):
  """Creates bash -l -c cmdline from args list."""
  # Use login shell:
  # * rvm and nvm require it
  # * makes error messages clearer if executables are missing
  return ['bash', '-l', '-c', ' '.join(cmdline)]


def _job_kill_handler(job):
  if job._spec.container_name:
    dockerjob.docker_kill(job._spec.container_name)
    # When the job times out and we decide to kill it,
    # we need to wait a before restarting the job
    # to prevent "container name already in use" error.
    # TODO(jtattermusch): figure out a cleaner way to to this.
    time.sleep(2)


def cloud_to_cloud_jobspec(language,
                           test_cases,
                           server_addresses,
                           test_duration_secs,
                           num_channels_per_server,
                           num_stubs_per_channel,
                           metrics_port,
                           docker_image=None):
  """Creates jobspec for cloud-to-cloud interop test"""
  cmdline = bash_login_cmdline(language.client_cmd([
      '--test_cases=%s' % test_cases, '--server_addresses=%s' %
      server_addresses, '--test_duration_secs=%s' % test_duration_secs,
      '--num_stubs_per_channel=%s' % num_stubs_per_channel,
      '--num_channels_per_server=%s' % num_channels_per_server,
      '--metrics_port=%s' % metrics_port
  ]))
  print(cmdline)
  cwd = language.client_cwd
  environ = language.global_env()
  if docker_image:
    container_name = dockerjob.random_name('interop_client_%s' %
                                           language.safename)
    cmdline = docker_run_cmdline(
        cmdline,
        image=docker_image,
        environ=environ,
        cwd=cwd,
        docker_args=['--net=host', '--name', container_name])
    cwd = None

  test_job = jobset.JobSpec(cmdline=cmdline,
                            cwd=cwd,
                            environ=environ,
                            shortname='cloud_to_cloud:%s:%s_server:stress_test' % (
                                language, server_name),
                            timeout_seconds=test_duration_secs * 2,
                            flake_retries=0,
                            timeout_retries=0,
                            kill_handler=_job_kill_handler)
  test_job.container_name = container_name
  return test_job


def server_jobspec(language, docker_image, test_duration_secs):
  """Create jobspec for running a server"""
  container_name = dockerjob.random_name('interop_server_%s' %
                                         language.safename)
  cmdline = bash_login_cmdline(language.server_cmd(['--port=%s' %
                                                    _DEFAULT_SERVER_PORT]))
  environ = language.global_env()
  docker_cmdline = docker_run_cmdline(
      cmdline,
      image=docker_image,
      cwd=language.server_cwd,
      environ=environ,
      docker_args=['-p', str(_DEFAULT_SERVER_PORT), '--name', container_name])

  server_job = jobset.JobSpec(cmdline=docker_cmdline,
                              environ=environ,
                              shortname='interop_server_%s' % language,
                              timeout_seconds=test_duration_secs * 3)
  server_job.container_name = container_name
  return server_job


def build_interop_stress_image_jobspec(language, tag=None):
  """Creates jobspec for building stress test docker image for a language"""
  if not tag:
    tag = 'grpc_interop_stress_%s:%s' % (language.safename, uuid.uuid4())
  env = {'INTEROP_IMAGE': tag,
         'BASE_NAME': 'grpc_interop_stress_%s' % language.safename}
  build_job = jobset.JobSpec(cmdline=['tools/run_tests/dockerize/build_interop_stress_image.sh'],
                             environ=env,
                             shortname='build_docker_%s' % (language),
                             timeout_seconds=30 * 60)
  build_job.tag = tag
  return build_job

argp = argparse.ArgumentParser(description='Run stress tests.')
argp.add_argument('-l',
                  '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'],
                  help='Clients to run.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument(
    '-s',
    '--server',
    choices=['all'] + sorted(_SERVERS),
    action='append',
    help='Run cloud_to_cloud servers in a separate docker ' + 'image.',
    default=[])
argp.add_argument(
    '--override_server',
    action='append',
    type=lambda kv: kv.split('='),
    help=
    'Use servername=HOST:PORT to explicitly specify a server. E.g. '
    'csharp=localhost:50000',
    default=[])
argp.add_argument('--test_duration_secs',
                  help='The duration of the test in seconds',
                  default=_DEFAULT_TEST_DURATION_SECS)

args = argp.parse_args()

servers = set(
    s
    for s in itertools.chain.from_iterable(_SERVERS if x == 'all' else [x]
                                           for x in args.server))

languages = set(_LANGUAGES[l] for l in itertools.chain.from_iterable(
  six.iterkeys(_LANGUAGES) if x == 'all' else [x] for x in args.language))

docker_images = {}
# languages for which to build docker images
languages_to_build = set(
    _LANGUAGES[k]
    for k in set([str(l) for l in languages] + [s for s in servers]))
build_jobs = []
for l in languages_to_build:
  job = build_interop_stress_image_jobspec(l)
  docker_images[str(l)] = job.tag
  build_jobs.append(job)

if build_jobs:
  jobset.message('START', 'Building interop docker images.', do_newline=True)
  num_failures, _ = jobset.run(build_jobs,
                               newline_on_success=True,
                               maxjobs=args.jobs)
  if num_failures == 0:
    jobset.message('SUCCESS',
                   'All docker images built successfully.',
                   do_newline=True)
  else:
    jobset.message('FAILED',
                   'Failed to build interop docker images.',
                   do_newline=True)
    for image in six.itervalues(docker_images):
      dockerjob.remove_image(image, skip_nonexistent=True)
    sys.exit(1)

# Start interop servers.
server_jobs = {}
server_addresses = {}
try:
  for s in servers:
    lang = str(s)
    spec = server_jobspec(_LANGUAGES[lang], docker_images.get(lang), args.test_duration_secs)
    job = dockerjob.DockerJob(spec)
    server_jobs[lang] = job
    server_addresses[lang] = ('localhost',
                              job.mapped_port(_DEFAULT_SERVER_PORT))

  jobs = []

  for server in args.override_server:
    server_name = server[0]
    (server_host, server_port) = server[1].split(':')
    server_addresses[server_name] = (server_host, server_port)

  for server_name, server_address in server_addresses.items():
    (server_host, server_port) = server_address
    for language in languages:
      test_job = cloud_to_cloud_jobspec(
          language,
          _DEFAULT_TEST_CASES,
          ('%s:%s' % (server_host, server_port)),
          args.test_duration_secs,
          _DEFAULT_NUM_CHANNELS_PER_SERVER,
          _DEFAULT_NUM_STUBS_PER_CHANNEL,
          _DEFAULT_METRICS_PORT,
          docker_image=docker_images.get(str(language)))
      jobs.append(test_job)

  if not jobs:
    print('No jobs to run.')
    for image in six.itervalues(docker_images):
      dockerjob.remove_image(image, skip_nonexistent=True)
    sys.exit(1)

  num_failures, resultset = jobset.run(jobs,
                                       newline_on_success=True,
                                       maxjobs=args.jobs)
  if num_failures:
    jobset.message('FAILED', 'Some tests failed', do_newline=True)
  else:
    jobset.message('SUCCESS', 'All tests passed', do_newline=True)

finally:
  # Check if servers are still running.
  for server, job in server_jobs.items():
    if not job.is_running():
      print('Server "%s" has exited prematurely.' % server)

  dockerjob.finish_jobs([j for j in six.itervalues(server_jobs)])

  for image in six.itervalues(docker_images):
    print('Removing docker image %s' % image)
    dockerjob.remove_image(image)
