#!/usr/bin/env python
# Copyright 2017 gRPC authors.
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
#
# This script is invoked by a Jenkins pull request job and executes all
# args passed to this script if the pull request affect C/C++ code

import subprocess
import re
import sys
import os

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils
import name_resolution.dns_records_config as dns_records_config

_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(_ROOT)

def _fail_error(cmd, found, expected):
  expected_str = ''
  for e in expected:
    expected_str += ' '.join(e)
    expected_str += '\n'
  jobset.message('FAILED', ('A dns records sanity check failed.\n'
                            '\"%s\" yielded bad record: \n'
                            '  Found: \n'
                            '    %s\n  Expected: \n'
                            '    %s') % (cmd, found and ' '.join(found), expected_str))
  sys.exit(1)


def _matches_any(parsed_record, candidates):
  for e in candidates:
    if len(e) == len(parsed_record):
      matches = True
      for i in range(len(e)):
        if e[i] != parsed_record[i]:
          matches = False
      if matches:
        return True
  return False


def check_dns_record(command, expected_data):
  output = subprocess.check_output(re.split('\s+', command))
  lines = output.splitlines()

  found = None
  i = 0
  for l in lines:
    l = l.strip()
    if l == ';; ANSWER SECTION:':
      found = i
      break
    i += 1

  if found is None or len(expected_data) > len(lines) - found:
    _fail_error(command, found, expected_data)
  for l in lines[found:found+len(expected_data)]:
    parsed = re.split('\s+', lines[i + 1])
    if not _matches_any(parsed, expected_data):
      _fail_error(command, parsed, expected_data)


def sanity_check_dns_records(dns_records):
  for r in dns_records:
    cmd = 'dig %s %s' % (r.record_type, r.record_name)
    expected_data = []
    for d in r.record_data.split(','):
      expected_line = '%s %s %s %s %s' % (r.record_name, r.ttl, r.record_class, r.record_type, d)
      expected_data.append(expected_line.split(' '))

      check_dns_record(
        command=cmd,
        expected_data=expected_data)


def shortname(name, cmd, environ={}):
  env_str = ''
  for k in environ.keys():
    env_str += '%s=%s,' % (k, environ[k])
  return '%s - %s - %s' % (name, ' '.join(cmd), env_str)


class CLanguage(object):
  def __init__(self):
    self.name = 'c-core'
    self.build_config = config = os.environ.get('CONFIG', None) or 'opt'

  def build_cmd(self):
    cmd = 'make resolve_srv_records CC=gcc CXX=g++ LD=gcc LDXX=g++'.split(' ')
    env = { 'CONFIG': self.build_config }
    return [jobset.JobSpec(cmd, shortname=shortname(l.name, cmd), environ=env)]

  def create_ares_srv_jobspec(self, srv_record, cmd):
    expected_addrs = dns_records_config.expected_result_for_srv_record(
      srv_record, dns_records_config.DNS_RECORDS)
    grpc_ares_friendly_name = srv_record.record_name.replace(
      '_grpclb._tcp.', '')
    env = {
      'GRPC_POLL_STRATEGY': 'all', #TODO(apolcyn) change this?
      'GRPC_VERBOSITY': 'DEBUG',
      'GRPC_DNS_RESOLVER': 'ares',
      'GRPC_DNS_TEST_SRV_RECORD_NAME': grpc_ares_friendly_name,
      'GRPC_DNS_TEST_A_RECORD_NAME': '',
      'GRPC_DNS_TEST_EXPECTED_ADDRS': expected_addrs
    }
    return jobset.JobSpec(cmd,
                          shortname=shortname(self.name, cmd, environ=env),
                          environ=env)

  def create_a_record_jobspec(self, a_record, resolver, cmd):
    assert a_record.record_type == 'A' or a_record.record_type == 'AAAA'
    expected_addrs = dns_records_config.expected_result_for_a_record(
      a_record)
    env = {
      'GRPC_POLL_STRATEGY': 'all', #TODO(apolcyn) change this?
      'GRPC_VERBOSITY': 'DEBUG',
      'GRPC_DNS_RESOLVER': resolver,
      'GRPC_DNS_TEST_SRV_RECORD_NAME': '',
      'GRPC_DNS_TEST_A_RECORD_NAME': a_record.record_name,
      'GRPC_DNS_TEST_EXPECTED_ADDRS': expected_addrs,
    }
    return jobset.JobSpec(cmd,
                          shortname=shortname(self.name, cmd, environ=env),
                          environ=env)

  def test_runner_cmd(self):
    specs = []
    cmd = ['%s/bins/%s/resolve_srv_records' % (_ROOT, self.build_config)]
    for r in dns_records_config.DNS_RECORDS:
      if r.record_type == 'SRV':
        # SRV records are only currently resolveable by the ares resolver
        specs.append(self.create_ares_srv_jobspec(r, cmd))
      else:
        assert r.record_type == 'A' or r.record_type == 'AAAA'
        for resolver in ['native', 'ares']:
          specs.append(self.create_a_record_jobspec(r, resolver, cmd))
    return specs

class JavaLanguage(object):
  def __init__(self):
    self.name = 'java'

  def build_cmd(self):
    cmd = 'echo TODO: add java build cmd'.split(' ')
    return [jobset.JobSpec(cmd, shortname=shortname(l.name, cmd))]

  def test_runner_cmd(self):
    cmd = 'echo TODO: add java test runner cmd'.split(' ')
    return [jobset.JobSpec(cmd, shortname=shortname(l.name, cmd))]

class GoLanguage(object):
  def __init__(self):
    self.name = 'go'

  def build_cmd(self):
    cmd = 'echo TODO: add go build cmd'.split(' ')
    return [jobset.JobSpec(cmd, shortname=shortname(l.name, cmd))]

  def test_runner_cmd(self):
    cmd = 'echo TODO: add go test runner cmd'.split(' ')
    return [jobset.JobSpec(cmd, shortname=shortname(l.name, cmd))]

languages = [CLanguage(), JavaLanguage(), GoLanguage()]

results = {}

sanity_check_dns_records(dns_records_config.DNS_RECORDS)

build_jobs = []
run_jobs = []
for l in languages:
  build_jobs.extend(l.build_cmd())
  run_jobs.extend(l.test_runner_cmd())

def _build():
  num_failures, _ = jobset.run(
    build_jobs, maxjobs=3, stop_on_failure=True,
    newline_on_success=True)
  return num_failures

def _run():
  num_failures, resultset = jobset.run(
    run_jobs, maxjobs=3, stop_on_failure=True,
    newline_on_success=True)

  report_utils.render_junit_xml_report(resultset, 'naming.xml',
    suite_name='naming_test')

  return num_failures

if _build():
  jobset.message('FAILED', 'Some of the tests failed to build\n')
  sys.exit(1)

if _run():
  jobset.message('FAILED', 'Some tests failed (but the build succeeded)\n')
  sys.exit(2)
