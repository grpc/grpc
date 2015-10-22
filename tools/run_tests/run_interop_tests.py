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

# TOOD(jtattermusch) wrapped languages use this variable for location
# of roots.pem. We might want to use GRPC_DEFAULT_SSL_ROOTS_FILE_PATH
# supported by C core SslCredentials instead.
_SSL_CERT_ENV = { 'SSL_CERT_FILE':'/usr/local/share/grpc/roots.pem' }


class CXXLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = 'cxx'

  def client_args(self):
    return ['bins/opt/interop_client']

  def cloud_to_prod_env(self):
    return {}

  def server_args(self):
    return ['bins/opt/interop_server', '--use_tls=true']

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def __str__(self):
    return 'c++'


class CSharpLanguage:

  def __init__(self):
    self.client_cwd = 'src/csharp/Grpc.IntegrationTesting.Client/bin/Debug'
    self.server_cwd = 'src/csharp/Grpc.IntegrationTesting.Server/bin/Debug'
    self.safename = str(self)

  def client_args(self):
    return ['mono', 'Grpc.IntegrationTesting.Client.exe']

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def server_args(self):
    return ['mono', 'Grpc.IntegrationTesting.Server.exe', '--use_tls=true']

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def __str__(self):
    return 'csharp'


class JavaLanguage:

  def __init__(self):
    self.client_cwd = '../grpc-java'
    self.server_cwd = '../grpc-java'
    self.safename = str(self)

  def client_args(self):
    return ['./run-test-client.sh']

  def cloud_to_prod_env(self):
    return {}

  def server_args(self):
    return ['./run-test-server.sh', '--use_tls=true']

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def __str__(self):
    return 'java'


class GoLanguage:

  def __init__(self):
    # TODO: this relies on running inside docker
    self.client_cwd = '/go/src/google.golang.org/grpc/interop/client'
    self.server_cwd = '/go/src/google.golang.org/grpc/interop/server'
    self.safename = str(self)

  def client_args(self):
    return ['go', 'run', 'client.go']

  def cloud_to_prod_env(self):
    return {}

  def server_args(self):
    return ['go', 'run', 'server.go', '--use_tls=true']

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def __str__(self):
    return 'go'


class NodeLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = str(self)

  def client_args(self):
    return ['node', 'src/node/interop/interop_client.js']

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def server_args(self):
    return ['node', 'src/node/interop/interop_server.js', '--use_tls=true']

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def __str__(self):
    return 'node'


class PHPLanguage:

  def __init__(self):
    self.client_cwd = None
    self.safename = str(self)

  def client_args(self):
    return ['src/php/bin/interop_client.sh']

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def __str__(self):
    return 'php'


class RubyLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = str(self)

  def client_args(self):
    return ['ruby', 'src/ruby/bin/interop/interop_client.rb']

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def server_args(self):
    return ['ruby', 'src/ruby/bin/interop/interop_server.rb', '--use_tls=true']

  def global_env(self):
    return {}

  def unimplemented_test_cases(self):
    return []

  def __str__(self):
    return 'ruby'


class PythonLanguage:

  def __init__(self):
    self.client_cwd = None
    self.server_cwd = None
    self.safename = str(self)

  def client_args(self):
    return ['python2.7_virtual_environment/bin/python', '-m', 'grpc_interop.client']

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def server_args(self):
    return ['python2.7_virtual_environment/bin/python', '-m', 'grpc_interop.server', '--use_tls=true']

  def global_env(self):
    return {'LD_LIBRARY_PATH': 'libs/opt'}

  def unimplemented_test_cases(self):
    return ['jwt_token_creds', 'per_rpc_creds']

  def __str__(self):
    return 'python'


_LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'go' : GoLanguage(),
    'java' : JavaLanguage(),
    'node' : NodeLanguage(),
    'php' :  PHPLanguage(),
    'ruby' : RubyLanguage(),
    'python' : PythonLanguage(),
}

# languages supported as cloud_to_cloud servers
_SERVERS = ['c++', 'node', 'csharp', 'java', 'go', 'ruby', 'python']

_TEST_CASES = ['large_unary', 'empty_unary', 'ping_pong',
               'empty_stream', 'client_streaming', 'server_streaming',
               'cancel_after_begin', 'cancel_after_first_response',
               'timeout_on_sleeping_server']

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
    if language in ['csharp', 'node', 'php', 'python', 'ruby']:
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


def _job_kill_handler(job):
  if job._spec.container_name:
    dockerjob.docker_kill(job._spec.container_name)


def cloud_to_prod_jobspec(language, test_case, docker_image=None, auth=False):
  """Creates jobspec for cloud-to-prod interop test"""
  cmdline = language.client_args() + [
      '--server_host_override=grpc-test.sandbox.google.com',
      '--server_host=grpc-test.sandbox.google.com',
      '--server_port=443',
      '--use_tls=true',
      '--test_case=%s' % test_case]
  cwd = language.client_cwd
  environ = dict(language.cloud_to_prod_env(), **language.global_env())
  container_name = None
  if auth:
    cmdline, environ = add_auth_options(language, test_case, cmdline, environ)
  cmdline = bash_login_cmdline(cmdline)

  if docker_image:
    container_name = dockerjob.random_name('interop_client_%s' % language.safename)
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
          shortname='%s:%s:%s' % (suite_name, language, test_case),
          timeout_seconds=2*60,
          flake_retries=5 if args.allow_flakes else 0,
          timeout_retries=2 if args.allow_flakes else 0,
          kill_handler=_job_kill_handler)
  test_job.container_name = container_name
  return test_job


def cloud_to_cloud_jobspec(language, test_case, server_name, server_host,
                           server_port, docker_image=None):
  """Creates jobspec for cloud-to-cloud interop test"""
  cmdline = bash_login_cmdline(language.client_args() +
                               ['--server_host_override=foo.test.google.fr',
                                '--use_tls=true',
                                '--use_test_ca=true',
                                '--test_case=%s' % test_case,
                                '--server_host=%s' % server_host,
                                '--server_port=%s' % server_port])
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
          timeout_seconds=2*60,
          flake_retries=5 if args.allow_flakes else 0,
          timeout_retries=2 if args.allow_flakes else 0,
          kill_handler=_job_kill_handler)
  test_job.container_name = container_name
  return test_job


def server_jobspec(language, docker_image):
  """Create jobspec for running a server"""
  container_name = dockerjob.random_name('interop_server_%s' % language.safename)
  cmdline = bash_login_cmdline(language.server_args() +
                               ['--port=%s' % _DEFAULT_SERVER_PORT])
  environ = language.global_env()
  docker_cmdline = docker_run_cmdline(cmdline,
                                      image=docker_image,
                                      cwd=language.server_cwd,
                                      environ=environ,
                                      docker_args=['-p', str(_DEFAULT_SERVER_PORT),
                                                   '--name', container_name])
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
  # TODO(stanleycheung): find a more elegant way to do this
  if language.safename == 'php' and os.path.exists('/var/local/.composer/auth.json'):
    env['BUILD_INTEROP_DOCKER_EXTRA_ARGS'] = \
      '-v /var/local/.composer/auth.json:/root/.composer/auth.json:ro'
  build_job = jobset.JobSpec(
          cmdline=['tools/jenkins/build_interop_image.sh'],
          environ=env,
          shortname='build_docker_%s' % (language),
          timeout_seconds=30*60)
  build_job.tag = tag
  return build_job


# TODO(adelez): Use mako template.
def fill_one_test_result(shortname, resultset, html_str):
  if shortname in resultset:
    result = resultset[shortname]
    if result.state == 'PASSED':
      html_str = '%s<td bgcolor=\"green\">PASS</td>\n' % html_str
    else:
      tooltip = ''
      if result.returncode > 0 or result.message:
        if result.returncode > 0:
          tooltip = 'returncode: %d ' % result.returncode
        if result.message:
          tooltip = '%smessage: %s' % (tooltip, result.message)     
      if result.state == 'FAILED':
        html_str = '%s<td bgcolor=\"red\">' % html_str
        if tooltip:  
          html_str = ('%s<a href=\"#\" data-toggle=\"tooltip\" '
                      'data-placement=\"auto\" title=\"%s\">FAIL</a></td>\n' % 
                      (html_str, tooltip))
        else:
          html_str = '%sFAIL</td>\n' % html_str
      elif result.state == 'TIMEOUT':
        html_str = '%s<td bgcolor=\"yellow\">' % html_str
        if tooltip:
          html_str = ('%s<a href=\"#\" data-toggle=\"tooltip\" '
                      'data-placement=\"auto\" title=\"%s\">TIMEOUT</a></td>\n' 
                      % (html_str, tooltip))
        else:
          html_str = '%sTIMEOUT</td>\n' % html_str
  else:
    html_str = '%s<td bgcolor=\"magenta\">Not implemented</td>\n' % html_str
  
  return html_str


def render_html_report(test_cases, client_langs, server_langs, resultset,
                       num_failures):
  """Generate html report."""
  sorted_test_cases = sorted(test_cases)
  sorted_client_langs = sorted(client_langs)
  print sorted_client_langs
  sorted_server_langs = sorted(server_langs)
  html_str = ('<!DOCTYPE html>\n'
              '<html lang=\"en\">\n'
              '<head><title>Interop Test Result</title></head>\n'
              '<body>\n')
  if num_failures > 1:
    html_str = (
        '%s<p><h2><font color=\"red\">%d tests failed!</font></h2></p>\n' % 
        (html_str, num_failures))
  elif num_failures:
    html_str = (
        '%s<p><h2><font color=\"red\">%d test failed!</font></h2></p>\n' % 
        (html_str, num_failures))
  else:
    html_str = (
        '%s<p><h2><font color=\"green\">All tests passed!</font></h2></p>\n' % 
        html_str)
  if args.cloud_to_prod_auth or args.cloud_to_prod:
    # Each column header is the client language.
    html_str = ('%s<h2>Cloud to Prod</h2>\n' 
                '<table style=\"width:100%%\" border=\"1\">\n'
                '<tr bgcolor=\"#00BFFF\">\n'
                '<th/>\n') % html_str
    for client_lang in sorted_client_langs:
      html_str = '%s<th>%s\n' % (html_str, client_lang)
    html_str = '%s</tr>\n' % html_str
    for test_case in sorted_test_cases:
      html_str = '%s<tr><td><b>%s</b></td>\n' % (html_str, test_case)
      for client_lang in sorted_client_langs:
        if args.cloud_to_prod:
          shortname = 'cloud_to_prod:%s:%s' % (client_lang, test_case)
        else:
          shortname = 'cloud_to_prod_auth:%s:%s' % (client_lang, test_case)
        html_str = fill_one_test_result(shortname, resultset, html_str)       
      html_str = '%s</tr>\n' % html_str 
    html_str = '%s</table>\n' % html_str
  if servers:
    for test_case in sorted_test_cases:
      # Each column header is the client language.
      html_str = ('%s<h2>%s</h2>\n' 
                  '<table style=\"width:100%%\" border=\"1\">\n'
                  '<tr bgcolor=\"#00BFFF\">\n'
                  '<th/>\n') % (html_str, test_case)
      for client_lang in sorted_client_langs:
        html_str = '%s<th>%s\n' % (html_str, client_lang)
      html_str = '%s</tr>\n' % html_str
      # Each row head is the server language.
      for server_lang in sorted_server_langs:
        html_str = '%s<tr><td><b>%s</b></td>\n' % (html_str, server_lang)
        # Fill up the cells with test result.
        for client_lang in sorted_client_langs:
          shortname = 'cloud_to_cloud:%s:%s_server:%s' % (
              client_lang, server_lang, test_case)
          html_str = fill_one_test_result(shortname, resultset, html_str)
        html_str = '%s</tr>\n' % html_str
      html_str = '%s</table>\n' % html_str

  html_str = ('%s\n'
              '<script>\n'
              '$(document).ready(function(){'
              '$(\'[data-toggle=\"tooltip\"]\').tooltip();\n'   
              '});\n'
              '</script>\n'
              '</body>\n'
              '</html>') % html_str  
  
  # Write to reports/index.html as set up in Jenkins plugin.
  html_report_dir = 'reports'
  if not os.path.exists(html_report_dir):
    os.mkdir(html_report_dir)
  html_file_path = os.path.join(html_report_dir, 'index.html')
  with open(html_file_path, 'w') as f:
    f.write(html_str)


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
  print 'Running interop servers is only supported with --use_docker option enabled.'
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
        if not test_case in language.unimplemented_test_cases():
          test_job = cloud_to_prod_jobspec(language, test_case,
                                           docker_image=docker_images.get(str(language)))
          jobs.append(test_job)

  if args.cloud_to_prod_auth:
    for language in languages:
      for test_case in _AUTH_TEST_CASES:
        if not test_case in language.unimplemented_test_cases():
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
        if not test_case in language.unimplemented_test_cases():
          test_job = cloud_to_cloud_jobspec(language,
                                            test_case,
                                            server_name,
                                            server_host,
                                            server_port,
                                            docker_image=docker_images.get(str(language)))
          jobs.append(test_job)

  if not jobs:
    print 'No jobs to run.'
    for image in docker_images.itervalues():
      dockerjob.remove_image(image, skip_nonexistent=True)
    sys.exit(1)

  root = ET.Element('testsuites')
  testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

  num_failures, resultset = jobset.run(jobs, newline_on_success=True, 
                                       maxjobs=args.jobs, xml_report=testsuite)
  if num_failures:
    jobset.message('FAILED', 'Some tests failed', do_newline=True)
  else:
    jobset.message('SUCCESS', 'All tests passed', do_newline=True)

  tree = ET.ElementTree(root)
  tree.write('report.xml', encoding='UTF-8')
  
  # Generate HTML report.
  render_html_report(_TEST_CASES, set([str(l) for l in languages]), servers, 
                     resultset, num_failures)

finally:
  # Check if servers are still running.
  for server, job in server_jobs.iteritems():
    if not job.is_running():
      print 'Server "%s" has exited prematurely.' % server

  dockerjob.finish_jobs([j for j in server_jobs.itervalues()])

  for image in docker_images.itervalues():
    print 'Removing docker image %s' % image
    dockerjob.remove_image(image)
