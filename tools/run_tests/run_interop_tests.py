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

"""Run interop (cross-language) tests in parallel."""

from __future__ import print_function

import argparse
import atexit
import itertools
import json
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

import python_utils.dockerjob as dockerjob
import python_utils.jobset as jobset
import python_utils.report_utils as report_utils

# Docker doesn't clean up after itself, so we do it on exit.
atexit.register(lambda: subprocess.call(['stty', 'echo']))

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

_DEFAULT_SERVER_PORT = 8080

_SKIP_CLIENT_COMPRESSION = ['client_compressed_unary',
                            'client_compressed_streaming']

_SKIP_SERVER_COMPRESSION = ['server_compressed_unary',
                            'server_compressed_streaming']

_SKIP_COMPRESSION = _SKIP_CLIENT_COMPRESSION + _SKIP_SERVER_COMPRESSION

_SKIP_ADVANCED = ['status_code_and_message',
                  'custom_metadata',
                  'unimplemented_method',
                  'unimplemented_service']

_TEST_TIMEOUT = 3*60

# Path of docker image in Google Container Registry
_GCR_PATH = 'gcr.io/grpc-testing'

class CXXLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = 'cxx'

  def client_cmd(self, args):
    return ['bins/opt/interop_client'] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return ['bins/opt/interop_server'] + args

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def unimplemented_test_cases_server(self):
    return []

  def __str__(self):
    return 'c++'


class CSharpLanguage:

  def __init__(self):
    self.client_cwd = 'src/csharp/Grpc.IntegrationTesting.Client/bin/Debug'
    self.server_cwd = 'src/csharp/Grpc.IntegrationTesting.Server/bin/Debug'
    self.safename = str(self)

  def client_cmd(self, args):
    return ['mono', 'Grpc.IntegrationTesting.Client.exe'] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return ['mono', 'Grpc.IntegrationTesting.Server.exe'] + args

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_SERVER_COMPRESSION

  def unimplemented_test_cases_server(self):
    return _SKIP_COMPRESSION

  def __str__(self):
    return 'csharp'


class CSharpCoreCLRLanguage:

  def __init__(self):
    self.client_cwd = 'src/csharp/Grpc.IntegrationTesting.Client/bin/Debug/netcoreapp1.0'
    self.server_cwd = 'src/csharp/Grpc.IntegrationTesting.Server/bin/Debug/netcoreapp1.0'
    self.safename = str(self)

  def client_cmd(self, args):
    return ['dotnet', 'exec', 'Grpc.IntegrationTesting.Client.dll'] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return ['dotnet', 'exec', 'Grpc.IntegrationTesting.Server.dll'] + args

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_SERVER_COMPRESSION

  def unimplemented_test_cases_server(self):
    return _SKIP_COMPRESSION

  def __str__(self):
    return 'csharpcoreclr'


class JavaLanguage:

  def __init__(self):
    self.client_cwd = '../grpc-java'
    self.server_cwd = '../grpc-java'
    self.safename = str(self)

  def client_cmd(self, args):
    return ['./run-test-client.sh'] + args

  def client_cmd_http2interop(self, args):
    return ['./interop-testing/build/install/grpc-interop-testing/bin/http2-client'] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return ['./run-test-server.sh'] + args

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_COMPRESSION

  def unimplemented_test_cases_server(self):
    return _SKIP_COMPRESSION

  def __str__(self):
    return 'java'


class GoLanguage:

  def __init__(self):
    # TODO: this relies on running inside docker
    self.client_cwd = '/go/src/google.golang.org/grpc/interop/client'
    self.server_cwd = '/go/src/google.golang.org/grpc/interop/server'
    self.safename = str(self)

  def client_cmd(self, args):
    return ['go', 'run', 'client.go'] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return ['go', 'run', 'server.go'] + args

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_COMPRESSION

  def unimplemented_test_cases_server(self):
    return _SKIP_COMPRESSION

  def __str__(self):
    return 'go'

class Http2Server:
  """Represents the HTTP/2 Interop Test server

  This pretends to be a language in order to be built and run, but really it
  isn't.
  """
  def __init__(self):
    self.server_cwd = None
    self.safename = str(self)

  def server_cmd(self, args):
    return ['python test/http2_test/http2_test_server.py']

  def cloud_to_prod_env(self):
    return {}

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _TEST_CASES

  def unimplemented_test_cases_server(self):
    return _TEST_CASES

  def __str__(self):
    return 'http2'

class Http2Client:
  """Represents the HTTP/2 Interop Test

  This pretends to be a language in order to be built and run, but really it
  isn't.
  """
  def __init__(self):
    self.client_cwd = None
    self.safename = str(self)

  def client_cmd(self, args):
    return ['tools/http2_interop/http2_interop.test', '-test.v'] + args

  def cloud_to_prod_env(self):
    return {}

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _TEST_CASES

  def unimplemented_test_cases_server(self):
    return []

  def __str__(self):
    return 'http2'

class NodeLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = str(self)

  def client_cmd(self, args):
    return ['tools/run_tests/interop/with_nvm.sh',
            'node', 'src/node/interop/interop_client.js'] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return ['tools/run_tests/interop/with_nvm.sh',
            'node', 'src/node/interop/interop_server.js'] + args

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_COMPRESSION

  def unimplemented_test_cases_server(self):
    return _SKIP_COMPRESSION

  def __str__(self):
    return 'node'


class PHPLanguage:

  def __init__(self):
    self.client_cwd = None
    self.safename = str(self)

  def client_cmd(self, args):
    return ['src/php/bin/interop_client.sh'] + args

  def cloud_to_prod_env(self):
    return {}

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_COMPRESSION

  def unimplemented_test_cases_server(self):
    return []

  def __str__(self):
    return 'php'


class PHP7Language:

  def __init__(self):
    self.client_cwd = None
    self.safename = str(self)

  def client_cmd(self, args):
    return ['src/php/bin/interop_client.sh'] + args

  def cloud_to_prod_env(self):
    return {}

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_COMPRESSION

  def unimplemented_test_cases_server(self):
    return []

  def __str__(self):
    return 'php7'


class RubyLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = str(self)

  def client_cmd(self, args):
    return ['tools/run_tests/interop/with_rvm.sh',
            'ruby', 'src/ruby/pb/test/client.rb'] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return ['tools/run_tests/interop/with_rvm.sh',
            'ruby', 'src/ruby/pb/test/server.rb'] + args

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return _SKIP_SERVER_COMPRESSION

  def unimplemented_test_cases_server(self):
    return _SKIP_COMPRESSION

  def __str__(self):
    return 'ruby'


class PythonLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = str(self)

  def client_cmd(self, args):
    return [
        'py27/bin/python',
        'src/python/grpcio_tests/setup.py',
        'run_interop',
        '--client',
        '--args="{}"'.format(' '.join(args))
    ]

  def client_cmd_http2interop(self, args):
    return [ 'py27/bin/python',
              'src/python/grpcio_tests/tests/http2/negative_http2_client.py',
           ] + args

  def cloud_to_prod_env(self):
    return {}

  def server_cmd(self, args):
    return [
        'py27/bin/python',
        'src/python/grpcio_tests/setup.py',
        'run_interop',
        '--server',
        '--args="{}"'.format(' '.join(args))
    ]

  def global_env(self):
    return {'LD_LIBRARY_PATH': '{}/libs/opt'.format(DOCKER_WORKDIR_ROOT),
            'PYTHONPATH': '{}/src/python/gens'.format(DOCKER_WORKDIR_ROOT)}

  def unimplemented_test_cases(self):
    return _SKIP_COMPRESSION

  def unimplemented_test_cases_server(self):
    return _SKIP_COMPRESSION

  def __str__(self):
    return 'python'


_LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'csharpcoreclr' : CSharpCoreCLRLanguage(),
    'go' : GoLanguage(),
    'java' : JavaLanguage(),
    'node' : NodeLanguage(),
    'php' :  PHPLanguage(),
    'php7' :  PHP7Language(),
    'ruby' : RubyLanguage(),
    'python' : PythonLanguage(),
}

# languages supported as cloud_to_cloud servers
_SERVERS = ['c++', 'node', 'csharp', 'csharpcoreclr', 'java', 'go', 'ruby', 'python']

_TEST_CASES = ['large_unary', 'empty_unary', 'ping_pong',
               'empty_stream', 'client_streaming', 'server_streaming',
               'cancel_after_begin', 'cancel_after_first_response',
               'timeout_on_sleeping_server', 'custom_metadata',
               'status_code_and_message', 'unimplemented_method',
               'client_compressed_unary', 'server_compressed_unary',
               'client_compressed_streaming', 'server_compressed_streaming',
               'unimplemented_service']

_AUTH_TEST_CASES = ['compute_engine_creds', 'jwt_token_creds',
                    'oauth2_auth_token', 'per_rpc_creds']

_HTTP2_TEST_CASES = ['tls', 'framing']

_HTTP2_BADSERVER_TEST_CASES = ['rst_after_header', 'rst_after_data', 'rst_during_data',
                     'goaway', 'ping', 'max_streams']

DOCKER_WORKDIR_ROOT = '/var/local/git/grpc'
DOCKER_TEST_INFO = os.path.join(DOCKER_WORKDIR_ROOT, 'test_info')

def docker_run_cmdline(cmdline, image, docker_args=[], cwd=None, environ=None):
  """Wraps given cmdline array to create 'docker run' cmdline from it."""
  docker_cmdline = ['docker', 'run', '-i', '--rm=true']

  # turn environ into -e docker args
  if environ:
    for k,v in environ.items():
      docker_cmdline += ['-e', '%s=%s' % (k,v)]

  # set working directory
  workdir = DOCKER_WORKDIR_ROOT
  if cwd:
    workdir = os.path.join(workdir, cwd)
  docker_cmdline += ['-w', workdir]

  docker_cmdline += docker_args + [image] + cmdline
  return docker_cmdline


def bash_cmdline(cmdline):
  """Creates bash -c cmdline from args list."""
  # Use login shell:
  # * makes error messages clearer if executables are missing
  return ['bash', '-c', ' '.join(cmdline)]


def auth_options(language, test_case):
  """Returns (cmdline, env) tuple with cloud_to_prod_auth test options."""

  language = str(language)
  cmdargs = []
  env = {}

  # TODO(jtattermusch): this file path only works inside docker
  key_filepath = '/root/service_account/stubbyCloudTestingTest-ee3fce360ac5.json'
  oauth_scope_arg = '--oauth_scope=https://www.googleapis.com/auth/xapi.zoo'
  key_file_arg = '--service_account_key_file=%s' % key_filepath
  default_account_arg = '--default_service_account=830293263384-compute@developer.gserviceaccount.com'

  if test_case in ['jwt_token_creds', 'per_rpc_creds', 'oauth2_auth_token']:
    if language in ['csharp', 'csharpcoreclr', 'node', 'php', 'php7', 'python', 'ruby']:
      env['GOOGLE_APPLICATION_CREDENTIALS'] = key_filepath
    else:
      cmdargs += [key_file_arg]

  if test_case in ['per_rpc_creds', 'oauth2_auth_token']:
    cmdargs += [oauth_scope_arg]

  if test_case == 'oauth2_auth_token' and language == 'c++':
    # C++ oauth2 test uses GCE creds and thus needs to know the default account
    cmdargs += [default_account_arg]

  if test_case == 'compute_engine_creds':
    cmdargs += [oauth_scope_arg, default_account_arg]

  return (cmdargs, env)


def _job_kill_handler(job):
  if job._spec.container_name:
    dockerjob.docker_kill(job._spec.container_name)
    # When the job times out and we decide to kill it,
    # we need to wait a before restarting the job
    # to prevent "container name already in use" error.
    # TODO(jtattermusch): figure out a cleaner way to to this.
    time.sleep(2)


def cloud_to_prod_jobspec(language, test_case, server_host_name,
                          server_host_detail, docker_image=None, auth=False):
  """Creates jobspec for cloud-to-prod interop test"""
  container_name = None
  cmdargs = [
      '--server_host=%s' % server_host_detail[0],
      '--server_host_override=%s' % server_host_detail[1],
      '--server_port=443',
      '--use_tls=true',
      '--test_case=%s' % test_case]
  environ = dict(language.cloud_to_prod_env(), **language.global_env())
  if auth:
    auth_cmdargs, auth_env = auth_options(language, test_case)
    cmdargs += auth_cmdargs
    environ.update(auth_env)
  cmdline = bash_cmdline(language.client_cmd(cmdargs))
  cwd = language.client_cwd

  if docker_image:
    container_name = dockerjob.random_name('interop_client_%s' %
                                           language.safename)
    cmdline = docker_run_cmdline(cmdline,
                                 image=docker_image,
                                 cwd=cwd,
                                 environ=environ,
                                 docker_args=['--net=host',
                                              '--name', container_name])
    cwd = None
    environ = None

  suite_name='cloud_to_prod_auth' if auth else 'cloud_to_prod'
  test_job = jobset.JobSpec(
          cmdline=cmdline,
          cwd=cwd,
          environ=environ,
          shortname='%s:%s:%s:%s' % (suite_name, server_host_name, language,
                                     test_case),
          timeout_seconds=_TEST_TIMEOUT,
          flake_retries=5 if args.allow_flakes else 0,
          timeout_retries=2 if args.allow_flakes else 0,
          kill_handler=_job_kill_handler)
  if docker_image:
    test_job.container_name = container_name
  return test_job


def cloud_to_cloud_jobspec(language, test_case, server_name, server_host,
                           server_port, docker_image=None, insecure=False):
  """Creates jobspec for cloud-to-cloud interop test"""
  interop_only_options = [
      '--server_host_override=foo.test.google.fr',
      '--use_tls=%s' % ('false' if insecure else 'true'),
      '--use_test_ca=true',
  ]
  common_options = [
      '--test_case=%s' % test_case,
      '--server_host=%s' % server_host,
  ]
  if test_case in _HTTP2_BADSERVER_TEST_CASES:
    # We are running the http2_badserver_interop test. Adjust command line accordingly.
    offset = sorted(_HTTP2_BADSERVER_TEST_CASES).index(test_case)
    client_options = common_options + ['--server_port=%s' %
                                       (int(server_port)+offset)]
    cmdline = bash_cmdline(language.client_cmd_http2interop(client_options))
  else:
    client_options = interop_only_options + common_options + ['--server_port=%s' % server_port]
    cmdline = bash_cmdline(language.client_cmd(client_options))

  cwd = language.client_cwd
  environ = language.global_env()
  if docker_image:
    container_name = dockerjob.random_name('interop_client_%s' % language.safename)
    cmdline = docker_run_cmdline(cmdline,
                                 image=docker_image,
                                 environ=environ,
                                 cwd=cwd,
                                 docker_args=['--net=host',
                                              '--name', container_name])
    cwd = None

  test_job = jobset.JobSpec(
          cmdline=cmdline,
          cwd=cwd,
          environ=environ,
          shortname='cloud_to_cloud:%s:%s_server:%s' % (language, server_name,
                                                        test_case),
          timeout_seconds=_TEST_TIMEOUT,
          flake_retries=5 if args.allow_flakes else 0,
          timeout_retries=2 if args.allow_flakes else 0,
          kill_handler=_job_kill_handler)
  if docker_image:
    test_job.container_name = container_name
  return test_job


def server_jobspec(language, docker_image, insecure=False):
  """Create jobspec for running a server"""
  container_name = dockerjob.random_name('interop_server_%s' % language.safename)
  cmdline = bash_cmdline(
      language.server_cmd(['--port=%s' % _DEFAULT_SERVER_PORT,
                           '--use_tls=%s' % ('false' if insecure else 'true')]))
  environ = language.global_env()
  if language.safename == 'http2':
    # we are running the http2 interop server. Open next N ports beginning
    # with the server port. These ports are used for http2 interop test
    # (one test case per port). We also attach the docker container running
    # the server to local network, so we don't have to mess with port mapping
    port_args = [
      '-p', str(_DEFAULT_SERVER_PORT+0),
      '-p', str(_DEFAULT_SERVER_PORT+1),
      '-p', str(_DEFAULT_SERVER_PORT+2),
      '-p', str(_DEFAULT_SERVER_PORT+3),
      '-p', str(_DEFAULT_SERVER_PORT+4),
      '-p', str(_DEFAULT_SERVER_PORT+5),
      '-p', str(_DEFAULT_SERVER_PORT+6),
      '--net=host',
    ]
  else:
    port_args = ['-p', str(_DEFAULT_SERVER_PORT)]

  docker_cmdline = docker_run_cmdline(cmdline,
                                      image=docker_image,
                                      cwd=language.server_cwd,
                                      environ=environ,
                                      docker_args=port_args +
                                        ['--name', container_name])
  server_job = jobset.JobSpec(
          cmdline=docker_cmdline,
          environ=environ,
          shortname='interop_server_%s' % language,
          timeout_seconds=30*60)
  server_job.container_name = container_name
  return server_job


def build_interop_image_jobspec(language, tag=None):
  """Creates jobspec for building interop docker image for a language"""
  if not tag:
    tag = 'grpc_interop_%s:%s' % (language.safename, uuid.uuid4())
  env = {'INTEROP_IMAGE': tag,
         'BASE_NAME': 'grpc_interop_%s' % language.safename}
  if not args.travis:
    env['TTY_FLAG'] = '-t'
  # This env variable is used to get around the github rate limit
  # error when running the PHP `composer install` command
  host_file = '%s/.composer/auth.json' % os.environ['HOME']
  if language.safename == 'php' and os.path.exists(host_file):
    env['BUILD_INTEROP_DOCKER_EXTRA_ARGS'] = \
      '-v %s:/root/.composer/auth.json:ro' % host_file
  build_job = jobset.JobSpec(
          cmdline=['tools/run_tests/dockerize/build_interop_image.sh'],
          environ=env,
          shortname='build_docker_%s' % (language),
          timeout_seconds=30*60)
  build_job.tag = tag
  return build_job


def aggregate_http2_results(stdout):
  match = re.search(r'\{"cases[^\]]*\]\}', stdout)
  if not match:
    return None

  results = json.loads(match.group(0))
  skipped = 0
  passed = 0
  failed = 0
  failed_cases = []
  for case in results['cases']:
    if case.get('skipped', False):
      skipped += 1
    else:
      if case.get('passed', False):
        passed += 1
      else:
        failed += 1
        failed_cases.append(case.get('name', "NONAME"))
  return {
    'passed': passed,
    'failed': failed,
    'skipped': skipped,
    'failed_cases': ', '.join(failed_cases),
    'percent': 1.0 * passed / (passed + failed)
  }

def push_to_gke_registry(image, gcr_path, gcr_tag='latest', with_files=[]):
  """Tags and Pushes a docker image in Google Containger Registry."""
  tag_idx = image.find(':')
  if tag_idx == -1:
    print('Failed to parse docker image name %s' % image)
    return False

  tmp_dir = os.path.join('/tmp', os.path.basename(DOCKER_TEST_INFO))
  shutil.rmtree(tmp_dir, ignore_errors=True)
  os.makedirs(tmp_dir)
  # Stores the original image:tag which might be referenced by tests
  # during replay.
  with open(os.path.join(tmp_dir, 'original_image'), 'w') as f:
    f.write(image + '\n')
  # Copy all other files inside the docker image.
  for f in with_files:
    shutil.copy(f, tmp_dir)

  tag_name = '%s/%s:%s' % (gcr_path, image[:tag_idx], gcr_tag)
  tmp_container_name = str(uuid.uuid4())
  cmd = (
      'docker run -v %(src)s:%(mnt)s:ro --name=%(vm)s %(img)s cp -r %(mnt)s %(dst)s'
  ) % {
      'src': tmp_dir,
      'mnt': os.path.join('/mnt/', os.path.basename(tmp_dir)),
      'vm': tmp_container_name,
      'img':image,
      'tag': tag_name,
      'dst': os.path.dirname(DOCKER_TEST_INFO),
  }
  # Store all files with the image and commit it.
  if (subprocess.call(cmd.split()) or
      subprocess.call(['docker', 'commit', '-m',
                       'added ' + DOCKER_TEST_INFO, tmp_container_name, tag_name])):
    print('Error in copying files into the image')
    succeed = False
  else:
    print('Pushing %s to the GKE registry..' % tag_name)
    if subprocess.call(['gcloud', 'docker', 'push', tag_name]):
      print('Error in pushing the image %s to the GKE registry' % tag_name)
      succeed = False
    succeed = True

  # Clean up
  subprocess.call(['docker', 'rm', tmp_container_name])
  dockerjob.remove_image(tag_name)
  return succeed

# A dictionary of prod servers to test.
# Format: server_name: (server_host, server_host_override, errors_allowed)
# TODO(adelez): implement logic for errors_allowed where if the indicated tests
# fail, they don't impact the overall test result.
prod_servers = {
    'default': ('216.239.32.254',
                'grpc-test.sandbox.googleapis.com', False),
    'gateway_v2': ('216.239.32.254',
                   'grpc-test2.sandbox.googleapis.com', True),
    'cloud_gateway': ('216.239.32.255', 'grpc-test.sandbox.googleapis.com',
                      False),
    'cloud_gateway_v2': ('216.239.32.255', 'grpc-test2.sandbox.googleapis.com',
                         True),
    'gateway_v4': ('216.239.32.254',
                   'grpc-test4.sandbox.googleapis.com', True),
    'cloud_gateway_v4': ('216.239.32.255', 'grpc-test4.sandbox.googleapis.com',
                         True),
}

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'],
                  help='Clients to run.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('--cloud_to_prod',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Run cloud_to_prod tests.')
argp.add_argument('--cloud_to_prod_auth',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Run cloud_to_prod_auth tests.')
argp.add_argument('--gcr_tag',
                  help='When specified, docker images will be stored in Google ' +
                       'Container Registery under ' + _GCR_PATH)
argp.add_argument('--prod_servers',
                  choices=prod_servers.keys(),
                  default=['default'],
                  nargs='+',
                  help=('The servers to run cloud_to_prod and '
                        'cloud_to_prod_auth tests against.'))
argp.add_argument('-s', '--server',
                  choices=['all'] + sorted(_SERVERS),
                  nargs='+',
                  help='Run cloud_to_cloud servers in a separate docker ' +
                       'image. Servers can only be started automatically if ' +
                       '--use_docker option is enabled.',
                  default=[])
argp.add_argument('--override_server',
                  action='append',
                  type=lambda kv: kv.split('='),
                  help='Use servername=HOST:PORT to explicitly specify a server. E.g. csharp=localhost:50000',
                  default=[])
argp.add_argument('-t', '--travis',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument('--use_docker',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Run all the interop tests under docker. That provides ' +
                  'additional isolation and prevents the need to install ' +
                  'language specific prerequisites. Only available on Linux.')
argp.add_argument('--allow_flakes',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Allow flaky tests to show as passing (re-runs failed tests up to five times)')
argp.add_argument('--http2_interop',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Enable HTTP/2 client edge case testing. (Bad client, good server)')
argp.add_argument('--http2_badserver_interop',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Enable HTTP/2 server edge case testing. (Good client, bad server)')
argp.add_argument('--insecure',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Whether to use secure channel.')

args = argp.parse_args()

servers = set(s for s in itertools.chain.from_iterable(_SERVERS
                                                       if x == 'all' else [x]
                                                       for x in args.server))

if args.use_docker:
  if not args.travis:
    print('Seen --use_docker flag, will run interop tests under docker.')
    print('')
    print('IMPORTANT: The changes you are testing need to be locally committed')
    print('because only the committed changes in the current branch will be')
    print('copied to the docker environment.')
    time.sleep(5)

if not args.use_docker and servers:
  print('Running interop servers is only supported with --use_docker option enabled.')
  sys.exit(1)

languages = set(_LANGUAGES[l]
                for l in itertools.chain.from_iterable(
                      _LANGUAGES.iterkeys() if x == 'all' else [x]
                      for x in args.language))

http2Interop = Http2Client() if args.http2_interop else None
http2InteropServer = Http2Server() if args.http2_badserver_interop else None

docker_images={}
if args.use_docker:
  # languages for which to build docker images
  languages_to_build = set(_LANGUAGES[k] for k in set([str(l) for l in languages] +
                                                    [s for s in servers]))
  if args.http2_interop:
    languages_to_build.add(http2Interop)

  if args.http2_badserver_interop:
    languages_to_build.add(http2InteropServer)

  build_jobs = []
  for l in languages_to_build:
    job = build_interop_image_jobspec(l)
    docker_images[str(l)] = job.tag
    build_jobs.append(job)

  if build_jobs:
    jobset.message('START', 'Building interop docker images.', do_newline=True)
    num_failures, _ = jobset.run(
        build_jobs, newline_on_success=True, maxjobs=args.jobs)
    if num_failures == 0:
      jobset.message('SUCCESS', 'All docker images built successfully.',
                     do_newline=True)
    else:
      jobset.message('FAILED', 'Failed to build interop docker images.',
                     do_newline=True)
      for image in docker_images.itervalues():
        dockerjob.remove_image(image, skip_nonexistent=True)
      sys.exit(1)

# Start interop servers.
server_jobs={}
server_addresses={}
try:
  for s in servers:
    lang = str(s)
    spec = server_jobspec(_LANGUAGES[lang], docker_images.get(lang),
                          args.insecure)
    job = dockerjob.DockerJob(spec)
    server_jobs[lang] = job
    server_addresses[lang] = ('localhost', job.mapped_port(_DEFAULT_SERVER_PORT))

  if args.http2_badserver_interop:
    # launch a HTTP2 server emulator that creates edge cases
    lang = str(http2InteropServer)
    spec = server_jobspec(http2InteropServer, docker_images.get(lang))
    job = dockerjob.DockerJob(spec)
    server_jobs[lang] = job
    server_addresses[lang] = ('localhost', _DEFAULT_SERVER_PORT)

  jobs = []
  if args.cloud_to_prod:
    if args.insecure:
      print('TLS is always enabled for cloud_to_prod scenarios.')
    for server_host_name in args.prod_servers:
      for language in languages:
        for test_case in _TEST_CASES:
          if not test_case in language.unimplemented_test_cases():
            if not test_case in _SKIP_ADVANCED + _SKIP_COMPRESSION:
              test_job = cloud_to_prod_jobspec(
                  language, test_case, server_host_name,
                  prod_servers[server_host_name],
                  docker_image=docker_images.get(str(language)))
              jobs.append(test_job)

      if args.http2_interop:
        for test_case in _HTTP2_TEST_CASES:
          test_job = cloud_to_prod_jobspec(
              http2Interop, test_case, server_host_name,
              prod_servers[server_host_name],
              docker_image=docker_images.get(str(http2Interop)))
          jobs.append(test_job)

  if args.cloud_to_prod_auth:
    if args.insecure:
      print('TLS is always enabled for cloud_to_prod scenarios.')
    for server_host_name in args.prod_servers:
      for language in languages:
        for test_case in _AUTH_TEST_CASES:
          if not test_case in language.unimplemented_test_cases():
            test_job = cloud_to_prod_jobspec(
                language, test_case, server_host_name,
                prod_servers[server_host_name],
                docker_image=docker_images.get(str(language)), auth=True)
            jobs.append(test_job)

  for server in args.override_server:
    server_name = server[0]
    (server_host, server_port) = server[1].split(':')
    server_addresses[server_name] = (server_host, server_port)

  for server_name, server_address in server_addresses.items():
    (server_host, server_port) = server_address
    server_language = _LANGUAGES.get(server_name, None)
    skip_server = []  # test cases unimplemented by server
    if server_language:
      skip_server = server_language.unimplemented_test_cases_server()
    if not args.http2_badserver_interop:
      for language in languages:
        for test_case in _TEST_CASES:
          if not test_case in language.unimplemented_test_cases():
            if not test_case in skip_server:
              test_job = cloud_to_cloud_jobspec(language,
                                                test_case,
                                                server_name,
                                                server_host,
                                                server_port,
                                                docker_image=docker_images.get(str(language)),
                                                insecure=args.insecure)
              jobs.append(test_job)

    if args.http2_interop:
      for test_case in _HTTP2_TEST_CASES:
        if server_name == "go":
          # TODO(carl-mastrangelo): Reenable after https://github.com/grpc/grpc-go/issues/434
          continue
        test_job = cloud_to_cloud_jobspec(http2Interop,
                                          test_case,
                                          server_name,
                                          server_host,
                                          server_port,
                                          docker_image=docker_images.get(str(http2Interop)),
                                          insecure=args.insecure)
        jobs.append(test_job)

    if args.http2_badserver_interop:
      for language in languages:
        for test_case in _HTTP2_BADSERVER_TEST_CASES:
          test_job = cloud_to_cloud_jobspec(language,
                                            test_case,
                                            server_name,
                                            server_host,
                                            server_port,
                                            docker_image=docker_images.get(str(language)))
          jobs.append(test_job)

  if not jobs:
    print('No jobs to run.')
    for image in docker_images.itervalues():
      dockerjob.remove_image(image, skip_nonexistent=True)
    sys.exit(1)

  num_failures, resultset = jobset.run(jobs, newline_on_success=True,
                                       maxjobs=args.jobs,
                                       joblog="testcase_cmds.txt")
  if num_failures:
    jobset.message('FAILED', 'Some tests failed', do_newline=True)
  else:
    jobset.message('SUCCESS', 'All tests passed', do_newline=True)

  report_utils.render_junit_xml_report(resultset, 'report.xml')

  for name, job in resultset.items():
    if "http2" in name:
      job[0].http2results = aggregate_http2_results(job[0].message)

  report_utils.render_interop_html_report(
      set([str(l) for l in languages]), servers, _TEST_CASES, _AUTH_TEST_CASES,
      _HTTP2_TEST_CASES, resultset, num_failures,
      args.cloud_to_prod_auth or args.cloud_to_prod, args.prod_servers,
      args.http2_interop)

finally:
  # Check if servers are still running.
  for server, job in server_jobs.items():
    if not job.is_running():
      print('Server "%s" has exited prematurely.' % server)

  dockerjob.finish_jobs([j for j in server_jobs.itervalues()])

  for image in docker_images.itervalues():
    if args.gcr_tag:
      push_to_gke_registry(
          image, _GCR_PATH, args.gcr_tag,
          with_files=['report.xml', 'testcase_cmds.txt'])
    print('Removing docker image %s' % image)
    dockerjob.remove_image(image)
