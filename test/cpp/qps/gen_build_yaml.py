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

import json
import pipes
import shutil
import sys
import os
import yaml

run_tests_root = os.path.abspath(os.path.join(
    os.path.dirname(sys.argv[0]),
    '../../../tools/run_tests'))
sys.path.append(run_tests_root)

import performance.scenario_config as scenario_config

def _scenario_json_string(scenario_json):
  # tweak parameters to get fast test times
  scenario_json['warmup_seconds'] = 1
  scenario_json['benchmark_seconds'] = 1
  return json.dumps(scenario_config.remove_nonproto_fields(scenario_json))

def threads_of_type(scenario_json, path):
  d = scenario_json
  for el in path.split('/'):
    if el not in d:
      return 0
    d = d[el]
  return d

def guess_cpu(scenario_json):
  client = threads_of_type(scenario_json, 'client_config/async_client_threads')
  server = threads_of_type(scenario_json, 'server_config/async_server_threads')
  # make an arbitrary guess if set to auto-detect
  # about the size of the jenkins instances we have for unit tests
  if client == 0: client = 8
  if server == 0: server = 8
  return (scenario_json['num_clients'] * client +
          scenario_json['num_servers'] * server)

print yaml.dump({
  'tests': [
    {
      'name': 'json_run_localhost',
      'shortname': 'json_run_localhost:%s' % scenario_json['name'],
      'args': ['--scenario_json',
               pipes.quote(_scenario_json_string(scenario_json))],
      'ci_platforms': ['linux', 'mac', 'posix', 'windows'],
      'platforms': ['linux', 'mac', 'posix', 'windows'],
      'flaky': False,
      'language': 'c++',
      'boringssl': True,
      'defaults': 'boringssl',
      'cpu_cost': guess_cpu(scenario_json),
      'exclude_configs': []
    }
    for scenario_json in scenario_config.CXXLanguage().scenarios()
  ]
})
