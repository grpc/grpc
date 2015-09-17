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


_CLOUD_TO_PROD_BASE_ARGS = [
    '--server_host_override=grpc-test.sandbox.google.com',
    '--server_host=grpc-test.sandbox.google.com',
    '--server_port=443']

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

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(_LANGUAGES),
                  nargs='+',
                  default=['all'])
args = argp.parse_args()

languages = set(_LANGUAGES[l]
                for l in itertools.chain.from_iterable(
                      _LANGUAGES.iterkeys() if x == 'all' else [x]
                      for x in args.language))

# TODO(jtattermusch): make python script generate cmdline params for interop
# tests. It's easier to manage than in a shell script.
jobs = []
jobNumber = 0
for language in languages:
  for test_case in _TEST_CASES:
    test_job = cloud_to_prod_jobspec(language, test_case)
    jobs.append(test_job)
    jobNumber+=1

root = ET.Element('testsuites')
testsuite = ET.SubElement(root, 'testsuite', id='1', package='grpc', name='tests')

jobset.run(jobs, maxjobs=jobNumber, xml_report=testsuite)

tree = ET.ElementTree(root)
tree.write('report.xml', encoding='UTF-8')


