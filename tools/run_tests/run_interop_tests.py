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

"""Run interop (cross-language) tests in parallel."""

import argparse
import dockerjob
import itertools
import xml.etree.cElementTree as ET
import jobset
import multiprocessing
import os
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)

_DEFAULT_SERVER_PORT=8080

_CLOUD_TO_PROD_BASE_ARGS = [
    '--server_host_override=grpc-test.sandbox.google.com',
    '--server_host=grpc-test.sandbox.google.com',
    '--server_port=443']

_CLOUD_TO_CLOUD_BASE_ARGS = [
    '--server_host_override=foo.test.google.fr']

# TOOD(jtattermusch) wrapped languages use this variable for location 
# of roots.pem. We might want to use GRPC_DEFAULT_SSL_ROOTS_FILE_PATH
# supported by C core SslCredentials instead.
_SSL_CERT_ENV = { 'SSL_CERT_FILE':'/usr/local/share/grpc/roots.pem' }

# TODO(jtattermusch) unify usage of --use_tls and --use_tls=true
# TODO(jtattermusch) unify usage of --use_prod_roots and --use_test_ca
# TODO(jtattermusch) go uses --tls_ca_file instead of --use_test_ca


class CXXLanguage:

  def __init__(self):
    self.client_cmdline_base = ['bins/opt/interop_client']
    self.client_cwd = None
    self.server_cwd = None

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls=true','--use_prod_roots'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls=true'])

  def cloud_to_prod_env(self):
    return {}

  def server_args(self):
    return ['bins/opt/interop_server', '--use_tls=true']

  def __str__(self):
    return 'c++'


class CSharpLanguage:

  def __init__(self):
    self.client_cmdline_base = ['mono', 'Grpc.IntegrationTesting.Client.exe']
    self.client_cwd = 'src/csharp/Grpc.IntegrationTesting.Client/bin/Debug'
    self.server_cwd = 'src/csharp/Grpc.IntegrationTesting.Server/bin/Debug'

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls=true'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls=true', '--use_test_ca=true'])

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def server_args(self):
    return ['mono', 'Grpc.IntegrationTesting.Server.exe', '--use_tls=true']

  def __str__(self):
    return 'csharp'


class JavaLanguage:

  def __init__(self):
    self.client_cmdline_base = ['./run-test-client.sh']
    self.client_cwd = '../grpc-java'
    self.server_cwd = '../grpc-java'

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls=true'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls=true', '--use_test_ca=true'])

  def cloud_to_prod_env(self):
    return {}

  def server_args(self):
    return ['./run-test-server.sh', '--use_tls=true']

  def __str__(self):
    return 'java'


class GoLanguage:

  def __init__(self):
    self.client_cmdline_base = ['go', 'run', 'client.go']
    # TODO: this relies on running inside docker
    self.client_cwd = '/go/src/google.golang.org/grpc/interop/client'
    self.server_cwd = '/go/src/google.golang.org/grpc/interop/server'

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls=true', '--tls_ca_file=""'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls=true'])

  def cloud_to_prod_env(self):
    return {}

  def server_args(self):
    return ['go', 'run', 'server.go', '--use_tls=true']

  def __str__(self):
    return 'go'


class NodeLanguage:

  def __init__(self):
    self.client_cmdline_base = ['node', 'src/node/interop/interop_client.js']
    self.client_cwd = None
    self.server_cwd = None

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls=true'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls=true', '--use_test_ca=true'])

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def server_args(self):
    return ['node', 'src/node/interop/interop_server.js', '--use_tls=true']

  def __str__(self):
    return 'node'


class PHPLanguage:

  def __init__(self):
    self.client_cmdline_base = ['src/php/bin/interop_client.sh']
    self.client_cwd = None

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls', '--use_test_ca'])

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def __str__(self):
    return 'php'


class RubyLanguage:

  def __init__(self):
    self.client_cmdline_base = ['ruby', 'src/ruby/bin/interop/interop_client.rb']
    self.client_cwd = None
    self.server_cwd = None

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls', '--use_test_ca'])

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def server_args(self):
    return ['ruby', 'src/ruby/bin/interop/interop_server.rb', '--use_tls']

  def __str__(self):
    return 'ruby'


# TODO(jtattermusch): python once we get it working
_LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'go' : GoLanguage(),
    'java' : JavaLanguage(),
    'node' : NodeLanguage(),
    'php' :  PHPLanguage(),
    'ruby' : RubyLanguage(),
}

# languages supported as cloud_to_cloud servers
_SERVERS = ['c++', 'node', 'csharp', 'java', 'go', 'ruby']

# TODO(jtattermusch): add empty_stream once PHP starts supporting it.
# TODO(jtattermusch): add timeout_on_sleeping_server once java starts supporting it.
# TODO(jtattermusch): add support for auth tests.
_TEST_CASES = ['large_unary', 'empty_unary', 'ping_pong',
               'client_streaming', 'server_streaming',
               'cancel_after_begin', 'cancel_after_first_response']

_AUTH_TEST_CASES = ['compute_engine_creds', 'jwt_token_creds',
                    'oauth2_auth_token', 'per_rpc_creds']


def docker_run_cmdline(cmdline, image, docker_args=[], cwd=None, environ=None):
  """Wraps given cmdline array to create 'docker run' cmdline from it."""
  docker_cmdline = ['docker', 'run', '-i', '--rm=true']

  # turn environ into -e docker args
  if environ:
    for k,v in environ.iteritems():
      docker_cmdline += ['-e', '%s=%s' % (k,v)]

  # set working directory
  workdir = '/var/local/git/grpc'
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


def add_auth_options(language, test_case, cmdline, env):
  """Returns (cmdline, env) tuple with cloud_to_prod_auth test options."""

  language = str(language)
  cmdline = list(cmdline)
  env = env.copy()

  # TODO(jtattermusch): this file path only works inside docker
  key_filepath = '/root/service_account/stubbyCloudTestingTest-ee3fce360ac5.json'
  oauth_scope_arg = '--oauth_scope=https://www.googleapis.com/auth/xapi.zoo'
  key_file_arg = '--service_account_key_file=%s' % key_filepath
  default_account_arg = '--default_service_account=830293263384-compute@developer.gserviceaccount.com'

  if test_case in ['jwt_token_creds', 'per_rpc_creds', 'oauth2_auth_token']:
    if language in ['csharp', 'node', 'php', 'ruby']:
      env['GOOGLE_APPLICATION_CREDENTIALS'] = key_filepath
    else:
      cmdline += [key_file_arg]

  if test_case in ['per_rpc_creds', 'oauth2_auth_token']:
    cmdline += [oauth_scope_arg]

  if test_case == 'oauth2_auth_token' and language == 'c++':
    # C++ oauth2 test uses GCE creds and thus needs to know the default account
    cmdline += [default_account_arg]

  if test_case == 'compute_engine_creds':
    cmdline += [oauth_scope_arg, default_account_arg]

  return (cmdline, env)


def cloud_to_prod_jobspec(language, test_case, docker_image=None, auth=False):
  """Creates jobspec for cloud-to-prod interop test"""
  cmdline = language.cloud_to_prod_args() + ['--test_case=%s' % test_case]
  cwd = language.client_cwd
  environ = language.cloud_to_prod_env()
  if auth:
    cmdline, environ = add_auth_options(language, test_case, cmdline, environ)
  cmdline = bash_login_cmdline(cmdline)

  if docker_image:
    cmdline = docker_run_cmdline(cmdline, image=docker_image, cwd=cwd, environ=environ)
    cwd = None
    environ = None

  suite_name='cloud_to_prod_auth' if auth else 'cloud_to_prod'
  test_job = jobset.JobSpec(
          cmdline=cmdline,
          cwd=cwd,
          environ=environ,
          shortname="%s:%s:%s" % (suite_name, language, test_case),
          timeout_seconds=2*60,
          flake_retries=5 if args.allow_flakes else 0,
          timeout_retries=2 if args.allow_flakes else 0)
  return test_job


def cloud_to_cloud_jobspec(language, test_case, server_name, server_host,
                           server_port, docker_image=None):
  """Creates jobspec for cloud-to-cloud interop test"""
  cmdline = bash_login_cmdline(language.cloud_to_cloud_args() +
                               ['--test_case=%s' % test_case,
                                '--server_host=%s' % server_host,
                                '--server_port=%s' % server_port ])
  cwd = language.client_cwd
  if docker_image:
    cmdline = docker_run_cmdline(cmdline,
                                 image=docker_image,
                                 cwd=cwd,
                                 docker_args=['--net=host'])
    cwd = None
  test_job = jobset.JobSpec(
          cmdline=cmdline,
          cwd=cwd,
          shortname="cloud_to_cloud:%s:%s_server:%s" % (language, server_name,
                                                 test_case),
          timeout_seconds=2*60,
          flake_retries=5 if args.allow_flakes else 0,
          timeout_retries=2 if args.allow_flakes else 0)
  return test_job


def server_jobspec(language, docker_image):
  """Create jobspec for running a server"""
  cidfile = tempfile.mktemp()
  cmdline = bash_login_cmdline(language.server_args() +
                               ['--port=%s' % _DEFAULT_SERVER_PORT])
  docker_cmdline = docker_run_cmdline(cmdline,
                                      image=docker_image,
                                      cwd=language.server_cwd,
                                      docker_args=['-p', str(_DEFAULT_SERVER_PORT),
                                                   '--cidfile', cidfile])
  server_job = jobset.JobSpec(
          cmdline=docker_cmdline,
          shortname="interop_server:%s" % language,
          timeout_seconds=30*60)
  server_job.cidfile = cidfile
  return server_job


def build_interop_image_jobspec(language, tag=None):
  """Creates jobspec for building interop docker image for a language"""
  safelang = str(language).replace("+", "x")
  if not tag:
    tag = 'grpc_interop_%s:%s' % (safelang, uuid.uuid4())
  env = {'INTEROP_IMAGE': tag, 'BASE_NAME': 'grpc_interop_%s' % safelang}
  if not args.travis:
    env['TTY_FLAG'] = '-t'
  build_job = jobset.JobSpec(
          cmdline=['tools/jenkins/build_interop_image.sh'],
          environ=env,
          shortname="build_docker_%s" % (language),
          timeout_seconds=30*60)
  build_job.tag = tag
  return build_job


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
argp.add_argument('-s', '--server',
                  choices=['all'] + sorted(_SERVERS),
                  action='append',
                  help='Run cloud_to_cloud servers in a separate docker ' +
                       'image. Servers can only be started automatically if ' +
                       '--use_docker option is enabled.',
                  default=[])
argp.add_argument('--override_server',
                  action='append',
                  type=lambda kv: kv.split("="),
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
                  help="Allow flaky tests to show as passing (re-runs failed tests up to five times)")
args = argp.parse_args()

servers = set(s for s in itertools.chain.from_iterable(_SERVERS
                                                       if x == 'all' else [x]
                                                       for x in args.server))

if args.use_docker:
  if not args.travis:
    print 'Seen --use_docker flag, will run interop tests under docker.'
    print
    print 'IMPORTANT: The changes you are testing need to be locally committed'
    print 'because only the committed changes in the current branch will be'
    print 'copied to the docker environment.'
    time.sleep(5)

if not args.use_docker and servers:
  print "Running interop servers is only supported with --use_docker option enabled."
  sys.exit(1)

languages = set(_LANGUAGES[l]
                for l in itertools.chain.from_iterable(
                      _LANGUAGES.iterkeys() if x == 'all' else [x]
                      for x in args.language))

docker_images={}
if args.use_docker:
  # languages for which to build docker images
  languages_to_build = set(_LANGUAGES[k] for k in set([str(l) for l in languages] +
                                                    [s for s in servers]))

  build_jobs = []
  for l in languages_to_build:
    job = build_interop_image_jobspec(l)
    docker_images[str(l)] = job.tag
    build_jobs.append(job)

  if build_jobs:
    jobset.message('START', 'Building interop docker images.', do_newline=True)
    if jobset.run(build_jobs, newline_on_success=True, maxjobs=args.jobs):
      jobset.message('SUCCESS', 'All docker images built successfully.', do_newline=True)
    else:
      jobset.message('FAILED', 'Failed to build interop docker images.', do_newline=True)
      for image in docker_images.itervalues():
        dockerjob.remove_image(image, skip_nonexistent=True)
      exit(1);

# Start interop servers.
server_jobs={}
server_addresses={}
try:
  for s in servers:
    lang = str(s)
    spec = server_jobspec(_LANGUAGES[lang], docker_images.get(lang))
    job = dockerjob.DockerJob(spec)
    server_jobs[lang] = job
    server_addresses[lang] = ('localhost', job.mapped_port(_DEFAULT_SERVER_PORT))


  jobs = []
  if args.cloud_to_prod:
    for language in languages:
      for test_case in _TEST_CASES:
        test_job = cloud_to_prod_jobspec(language, test_case,
                                         docker_image=docker_images.get(str(language)))
        jobs.append(test_job)

  if args.cloud_to_prod_auth:
    for language in languages:
      for test_case in _AUTH_TEST_CASES:
        test_job = cloud_to_prod_jobspec(language, test_case,
                                         docker_image=docker_images.get(str(language)),
                                         auth=True)
        jobs.append(test_job)

  for server in args.override_server:
    server_name = server[0]
    (server_host, server_port) = server[1].split(':')
    server_addresses[server_name] = (server_host, server_port)

  for server_name, server_address in server_addresses.iteritems():
    (server_host, server_port) = server_address
    for language in languages:
      for test_case in _TEST_CASES:
        test_job = cloud_to_cloud_jobspec(language,
                                          test_case,
                                          server_name,
                                          server_host,
                                          server_port,
                                          docker_image=docker_images.get(str(language)))
        jobs.append(test_job)

  if not jobs:
    print "No jobs to run."
    for image in docker_images.itervalues():
      dockerjob.remove_image(image, skip_nonexistent=True)
    sys.exit(1)

  root = ET.Element('testsuites')
  testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

  if jobset.run(jobs, newline_on_success=True, maxjobs=args.jobs, xml_report=testsuite):
    jobset.message('SUCCESS', 'All tests passed', do_newline=True)
  else:
    jobset.message('FAILED', 'Some tests failed', do_newline=True)

  tree = ET.ElementTree(root)
  tree.write('report.xml', encoding='UTF-8')

finally:
  # Check if servers are still running.
  for server, job in server_jobs.iteritems():
    if not job.is_running():
      print 'Server "%s" has exited prematurely.' % server

  dockerjob.finish_jobs([j for j in server_jobs.itervalues()])

  for image in docker_images.itervalues():
    print 'Removing docker image %s' % image
    dockerjob.remove_image(image)
