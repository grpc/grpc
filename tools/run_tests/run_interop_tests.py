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
import itertools
import xml.etree.cElementTree as ET
import jobset
import os
import subprocess
import sys
import time


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

# TODO(jtatttermusch) unify usage of --enable_ssl, --use_tls and --use_tls=true


class CXXLanguage:

  def __init__(self):
    self.client_cmdline_base = ['bins/opt/interop_client']
    self.client_cwd = None

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--enable_ssl','--use_prod_roots'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--enable_ssl'])

  def cloud_to_prod_env(self):
    return None

  def __str__(self):
    return 'c++'


class CSharpLanguage:

  def __init__(self):
    self.client_cmdline_base = ['mono', 'Grpc.IntegrationTesting.Client.exe']
    self.client_cwd = 'src/csharp/Grpc.IntegrationTesting.Client/bin/Debug'

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls', '--use_test_ca'])

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def __str__(self):
    return 'csharp'


class NodeLanguage:

  def __init__(self):
    self.client_cmdline_base = ['node', 'src/node/interop/interop_client.js']
    self.client_cwd = None

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls=true'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls=true', '--use_test_ca=true'])

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

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

  def cloud_to_prod_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_PROD_BASE_ARGS +
            ['--use_tls'])

  def cloud_to_cloud_args(self):
    return (self.client_cmdline_base + _CLOUD_TO_CLOUD_BASE_ARGS +
            ['--use_tls', '--use_test_ca'])

  def cloud_to_prod_env(self):
    return _SSL_CERT_ENV

  def __str__(self):
    return 'ruby'


# TODO(jtattermusch): add php and python once we get them working
_LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'node' : NodeLanguage(),
    'php' :  PHPLanguage(),
    'ruby' : RubyLanguage(),
}

# languages supported as cloud_to_cloud servers 
# TODO(jtattermusch): enable other languages as servers as well
_SERVERS = { 'c++' : 8010, 'node' : 8040, 'csharp': 8070 }

# TODO(jtattermusch): add empty_stream once C++ start supporting it.
# TODO(jtattermusch): add support for auth tests.
_TEST_CASES = ['large_unary', 'empty_unary', 'ping_pong',
               'client_streaming', 'server_streaming',
               'cancel_after_begin', 'cancel_after_first_response',
               'timeout_on_sleeping_server']


def cloud_to_prod_jobspec(language, test_case):
  """Creates jobspec for cloud-to-prod interop test"""
  cmdline = language.cloud_to_prod_args() + ['--test_case=%s' % test_case]
  test_job = jobset.JobSpec(
          cmdline=cmdline,
          cwd=language.client_cwd,
          shortname="cloud_to_prod:%s:%s" % (language, test_case),
          environ=language.cloud_to_prod_env(),
          timeout_seconds=60)
  return test_job


def cloud_to_cloud_jobspec(language, test_case, server_name, server_host,
                           server_port):
  """Creates jobspec for cloud-to-cloud interop test"""
  cmdline = language.cloud_to_cloud_args() + ['--test_case=%s' % test_case,
     '--server_host=%s' % server_host,
     '--server_port=%s' % server_port ]
  test_job = jobset.JobSpec(
          cmdline=cmdline,
          cwd=language.client_cwd,
          shortname="cloud_to_cloud:%s:%s_server:%s" % (language, server_name,
                                                 test_case),
          timeout_seconds=60)
  return test_job

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'],
                  help='Clients to run.')
argp.add_argument('-j', '--jobs', default=24, type=int)
argp.add_argument('--cloud_to_prod',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Run cloud_to_prod tests.')
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
args = argp.parse_args()

servers = set(s for s in itertools.chain.from_iterable(_SERVERS.iterkeys()
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

  child_argv = [ arg for arg in sys.argv if not arg == '--use_docker' ]
  run_tests_cmd = ('tools/run_tests/run_interop_tests.py %s' %
                   " ".join(child_argv[1:]))

  # cmdline args to pass to the container running servers.
  servers_extra_docker_args = ''
  server_port_tuples = ''
  for server in servers:
    port = _SERVERS[server]
    servers_extra_docker_args += ' -p %s' % port
    servers_extra_docker_args += ' -e SERVER_PORT_%s=%s' % (server.replace("+", "x"), port)
    server_port_tuples += ' %s:%s' % (server, port)

  env = os.environ.copy()
  env['RUN_TESTS_COMMAND'] = run_tests_cmd
  env['SERVERS_DOCKER_EXTRA_ARGS'] = servers_extra_docker_args
  env['SERVER_PORT_TUPLES'] = server_port_tuples
  if not args.travis:
    env['TTY_FLAG'] = '-t'  # enables Ctrl-C when not on Jenkins.

  subprocess.check_call(['tools/jenkins/build_docker_and_run_interop_tests.sh'],
                        shell=True,
                        env=env)
  sys.exit(0)

languages = set(_LANGUAGES[l]
                for l in itertools.chain.from_iterable(
                      _LANGUAGES.iterkeys() if x == 'all' else [x]
                      for x in args.language))

jobs = []
if args.cloud_to_prod:
  for language in languages:
    for test_case in _TEST_CASES:
      test_job = cloud_to_prod_jobspec(language, test_case)
      jobs.append(test_job)

# default servers to "localhost" and the default port
server_addresses = dict((s, ("localhost", _SERVERS[s])) for s in servers)

for server in args.override_server:
  server_name = server[0]
  (server_host, server_port) = server[1].split(":")
  server_addresses[server_name] = (server_host, server_port)

for server_name, server_address in server_addresses.iteritems():
  (server_host, server_port) = server_address
  for language in languages:
    for test_case in _TEST_CASES:
      test_job = cloud_to_cloud_jobspec(language,
                                        test_case,
                                        server_name,
                                        server_host,
                                        server_port)
      jobs.append(test_job)

if not jobs:
  print "No jobs to run."
  sys.exit(1)

root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

if jobset.run(jobs, newline_on_success=True, maxjobs=args.jobs, xml_report=testsuite):
  jobset.message('SUCCESS', 'All tests passed', do_newline=True)
else:
  jobset.message('FAILED', 'Some tests failed', do_newline=True)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')